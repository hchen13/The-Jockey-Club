// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Player locks, catalog opening and wager input processing.
void LockPlayer(){
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(player && !spectator_lock){
        ENTITY::FREEZE_ENTITY_POSITION(player,true);spectator_lock=true;
    }
    if(player){
        for(const char* name:{"INPUT_MOVE_LR","INPUT_MOVE_UD","INPUT_SPRINT","INPUT_JUMP","INPUT_ENTER","INPUT_VEH_EXIT"})
            PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(name),true);
    }
}
void UnlockPlayer(){
    if(spectator_lock && Exists(PLAYER::PLAYER_PED_ID()))ENTITY::FREEZE_ENTITY_POSITION(PLAYER::PLAYER_PED_ID(),false);
    spectator_lock=false;
}
void LockMenu(){
    if(!menu_lock){PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(),false,0,false);menu_lock=true;}
}
void UnlockMenu(){
    if(menu_lock){PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(),true,0,false);menu_lock=false;}
}
bool OpenCatalog(){
    std::vector<bookmaker_shop::Row> rows;
    if(menu.Current()==bm::Menu::Page::Riders){
        rows.reserve(jockeys.size());
        for(const auto& j:jockeys){
            rows.push_back({tr::Text(j.profile->name,j.profile->chinese),HorseLabel(j),
                j.ranks.speed+1,j.ranks.acceleration+1,HandlingHash(j)});
        }
    }else if(menu.Current()==bm::Menu::Page::Market){
        const auto& j=jockeys[menu.RiderIndex()];
        rows={{std::string(tr::Text("Win · ","独赢 · "))+bm::odds::Label(j.win_odds),HorseLabel(j),
                j.ranks.speed+1,j.ranks.acceleration+1,HandlingHash(j),bm::MarketRuleDescription(bm::Market::Win,tr::Chinese())},
            {tr::Text("Place · 1-to-1","位置 · 1-to-1"),HorseLabel(j),
                j.ranks.speed+1,j.ranks.acceleration+1,HandlingHash(j),bm::MarketRuleDescription(bm::Market::TopThree,tr::Chinese())}};
    }else return false;
    const bool show_stats=menu.Current()==bm::Menu::Page::Riders;
    return catalog.Active()?catalog.ReplaceRows(std::move(rows),show_stats):
        catalog.Open(std::move(rows),show_stats,false,"Leave Race",NativeCatalogTitle(),NativeCatalogSubtitle());
}
void CloseMenu(){catalog.Close();menu.Close();amount_repeat.Reset();RemoveStakePrompts();StopSlowdown();UnlockMenu();
    if(phase==Phase::Menu){ReleaseObstacleProfiles("quotation cancelled");phase=Phase::Idle;jockeys.clear();}}
