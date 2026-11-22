// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Wager settlement, odds persistence and pending credit.
void Settle(bm::Finish finish,int place){
    if(settle_requested)return;
    settle_requested=true;pending_finish=finish;pending_result_place=place;
    if(ticket.Phase()==bm::Ticket::State::Active)ticket.Settle(finish,place);
    pending_result_credit=ticket.PendingCredit();
    Log("settlement requested finish="+std::to_string(static_cast<int>(finish))+" place="+std::to_string(place)+
        " credit="+std::to_string(ticket.PendingCredit()));
}
void LoadOdds(){
    odds_calibration={};odds_store_writable=true;
    const auto path=paths.Learning("TheJockeyClub-odds.txt");
    std::error_code error;
    const bool exists=std::filesystem::exists(path,error);
    if(!exists && !error)return;
    const auto size=std::filesystem::file_size(path,error);
    if(error || size>bm::odds::kMaxFileBytes){
        odds_store_writable=false;Log("odds history inaccessible/oversized; preserving file and using seed");return;
    }
    std::ifstream input(path,std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>{input},{});
    if(!input || !odds_calibration.Parse(bytes)){
        odds_store_writable=false;Log("odds history invalid; preserving file and using seed");return;
    }
    Log("local WFA calibration loaded records="+std::to_string(odds_calibration.Records().size()));
}
void SaveOdds(){
    if(!odds_store_writable)return;
    const auto target=paths.Learning("TheJockeyClub-odds.txt");
    const auto bytes=odds_calibration.Serialize();
    if(!runtime_paths::EnsureDirectory(target.parent_path()) || !files::ReplaceAtomically(target,bytes)){
        Log("local WFA calibration save failed; existing history preserved");return;
    }
    Log("local WFA calibration saved records="+std::to_string(odds_calibration.Records().size()));
}
void LearnCompletedRace(){
    if(learning_done || Venue().shared_attributes)return;
    learning_done=true;
    if(learning_excluded || player_sabotaged || field.Running() ||
        field.Riders().size()!=jockeys.size()){
        Log("WFA learning skipped: technical failure or invalid race");return;
    }
    bool any_finish=false;
    for(const auto& r:field.Riders()){
        if(r.survival_win){Log("WFA learning skipped: survival winner has no full-course time");return;}
        any_finish|=r.status==bm::Field::Status::Finished;
    }
    if(!any_finish){Log("WFA learning skipped: no valid finish");return;}
    auto candidate=odds_calibration;
    for(std::size_t i=0;i<jockeys.size();++i){
        const auto& r=field.Riders()[i];
        if(!candidate.Observe(jockeys[i].prediction,r.status==bm::Field::Status::Finished,r.finish_at-go_at)){
            Log("WFA learning skipped: elapsed sample invalid or history full");return;
        }
    }
    odds_calibration=std::move(candidate);SaveOdds();
}
void FlushCredit(){
    if(ticket.Phase()!=bm::Ticket::State::CreditPending ||
        (phase!=Phase::Idle && phase!=Phase::Results) || Clock::now()<next_cash)return;
    next_cash=Clock::now()+std::chrono::seconds(1);
    const int cents=ticket.PendingCredit();
    const int before=MONEY::_MONEY_GET_CASH_BALANCE();
    if(MONEY::_MONEY_INCREMENT_CASH_BALANCE(cents,MISC::GET_HASH_KEY("ADD_REASON_DEFAULT"))){
        ticket.CreditSucceeded();cash_receipts.Queue(cents);
        Log("ticket credit committed cents="+std::to_string(cents)+" balance_before="+
            std::to_string(before)+" balance_after="+std::to_string(MONEY::_MONEY_GET_CASH_BALANCE())+
            " native_receipt=queued");
    }else Log("ticket credit pending retry cents="+std::to_string(cents));
}
