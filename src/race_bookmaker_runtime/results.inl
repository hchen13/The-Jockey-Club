// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Final result rows, notes and result-phase presentation.
std::vector<bookmaker_shop::Row> FinalRows(){
    std::vector<bookmaker_shop::Row> rows;
    const auto order=field.StandingsOrder();
    for(std::size_t rank=0;rank<order.size();++rank){
        const auto i=order[rank];const bool out=field.Riders()[i].status==bm::Field::Status::Out;
        rows.push_back({(out?"—  ":std::to_string(rank+1)+"  ")+
                tr::Text(jockeys[i].profile->name,jockeys[i].profile->chinese),
            HorseLabel(jockeys[i])+"  "+(out?"OUT":"FINISHED"),0,0,0});
    }
    return rows;
}
bm::ResultNotes FinalNotes(){
    std::string chosen;
    for(const auto& j:jockeys)if(j.profile->id==ticket.Selection())
        chosen=tr::Text(j.profile->name,j.profile->chinese);
    return bm::ResultsNotes(candidate_bet,tr::Chinese(),chosen,ticket.BetMarket()==bm::Market::Win,
        Dollars(ticket.Stake()),bm::odds::Label(ticket.LockedNetOdds()),Dollars(pending_result_credit));
}
void TickResults(){
    if(!race_speed_policy::Enabled()){
        // An already-departing finisher may still own an earlier explicit pace.
        for(std::size_t i=0;i<jockeys.size();++i)
            if(jockeys[i].finish_departing && jockeys[i].finish_speed_overridden)TickFinishedJockey(i,Now());
    }
    LockPlayer();
    scene.TickActive(Now(),false,[](Ped){});
    if(!result_catalog_opened && Now()>=result_catalog_retry_at){
        const auto notes=FinalNotes();
        const bookmaker_shop::ResultPresentation result{notes.odds,notes.stake,notes.payout,
            notes.profit_label,pending_result_credit-ticket.Stake()};
        if(catalog.Open(FinalRows(),false,true,tr::Text("Leave Race","离开赛场"),NativeCatalogTitle(true),
            NativeCatalogSubtitle(true),"",notes.description.c_str(),notes.wagered?&result:nullptr)){
            result_catalog_opened=true;Log("native final standings opened");}
        else result_catalog_retry_at=Now()+1;
    }
    if(result_catalog_opened){
        const bool presented=catalog.Presented();
        const auto event=catalog.Tick();
        if(event && event->action==bookmaker_shop::Action::Back){
            if(presented){
                ReturnFromRace(pending_finish,pending_result_place,"spectator left final results");return;
            }
            result_catalog_opened=false;result_catalog_retry_at=Now()+1;
        }
    }
    if(PAD::IS_CONTROL_JUST_PRESSED(0,INPUT_CONTEXT_B) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,INPUT_CONTEXT_B) ||
        PAD::IS_CONTROL_JUST_PRESSED(0,INPUT_FRONTEND_CANCEL) ||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,INPUT_FRONTEND_CANCEL))
        ReturnFromRace(pending_finish,pending_result_place,"spectator left final results");
}