void MenuInputTick(){
    if(phase!=Phase::Menu)return;
    if(menu.Current()!=bm::Menu::Page::Stake){
        UnlockMenu();
        if(!Foreground() || HUD::IS_PAUSE_MENU_ACTIVE())return;
        if(!catalog.Active()){
            if(OpenCatalog())return;
            if(Now()<catalog_retry_until)return;
            Log("native shop did not reopen");CloseMenu();return;
        }
        const std::string before=catalog.Summary();
        const auto event=catalog.Tick();
        if(!catalog.Active()){
            // Tick may close/drop the bindings. Retain the pre-close state,
            // but read its failure afterwards so the actual cause is logged.
            Log("native catalog ended: reason="+std::string(catalog.Failure()[0]?catalog.Failure():"cancelled")+
                "; before "+before);CloseMenu();return;
        }
        if(catalog_diagnostics_left>0 && Now()>=catalog_diagnostic_at){
            Log("native catalog state: "+catalog.Summary());
            --catalog_diagnostics_left;catalog_diagnostic_at=Now()+1;
        }
        if(!event)return;
        const auto page=menu.Current();
        if(event->action==bookmaker_shop::Action::Back){
            menu.Press(bm::MenuInput::Back);
            if(menu.Current()==bm::Menu::Page::Closed){CloseMenu();return;}
            if(!OpenCatalog()){Log("native shop could not return to rider catalog");CloseMenu();}
            return;
        }
        if(page==bm::Menu::Page::Riders){
            if(event->index>=jockeys.size())return;
            while(menu.RiderIndex()!=event->index)menu.Press(bm::MenuInput::Down);
        }else if(page==bm::Menu::Page::Market){
            if(event->index>1)return;
            if((event->index==0)!=(menu.SelectedMarket()==bm::Market::Win))
                menu.Press(bm::MenuInput::Down);
        }
        menu.Press(bm::MenuInput::Accept);
        if(menu.Current()==bm::Menu::Page::Market && !OpenCatalog()){
            Log("native shop could not enter market catalog");CloseMenu();
        }else if(menu.Current()==bm::Menu::Page::Stake){catalog.Close();catalog_retry_until=0;}
        return;
    }
    LockMenu();DrawMenu();
    if(!Foreground() || HUD::IS_PAUSE_MENU_ACTIVE()){amount_repeat.Reset();return;}
    std::optional<bm::MenuInput> input;
    const bool stake=menu.Current()==bm::Menu::Page::Stake;
    const std::array<std::pair<Hash,bm::MenuInput>,6> pad{{
        {INPUT_FRONTEND_UP,bm::MenuInput::Up},{INPUT_FRONTEND_DOWN,bm::MenuInput::Down},
        {INPUT_FRONTEND_LEFT,bm::MenuInput::Left},{INPUT_FRONTEND_RIGHT,bm::MenuInput::Right},
        {INPUT_FRONTEND_ACCEPT,bm::MenuInput::Accept},{INPUT_FRONTEND_CANCEL,bm::MenuInput::Back}}};
    for(const auto& [control,action]:pad)if(PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,control) ||
        PAD::IS_CONTROL_JUST_PRESSED(0,control)){
        if(!stake || (action!=bm::MenuInput::Up && action!=bm::MenuInput::Down &&
            action!=bm::MenuInput::Left && action!=bm::MenuInput::Right))input=action;
        break;
    }
    if(stake){
        const auto pressed=[](Hash control){return PAD::IS_CONTROL_PRESSED(0,control) ||
            PAD::IS_DISABLED_CONTROL_PRESSED(0,control);};
        const bool up=pressed(INPUT_MINIGAME_INCREASE_BET)||pressed(INPUT_FRONTEND_UP);
        const bool down=pressed(INPUT_MINIGAME_DECREASE_BET)||pressed(INPUT_FRONTEND_DOWN);
        menu.AdjustAmount(amount_repeat.Update(up==down?0:(up?1:-1),GetTickCount64()));
        if(!input && (PAD::IS_CONTROL_JUST_PRESSED(0,INPUT_MINIGAME_PLACE_BET) ||
            PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,INPUT_MINIGAME_PLACE_BET)))input=bm::MenuInput::Accept;
        if(!input && (PAD::IS_CONTROL_JUST_PRESSED(0,INPUT_MINIGAME_QUIT) ||
            PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,INPUT_MINIGAME_QUIT)))input=bm::MenuInput::Back;
    }else amount_repeat.Reset();
    if(input){
        const auto choice=menu.Press(*input);
        if(menu.Current()!=bm::Menu::Page::Stake){
            amount_repeat.Reset();RemoveStakePrompts();UnlockMenu();
            if(menu.Current()==bm::Menu::Page::Market)catalog_retry_until=Now()+3;
        }
        if(menu.Current()==bm::Menu::Page::Closed){CloseMenu();return;}
        if(!choice)return;
        if(MONEY::_MONEY_GET_CASH_BALANCE()<choice->cents){Notice(tr::Text("Not enough cash","现金不足"));return;}
        if(ticket.Phase()==bm::Ticket::State::Closed)ticket={};
        if(!ticket.BeginQuoted(choice->rider,choice->market,choice->cents,
            MONEY::_MONEY_GET_CASH_BALANCE(),jockeys[menu.RiderIndex()].win_odds)){
            Notice(tr::Text("Previous wager still settling","上一笔赌注尚未结算"));return;
        }
        // Do not commit a cash change until the route, cast, and menu proposal
        // are all validated. This is still before the race starts, as requested.
        if(!MONEY::_MONEY_DECREMENT_CASH_BALANCE(ticket.PendingDebit())){
            ticket={};Notice(tr::Text("Wager could not be placed","下注失败"));return;
        }
        ticket.DebitSucceeded();transaction_debited=true;
        transaction_feed::Cash(-choice->cents);
        Log("stake debited cents="+std::to_string(choice->cents)+" locked_odds="+
            bm::odds::Label(ticket.LockedNetOdds()));
        catalog.Close();menu.Close();amount_repeat.Reset();RemoveStakePrompts();StopSlowdown();UnlockMenu();
        phase=Phase::Loading;entered_at=Now();candidate_bet=true;return;
    }
}
