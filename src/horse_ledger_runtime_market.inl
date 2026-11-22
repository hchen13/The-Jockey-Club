// Horse market quote/commit transactions and membership persistence.
// Included within runtime at the original definition site; save-state sequencing is unchanged.
namespace {
bool PreviewObservation(const save_observer::Observation& observation){
    const auto& r=observation.raw;
    return observation.healthy&&!r.pending&&!r.load_state&&
        (r.operation==0||r.operation==2||r.operation==6||r.operation==7);
}
bool MarketReady(std::string& reason,bool mutation=true) {
    auto& s=S();
    if(!s.enabled||!s.sales){reason="Horse sales are unavailable.";return false;}
    Pump(true);
    if(!s.enabled||!s.ledger.Active()){
        reason=s.archive_issue.empty()?"The current save archive is not ready.":s.archive_issue;return false;
    }
    if(s.market_blocked_timeline==s.timeline){
        reason=i18n::Text("An earlier horse sale needs review. Reload a save before trading again.",
            "上一笔卖马交易尚未确认，请重新载入存档后再交易。");return false;
    }
    if(s.racing){reason="Finish or cancel the current race before trading.";return false;}
    if(s.payment){reason="A race payment is still pending.";return false;}
    if(s.sale_in_progress){reason="A market transaction is already in progress.";return false;}
    if(s.loading||s.restoring||s.waiting_world){reason="The current save is still loading.";return false;}
    if(!WorldReady()){reason="The game world is not ready for trading.";return false;}
    const auto observed=save_observer::Latest();
    if(!PreviewObservation(observed)||(mutation&&Busy(observed))){reason="A save or load operation is still active.";return false;}
    return true;
}
bool SameHorse(const individual_identity::Identity& a,const individual_identity::Identity& b){
    return a.guid==b.guid&&a.model==b.model&&a.slot==b.slot;
}
}
std::optional<MarketQuote> QuoteHorse(int horse,Cents base,std::string& reason) {
    if(!MarketReady(reason,false))return {};
    const auto seller=CurrentSeller();
    if(seller==SellerEra::Unknown){reason="Horse sales require Arthur or John.";return {};}
    auto& s=S();
    const auto identity=individual_identity::Capture(horse,reason);
    if(!identity){reason="Owned horse identity unavailable: "+reason;return {};}
    const auto total=s.ledger.Quote(identity->guid,base,true);
    if(!total){reason=s.ledger.Sold(identity->guid)?"This horse has already been sold.":
        "The club only buys horses with an official race victory.";return {};}
    if(*total>std::numeric_limits<int>::max()){
        reason="This horse's price exceeds the game's cash limit.";return {};
    }
    MarketQuote quote;quote.identity=*identity;quote.revision=*s.ledger.Active();
    quote.timeline=s.timeline;quote.epoch=s.ledger.Epoch();quote.mutation=s.ledger.Mutation();
    quote.live_wins=s.ledger.Wins();
    quote.sale.event_id=EventId();quote.sale.horse=identity->guid;quote.sale.base_cents=base;
    quote.sale.total_cents=*total;quote.sale.premium_cents=*total-base;quote.sale.unix_ms=UnixMs();
    quote.sale.seller=seller;
    if(quote.sale.event_id.empty()){reason="Could not prepare the sale receipt.";return {};}
    for(const auto& win:s.ledger.Wins())if(win.horse==identity->guid&&!win.practice){
        quote.wins.push_back(win);quote.sale.win_event_ids.push_back(win.event_id);
    }
    reason.clear();return quote;
}
bool CheckQuote(const MarketQuote& quote,int horse,std::string& reason) {
    if(!MarketReady(reason,false))return false;
    auto& s=S();
    if(quote.sale.seller==SellerEra::Unknown||quote.sale.seller!=CurrentSeller()||
       quote.timeline!=s.timeline||quote.epoch!=s.ledger.Epoch()||quote.mutation!=s.ledger.Mutation()||
       !s.ledger.Active()||quote.revision.profile!=s.profile||quote.revision.profile!=s.ledger.Active()->profile||quote.live_wins!=s.ledger.Wins()){
        reason="The save or race history changed. Open Sell Horse again.";return false;
    }
    const auto identity=individual_identity::Capture(horse,reason);
    if(!identity||!SameHorse(*identity,quote.identity)){
        reason="The horse changed. Open Sell Horse again.";return false;
    }
    const auto total=s.ledger.Quote(identity->guid,quote.sale.base_cents,true);
    std::vector<Win> wins;
    for(const auto& win:s.ledger.Wins())if(win.horse==identity->guid&&!win.practice)wins.push_back(win);
    std::vector<std::string> ids;for(const auto& win:wins)ids.push_back(win.event_id);
    if(!total||*total!=quote.sale.total_cents||quote.sale.horse!=identity->guid||
       quote.sale.premium_cents!=*total-quote.sale.base_cents||wins!=quote.wins||
       ids!=quote.sale.win_event_ids||!Validate(quote.sale)){
        reason="The quoted price changed. Open Sell Horse again.";return false;
    }
    reason.clear();return true;
}
MarketTransactionState TradingReadiness(std::string& reason){
    if(!MarketReady(reason,false))return MarketTransactionState::Invalid;
    if(Busy(save_observer::Latest())){
        reason="The game is saving. Please wait before selling.";return MarketTransactionState::Saving;
    }
    reason.clear();return MarketTransactionState::Ready;
}
MarketTransactionState ValidateSaleForCommit(const MarketQuote& quote,int horse,std::string& reason){
    if(!CheckQuote(quote,horse,reason))return MarketTransactionState::Invalid;
    if(Busy(save_observer::Latest())){
        reason="The game is saving. Please wait before selling.";return MarketTransactionState::Saving;
    }
    reason.clear();return MarketTransactionState::Ready;
}
SaleCompletion CommitSale(const MarketQuote& preview,int horse,
    const std::function<RemovalOutcome(std::string&)>& remove) {
    ReofferArchiveOnExit reoffer;
    CloseStartup("horse sale attempt");
    std::string reason;
    if(ValidateSaleForCommit(preview,horse,reason)!=MarketTransactionState::Ready)return {false,false,reason};
    auto& s=S();
    // Only a fully revalidated same-world preview may follow an accepted save.
    // The caller's quote remains frozen; every mutation/receipt pins this copy.
    auto quote=preview;quote.revision=*s.ledger.Active();
    const int cash=MONEY::_MONEY_GET_CASH_BALANCE();
    if(cash<0||quote.sale.total_cents>std::numeric_limits<int>::max()-static_cast<Cents>(cash))
        return {false,false,"The sale would exceed the game's cash limit."};
    const auto pack=save_observer::Latest().raw.pack_counter;
    bool entered=false,wallet_attempted=false;
    struct ProgressGuard {State& s;~ProgressGuard(){s.sale_in_progress=false;}} guard{s};
    RevokeArchiveMirror();
    s.sale_in_progress=true;
    TransactionPins pins{s,quote.revision,quote.sale.event_id};
    if(!RefreshArchivePins("sale transaction"))return {false,false,"Could not retain the sale archive. No sale was attempted."};
    const auto journal=[&](SaleStage stage){
        const auto result=s.store->SaleJournal(quote.revision,quote.sale,stage,reason);
        if(result==WriteResult::Error||result==WriteResult::Conflict)WarnArchiveSave();
        return result==WriteResult::Written; // A previous attempt is NEVER replayed.
    };
    const auto uncertain=[&](const std::string& detail)->SaleCompletion{
        Log("sale uncertain event="+quote.sale.event_id+" guid="+quote.sale.horse.Key()+
            " wallet_attempted="+std::to_string(wallet_attempted)+" reason="+detail);
        if(s.timeline==quote.timeline){
            if(wallet_attempted)Quarantine("sale payment outcome needs reconciliation; no receipt replay");
            else if(s.ledger.Active()&&s.ledger.Active()->profile==quote.revision.profile){
                // No wallet call or sold append occurred. The existing wins and
                // campaign remain truthful even if native ownership partly changed.
                // Preserve their ordinary save captures; lock only trading until
                // a proven load. A missing horse cannot pass the next exact GUID,
                // slot and quantity-one sale preflight. Receipts are never replayed.
                s.market_blocked_timeline=s.timeline;s.market_blocked_event=quote.sale.event_id;
                Log("market locked pending load; working archive retained event="+quote.sale.event_id);
            }
        }
        if(!wallet_attempted)return {false,true,i18n::Text(
            "The sale could not be confirmed. No money was paid. Reload a save before trading again; race records remain available.",
            "交易未能确认，未支付钱款。请重新载入存档后再交易，赛马记录仍然保留。")};
        return {false,true,"Sale could not be confirmed. Reload your pre-sale save before continuing."};
    };
    const auto unchanged=[&](bool entered_transaction=false){
        Pump(true);const auto observation=save_observer::Latest();
        return s.enabled&&s.timeline==quote.timeline&&s.ledger.Active()&&
            s.ledger.Active()->profile==quote.revision.profile&&s.ledger.Epoch()==quote.epoch&&
            s.ledger.Mutation()==quote.mutation&&WorldReady()&&CurrentSeller()==quote.sale.seller&&
            (entered_transaction?PreviewObservation(observation):
                (*s.ledger.Active()==quote.revision&&!Busy(observation)&&observation.raw.pack_counter==pack));
    };
    try {
        if(!journal(SaleStage::Prepared))return {false,false,"Could not write the sale receipt. No sale was attempted."};
        // Filesystem writes may take time: read the engine observation again
        // immediately before entering ownership mutation.
        if(!unchanged())return {false,false,"The save changed. No sale was attempted."};
        if(!journal(SaleStage::RemovalEntered))return {false,false,"Could not prepare ownership transfer. No sale was attempted."};
        if(!unchanged())return {false,false,"The save changed. No sale was attempted."};
        entered=true;
        Log("sale removal entered event="+quote.sale.event_id+" guid="+quote.sale.horse.Key()+
            " slot="+std::to_string(quote.identity.slot)+" total_cents="+std::to_string(quote.sale.total_cents));
        const auto removed=remove(reason);
        if(removed==RemovalOutcome::NotStarted){Log("sale not started: "+reason);return {false,false,reason};}
        if(removed==RemovalOutcome::Unchanged){
            Log("sale cancelled unchanged event="+quote.sale.event_id+" guid="+quote.sale.horse.Key()+" reason="+reason);
            return {false,false,i18n::Text("The sale could not be completed. Your horse and money are unchanged.",
                "交易未完成。马匹仍归你所有，钱款未变。")};
        }
        if(removed!=RemovalOutcome::Removed)return uncertain(reason);
        if(!journal(SaleStage::Removed))return uncertain("removed receipt: "+reason);
        if(!unchanged(true))return uncertain("world changed during ownership transfer");
        if(!journal(SaleStage::PayoutEntered))return uncertain("payout receipt: "+reason);
        if(!unchanged(true))return uncertain("world changed during payout preparation");
        if(MONEY::_MONEY_GET_CASH_BALANCE()!=cash)return uncertain("wallet changed during ownership transfer");
        wallet_attempted=true;
        const bool paid=MONEY::_MONEY_INCREMENT_CASH_BALANCE(static_cast<int>(quote.sale.total_cents),
            MISC::GET_HASH_KEY("ADD_REASON_DEFAULT"));
        const int after=MONEY::_MONEY_GET_CASH_BALANCE();
        if(!paid||static_cast<Cents>(after)-cash!=quote.sale.total_cents)return uncertain("cash result not confirmed");
        if(!journal(SaleStage::Paid))return uncertain("paid receipt: "+reason);
        if(!unchanged(true))return uncertain("world changed during confirmed payment");
        if(s.ledger.MarkSold(quote.sale,true)!=AddResult::Added)return uncertain("sold record rejected");
        s.mutation_tick=GetTickCount64();s.last_horse.clear();
        Log("sale paid event="+quote.sale.event_id+" guid="+quote.sale.horse.Key()+
            " seller="+std::to_string(static_cast<int>(quote.sale.seller))+
            " total_cents="+std::to_string(quote.sale.total_cents)+" cash_before="+std::to_string(cash)+
            " cash_after="+std::to_string(after));
        return {true,false,"Horse sold."};
    }catch(const std::exception& error){
        if(entered)return uncertain(error.what());
        Log(std::string("sale preparation failed: ")+error.what());
        return {false,false,"Could not prepare the sale. No sale was attempted."};
    }
}
SellerEra CurrentSeller(){
    const auto player=PLAYER::PLAYER_PED_ID();
    if(!player||!ENTITY::DOES_ENTITY_EXIST(player))return SellerEra::Unknown;
    switch(world::ProtagonistOf(ENTITY::GET_ENTITY_MODEL(player))){
    case world::Protagonist::Arthur:return SellerEra::Arthur;
    case world::Protagonist::John:return SellerEra::John;
    default:return SellerEra::Unknown;
    }
}
bool IsMember(SellerEra era){
    const auto& s=S();
    return s.enabled&&s.ledger.Active()&&era!=SellerEra::Unknown&&
        CurrentSeller()==era&&s.ledger.IsMember(era);
}
bool CanPurchaseMembership(std::string& reason,const HonoraryQuery& honorary_eligible){
    if(!MarketReady(reason))return false;
    const auto era=CurrentSeller();
    if(era==SellerEra::Unknown){reason="Membership requires Arthur or John.";return false;}
    if(S().ledger.IsMember(era)){reason="You are already a club member.";return false;}
    const bool honorary=honorary_eligible&&honorary_eligible();
    if(!S().ledger.HasQualifyingSale(era)){
        reason="Sell a winning racehorse as this character to qualify for membership.";return false;
    }
    if(!honorary&&MONEY::_MONEY_GET_CASH_BALANCE()<kMembershipFeeCents){
        reason="Club membership costs $1000.00.";return false;
    }
    reason.clear();return true;
}
bool ClearMembershipRecords(bool busy,std::string& reason){
    ReofferArchiveOnExit reoffer;
    CloseStartup("membership reset attempt");
    if(busy){reason="Finish the current event before clearing membership records.";return false;}
    if(!MarketReady(reason))return false;
    auto& s=S();const auto era=CurrentSeller();
    if(era==SellerEra::Unknown){reason="Membership records require Arthur or John.";return false;}
    const auto sales=s.ledger.Sales().size(),members=s.ledger.Memberships().size();
    const auto campaign=s.ledger.Progression()?progression::State::Decode(*s.ledger.Progression()):std::nullopt;
    if(!campaign){reason=s.archive_issue.empty()?"The current campaign archive is not ready.":s.archive_issue;return false;}
    auto changed=*campaign;
    changed.ClearHonoraryQualification(era==SellerEra::Arthur?progression::Era::Arthur:progression::Era::John);
    auto replacement=s.ledger;
    if(!replacement.ClearMarketHistory(era)||!replacement.SetProgression(changed.Encode())){
        reason="Could not clear the loaded membership records.";return false;
    }
    // One synchronous working-state replacement. Captures from an earlier
    // serializer still describe that earlier save, not the current live state.
    RevokeArchiveMirror();
    s.ledger=std::move(replacement);s.mutation_tick=GetTickCount64();s.last_horse.clear();
    Log("membership records cleared seller="+std::to_string(static_cast<int>(era))+
        " removed_sales="+std::to_string(sales-s.ledger.Sales().size())+
        " removed_memberships="+std::to_string(members-s.ledger.Memberships().size())+
        " preserved_wins="+std::to_string(s.ledger.Wins().size())+
        " epoch="+std::to_string(s.ledger.Epoch())+" save_required=1");
    reason.clear();return true;
}
MembershipCompletion PurchaseMembership(SellerEra expected,const std::function<bool()>& betting_unlocked,const HonoraryQuery& honorary_eligible,std::optional<Cents> expected_fee){
    ReofferArchiveOnExit reoffer;
    CloseStartup("membership registration attempt");
    std::string reason;
    if(!CanPurchaseMembership(reason,honorary_eligible))return {false,false,reason};
    if(expected==SellerEra::Unknown||CurrentSeller()!=expected||!betting_unlocked||betting_unlocked())
        return {false,false,"Membership is no longer available."};
    auto& s=S();
    const auto revision=*s.ledger.Active();
    const auto timeline=s.timeline,epoch=s.ledger.Epoch(),mutation=s.ledger.Mutation();
    const auto pack=save_observer::Latest().raw.pack_counter;
    const bool honorary=honorary_eligible&&honorary_eligible();
    const Cents fee=honorary?0:kMembershipFeeCents;
    if(expected_fee&&*expected_fee!=fee)return {false,false,"Membership price changed. Release the control and review the new amount."};
    const int cash=honorary?0:MONEY::_MONEY_GET_CASH_BALANCE();
    if(cash<fee)return {false,false,"Club membership costs $1000.00."};
    const auto sale=std::find_if(s.ledger.Sales().begin(),s.ledger.Sales().end(),
        [&](const Sale& value){return value.seller==expected;});
    if(sale==s.ledger.Sales().end())return {false,false,"Sell a winning racehorse first."};
    const Membership member{EventId(),expected,fee,sale->event_id,UnixMs()};
    if(!Validate(member))return {false,false,"Could not prepare the membership receipt."};
    struct ProgressGuard{State& s;~ProgressGuard(){s.sale_in_progress=false;}} guard{s};
    RevokeArchiveMirror();
    s.sale_in_progress=true;
    TransactionPins pins{s,revision,member.event_id};
    if(!RefreshArchivePins("membership transaction"))return {false,false,"Could not retain the membership archive. No fee was charged."};
    bool entered=false;
    const auto journal=[&](MembershipStage stage){
        const auto result=s.store->MembershipJournal(revision,member,stage,reason);
        if(result==WriteResult::Error||result==WriteResult::Conflict)WarnArchiveSave();
        return result==WriteResult::Written;
    };
    const auto unchanged=[&](bool entered_transaction=false){
        const bool unlocked=betting_unlocked();
        const bool still_honorary=honorary_eligible&&honorary_eligible();
        Pump(true);const auto observation=save_observer::Latest();
        return s.enabled&&s.timeline==timeline&&s.ledger.Active()&&s.ledger.Active()->profile==revision.profile&&
            s.ledger.Epoch()==epoch&&s.ledger.Mutation()==mutation&&WorldReady()&&CurrentSeller()==expected&&
            (entered_transaction?PreviewObservation(observation):
                (*s.ledger.Active()==revision&&!Busy(observation)&&observation.raw.pack_counter==pack))&&
            !unlocked&&!s.ledger.IsMember(expected)&&still_honorary==honorary&&
            std::any_of(s.ledger.Sales().begin(),s.ledger.Sales().end(),[&](const Sale& value){
                return value.event_id==member.qualifying_sale_id&&value.seller==expected;});
    };
    const auto uncertain=[&](const std::string& detail)->MembershipCompletion{
        Log("membership uncertain event="+member.event_id+" reason="+detail);
        if(s.timeline==timeline)Quarantine("membership payment needs reconciliation; no receipt replay");
        return {false,true,honorary?"Honorary membership could not be confirmed. Reload your pre-registration save.":
            "Membership payment could not be confirmed. Reload your pre-payment save."};
    };
    try{
        if(!journal(MembershipStage::Prepared))return {false,false,"Could not write the membership receipt. No fee was charged."};
        if(!unchanged())return {false,false,"The save or eligibility changed. No fee was charged."};
        if(!journal(MembershipStage::PaymentEntered))return {false,false,"Could not prepare the membership payment. No fee was charged."};
        if(!unchanged()||(!honorary&&MONEY::_MONEY_GET_CASH_BALANCE()!=cash))
            return {false,false,"The save, wallet or eligibility changed. No fee was charged."};
        entered=true;
        const bool paid=honorary||MONEY::_MONEY_DECREMENT_CASH_BALANCE(static_cast<int>(fee));
        const int after=honorary?cash:MONEY::_MONEY_GET_CASH_BALANCE();
        if(!paid||static_cast<Cents>(cash)-after!=fee)return uncertain("cash result not confirmed");
        if(!journal(MembershipStage::Paid))return uncertain("paid receipt: "+reason);
        if(!unchanged(true))return uncertain("world or eligibility changed during payment");
        if(s.ledger.AddMembership(member,true)!=AddResult::Added)return uncertain("membership record rejected");
        s.mutation_tick=GetTickCount64();
        Log("membership paid event="+member.event_id+" seller="+std::to_string(static_cast<int>(expected))+
            " qualifying_sale="+member.qualifying_sale_id+" fee_cents="+std::to_string(fee)+
            " cash_before="+std::to_string(cash)+" cash_after="+std::to_string(after));
        return {true,false,honorary?"Honorary membership registered.":"Club membership registered."};
    }catch(const std::exception& error){
        if(entered)return uncertain(error.what());
        Log(std::string("membership preparation failed: ")+error.what());
        return {false,false,"Could not prepare membership. No fee was charged."};
    }
}
