// Private corridor api implementation; included by race_system.cpp.
AutoSurveyState GetAutoSurveyState() {
    AutoSurveyState s;
    s.running=g_trial.active;s.armed=g_trial.armed;s.riders=g_trial.riders;s.incidents=g_trial.incidents;
    return s;
}
std::optional<Ped> GetTrialObservedHorse() {
    if(!g_trial.active)return {};
    return g_trial.spectator.ObservedHorse();
}
void ToggleAutoSurvey() {
    if(g_trial.active){AbortRace("trial stopped from the menu",true,Ender::Game);return;}
    g_trial.armed=!g_trial.armed;
    Log(std::string("trial: ")+(g_trial.armed?"armed - the next race is ridden by the host alone":"disarmed"));
    ShowObjective(g_trial.armed?i18n::Text("Trial armed: start a race, the host rides it alone","试跑已武装：发起一场比赛，host 将独自跑线"):
        i18n::Text("Trial disarmed","试跑已取消"));
}
CorridorView GetCorridorView() {
    CorridorView view;
    view.applicable=CurrentRules().corridor_dq;
    view.station_key=CorridorStationKey();
    view.override_width=CorridorOverride();
    view.highlight=g_corridor_highlight;
    view.racing=g_state==State::Racing;
    view.deferred=g_corridor_pending.has_value();
    if(!view.applicable)return view;
    const auto& definition=SelectedFixedRoute().circuit;
    view.route_default=definition?definition->strict_half_width:0;
    view.judged=g_laps.Frozen()?g_laps.plan.course.strict_half_width:corridor_override::Effective(view.override_width,view.route_default);
    view.effective=g_corridor_pending.value_or(view.judged);
    return view;
}
void ApplyCorridorRequest(const CorridorRequest& request) {
    if(request.racing_line_percent) {
        Log("F11 racing line percent="+std::to_string(*request.racing_line_percent));
        SetRacingLinePercent(*request.racing_line_percent);
    }
    if(request.racing_line_highlight) {
        g_racing_line_highlight=std::clamp(*request.racing_line_highlight,0,1);
        Log("F11 racing line highlight="+std::to_string(g_racing_line_highlight));
    }
    if(request.highlight) {
        g_corridor_highlight=std::clamp(*request.highlight,0,1);
        Log("F11 corridor highlight="+std::to_string(g_corridor_highlight));
    }
    if(!request.width_changed)return;
    // The page already persisted the key; the cache follows it so the next
    // freeze and the view agree without another read of the file.
    g_corridor_key=CorridorStationKey();g_corridor_override=request.width;
    const std::string wanted=request.width?std::to_string(*request.width)+" m":"route default";
    if(!CurrentRules().corridor_dq || !g_laps.Frozen()) {
        Log("F11 corridor half-width saved for '"+g_corridor_key+"': "+wanted+"; no judged plan is frozen, applies at the next freeze");
        return;
    }
    const auto& definition=SelectedFixedRoute().circuit;
    const float route_default=definition?definition->strict_half_width:g_laps.plan.course.strict_half_width;
    const float width=corridor_override::Effective(request.width,route_default);
    const float judged=g_laps.plan.course.strict_half_width;
    if(g_state==State::Racing && width<judged) {
        g_corridor_pending=width;
        Log("F11 corridor narrowed to "+std::to_string(width)+" m during a race: highlight now, legality from the next race (judged "+
            std::to_string(judged)+" m)");
        return;
    }
    g_corridor_pending.reset();
    if(!g_laps.plan.SetHalfWidth(width)) {
        Log("F11 corridor half-width "+std::to_string(width)+" m refused by the plan; judged width stays "+std::to_string(judged)+" m");
        return;
    }
    Log("F11 corridor half-width applied live: "+std::to_string(width)+" m (was "+std::to_string(judged)+", route "+
        std::to_string(route_default)+") state="+StateName(g_state)+"; every entrant is judged by the same plan");
    // The same build path invalidates prepared lines before GO or queues the
    // live drivers, so judging and planning consume the same corridor width.
    RebuildRacingLines("corridor width changed");
}
