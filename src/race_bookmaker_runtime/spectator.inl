// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Live standings, followed horse information and spectator camera.
void DrawStandings(){
    const char* gate=phase!=Phase::Racing?"not racing":
        HUD::IS_PAUSE_MENU_ACTIVE()?"pause menu":!Foreground()?"game unfocused":
        !PlayerOkay()?"player unavailable":!camera_blackout.FullyVisible()?"blackout":
        field.Riders().size()!=jockeys.size()?"roster mismatch":"ready";
    if(native_standings_gate!=gate){
        native_standings_gate=gate;
        Log("native standings gate: "+native_standings_gate+
            " uiapp_active="+std::to_string(UIAPPS::IS_ANY_UIAPP_ACTIVE()));
    }
    // IS_ANY_UIAPP_ACTIVE remains true for some background Story Mode apps.
    // The shop catalog is closed before Racing and cannot justify hiding the
    // non-interactive standings HUD for the entire race.
    if(native_standings_gate!="ready"){
        native_standings.Hide(Now());return;
    }
    const auto order=field.StandingsOrder();
    std::vector<bm::NativeStandingRow> rows;
    rows.reserve(order.size());
    for(const auto i:order){
        const auto& rider=field.Riders()[i];
        const bool out=rider.status==bm::Field::Status::Out;
        const float fraction=rider.status==bm::Field::Status::Finished?1.0F:
            std::clamp(rider.progress.Metres(plan.course)/
                std::max(1.0F,plan.course.path.Length()),0.0F,1.0F);
        rows.push_back({out,fraction,bm::StandingPortrait(jockeys[i].profile->id),
            bm::StandingPortraitDict(jockeys[i].profile->id)});
    }
    // Native composition: one 64-unit box owns background, clipped portrait
    // and foreground progress ring. No title/font/screen coordinate estimates.
    // Keep a hot-readable diagnostic switch until real-game acceptance, so an
    // LML resource loading failure does not require another ASI/restart merely
    // to restore the previously visible (but approximate) sprites.
    static bool native_portraits=false;
    static bool portrait_setting_logged=false;
    static unsigned portrait_resource_mask=0;
    static double portrait_setting_after=-1;
    if(Now()>=portrait_setting_after){
        portrait_setting_after=Now()+1;
        const bool requested=horse_races::ReadNativeStandingsPortraitsSetting();
        const unsigned loaded=horse_races::NativeStandingsResourceLoadMask();
        const bool selected=requested && loaded==3;
        if(selected!=native_portraits || loaded!=portrait_resource_mask || !portrait_setting_logged)
            Log(std::string("native standings renderer: requested=")+(requested?"native":"legacy")+
                " lml_resource_mask="+std::to_string(loaded)+" selected="+
                (selected?"native_inline_masked_ring_foreground":"legacy_approximate_sprites")+
                " (resource selection is not pixel verification)");
        portrait_setting_logged=true;
        portrait_resource_mask=loaded;
        native_portraits=selected;
    }
    native_standings.NativePortraits(native_portraits);
    // The stock MP competition flow owns layout, fonts and the meter rings.
    native_standings.Update(StandingsName(),rows,Now());
    if(native_portraits)return;
    // Diagnostic rollback only: original textures submitted over the old
    // native rings. This path is approximate and cannot prove exact alignment.
    if(!native_standings.Active() || HUD::IS_HUD_HIDDEN())return;
    // The MP flow ignores its portrait and overlay image bindings in-game.
    // Always draw the original Story Mode headshots as script sprites; the
    // native flow still owns the title, progress rings and relative layout.
    struct Screen {int width=0,height=0;};
    const auto find_window=[](HWND window,LPARAM data)->BOOL{
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid!=GetCurrentProcessId() || !IsWindowVisible(window))return TRUE;
        RECT rect{};if(!GetClientRect(window,&rect))return TRUE;
        auto& best=*reinterpret_cast<Screen*>(data);
        const int width=rect.right-rect.left,height=rect.bottom-rect.top;
        if(width>0 && height>0 && width*height>best.width*best.height)
            best={width,height};
        return TRUE;
    };
    static Screen screen{};
    static double measure_after=0;
    if(Now()>=measure_after || screen.width<=0 || screen.height<=0){
        measure_after=Now()+1.0;Screen measured{};
        EnumWindows(find_window,reinterpret_cast<LPARAM>(&measured));
        if(measured.width<=0 || measured.height<=0)
            measured={GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
        if(measured.width>0 && measured.height>0)screen=measured;
    }
    if(screen.width<=0 || screen.height<=0)return;
    static std::string measured_title;
    static float title_advance_1440=0.0F;
    const char* native_title=StandingsName();
    if(measured_title!=native_title){
        measured_title=native_title;
        title_advance_1440=bm::StandingTitleAdvance1440(measured_title);
        Log("native standings title geometry: title="+measured_title+
            " advance_1440="+std::to_string(title_advance_1440));
    }
    const auto slots=bm::PortraitSlots(rows.size(),screen.width,screen.height,
        title_advance_1440);
    bm::NativePortraitSpriteApi sprite_api;
    const auto report=bm::DrawStandingPortraitSprites(sprite_api,
        native_standings.Active() && !HUD::IS_HUD_HIDDEN(),rows,slots,screen.width,screen.height);
    const auto drawn=report.count;
    const auto& rendered=report.drawn;
    static std::uint64_t logged_token=std::numeric_limits<std::uint64_t>::max();
    static std::size_t logged_drawn=0;
    static bool logged_once=false;
    if(logged_token!=token){
        logged_token=token;
        logged_drawn=0;
        logged_once=false;
    }
    if(!logged_once || drawn!=logged_drawn){
        logged_once=true;
        logged_drawn=drawn;
        std::string geometry="native portrait overlay: client="+
            std::to_string(screen.width)+"x"+std::to_string(screen.height)+
            " rows="+std::to_string(rows.size())+
            " draw_submissions="+std::to_string(drawn)+
            " renderer=restored_sprite draw_order=4 layout=legacy_approximate sprite_centers_px=";
        for(std::size_t i=0;i<rows.size() && i<slots.size();++i){
            if(i>0)geometry+=";";
            geometry+=std::to_string(i+1)+"@("+std::to_string(slots[i].x)+","+
                std::to_string(slots[i].y)+",d"+std::to_string(slots[i].diameter)+","+
                (rendered[i]?"drawn":"pending")+")";
        }
        Log(geometry);
    }
}
std::optional<std::size_t> VisibleFollowRider(){
    if(phase!=Phase::Racing || camera_director.automatic || !camera_revealed ||
        !camera_blackout.FullyVisible() || HUD::IS_PAUSE_MENU_ACTIVE() || !Foreground() ||
        !PlayerOkay() || camera_focus!=camera_director.target || !camera_focus)return {};
    const auto index=*camera_focus;
    if(index>=jockeys.size() || index>=field.Riders().size() ||
        field.Riders()[index].status!=bm::Field::Status::Racing ||
        !Same(jockeys[index].horse_id) || !Same(jockeys[index].rider_id))return {};
    const bool aim=PAD::IS_CONTROL_PRESSED(0,INPUT_AIM)||PAD::IS_DISABLED_CONTROL_PRESSED(0,INPUT_AIM);
    return aim?camera_focus:std::optional<std::size_t>{};
}
void TickFollowHorseInfo(bool toggle=false){
    const auto visible=VisibleFollowRider();
    rider_horse_info.Tick(visible,Identify);
    if(!visible)return;
    if(toggle){
        const auto& j=jockeys[*visible];
        const bool opened=rider_horse_info.Toggle(*visible,j.horse_id,horse_catalog::current.Labels(j.horse_model));
        Log(std::string("follow rider horse info ")+(opened?(rider_horse_info.Active()?"opened":"closed"):"unavailable")+
            " rider="+std::to_string(*visible)+" profile="+j.profile->key+" model="+j.horse_model+
            (opened?"":" reason=")+rider_horse_info.LastOpenReason());
        if(!opened)Notice(tr::Text("Horse info unavailable; see log for detail.","马匹属性暂不可用，详情见日志。"));
    }
}
void UpdateCameraPrompts(bool aim_pressed,bool input_ready,bm::SpectatorPromptMode q_mode){
    const bool following=aim_pressed && !camera_director.automatic;
    const auto target=camera_director.target;
    const char* name=target && *target<jockeys.size()?
        tr::Text(jockeys[*target].profile->name,jockeys[*target].profile->chinese):tr::Text("SPECTATOR","观赛");
    NativeSpectatorPromptApi api;
    bm::UpdateSpectatorCameraPrompts(api,camera_prompts,MISC::GET_HASH_KEY("TJC_SPECTATOR_CAMERA"),
        CameraPromptLabels(),name,input_ready,camera_director.automatic,following,q_mode,
        spectator_hud.RankingsVisible() && native_standings.Active());
}
void StopLeaderCamera(bool ease=true){
    (void)ease;
    rider_horse_info.Close();
    if(leader_focus_owned){STREAMING::CLEAR_FOCUS();leader_focus_owned=false;}
    if(native_cinema_owned){CAM::SET_CINEMATIC_MODE_ACTIVE(false);native_cinema_owned=false;}
    camera_focus.reset();camera_focus_at=camera_motion_since=0;
    camera_revealed=false;
}
void TickNativeSpectatorCamera(){
    if(phase!=Phase::Racing || HUD::IS_PAUSE_MENU_ACTIVE()){
        StopLeaderCamera(false);return;
    }
    // We own the V-key hold and the mode transition during the exhibition;
    // the game's own cinematic renderer, not a scripted camera, draws shots.
    CAM::SET_CINEMATIC_BUTTON_ACTIVE(false);
    EnsureCameraPrompts();
    const bool aim_pressed=PAD::IS_CONTROL_PRESSED(0,INPUT_AIM)||
        PAD::IS_DISABLED_CONTROL_PRESSED(0,INPUT_AIM);
    const auto down=[](Hash control){return PAD::IS_CONTROL_PRESSED(0,control)||
        PAD::IS_DISABLED_CONTROL_PRESSED(0,control);};
    const auto pressed=[](Hash control){return PAD::IS_CONTROL_JUST_PRESSED(0,control)||
        PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,control);};
    const bool aim=aim_pressed;
    const bool cinematic=down(INPUT_CINEMATIC_CAM)||down(INPUT_CINEMATIC_CAM_HOLD);
    // The two modes are exclusive. V never arms while Aim is held, and Aim
    // cannot take over until the player explicitly leaves cinematic mode.
    const bool follow=aim && !camera_director.automatic;
    aim_hold.Tick(follow,Now());
    if(follow)cinematic_hold.Reset();
    const bool toggle=follow?false:camera_director.automatic?
        (pressed(INPUT_CINEMATIC_CAM)||pressed(INPUT_CINEMATIC_CAM_HOLD)):
        cinematic_hold.Tick(cinematic,Now());
    if(camera_director.automatic)cinematic_hold.Reset();
    std::vector<std::size_t> eligible;
    for(const auto index:field.VisibleOrder())if(field.Riders()[index].status==bm::Field::Status::Racing &&
        Same(jockeys[index].horse_id)&&Same(jockeys[index].rider_id)&&
        (!camera_failed || *camera_failed!=index || Now()-camera_failed_at>8))eligible.push_back(index);
    std::optional<std::size_t> wager_rider;
    if(candidate_bet)for(std::size_t i=0;i<jockeys.size();++i)
        if(jockeys[i].profile->id==ticket.Selection()){wager_rider=i;break;}
    camera_director.Tick(follow,toggle,pressed(INPUT_CINEMATIC_CAM_CHANGE_SHOT),Now(),eligible,
        [](int count){return MISC::GET_RANDOM_INT_IN_RANGE(0,count);},wager_rider);
    const bool input_ready=Foreground() && PlayerOkay() && camera_blackout.FullyVisible();
    const bool follow_requested=aim_pressed && !camera_director.automatic;
    const bool follow_rider_visible=VisibleFollowRider().has_value();
    auto q_mode=spectator_hud.PromptMode(input_ready,camera_director.automatic,
        follow_requested,follow_rider_visible,rider_horse_info.Active(),rider_horse_info.Busy());
    if(q_mode==bm::SpectatorPromptMode::Rankings && spectator_hud.RankingsVisible() &&
        !native_standings.Active())q_mode=bm::SpectatorPromptMode::None;
    // A disabled control hides its native prompt row. During a follow fade,
    // no Q prompt is shown, so suppress target-info until the rider is visible.
    if(input_ready && follow_requested && q_mode==bm::SpectatorPromptMode::None)
        PAD::DISABLE_CONTROL_ACTION(0,INPUT_INTERACT_LOCKON_TARGET_INFO,true);
    const bool q_pressed=q_mode!=bm::SpectatorPromptMode::None &&
        pressed(INPUT_INTERACT_LOCKON_TARGET_INFO);
    TickFollowHorseInfo((q_mode==bm::SpectatorPromptMode::HorseInfo ||
        q_mode==bm::SpectatorPromptMode::FollowRankings) && q_pressed);
    if(q_mode==bm::SpectatorPromptMode::Rankings && q_pressed)spectator_hud.ToggleRankings();
    auto display_q_mode=spectator_hud.PromptMode(input_ready,camera_director.automatic,
        follow_requested,VisibleFollowRider().has_value(),rider_horse_info.Active(),rider_horse_info.Busy());
    if(display_q_mode==bm::SpectatorPromptMode::Rankings && spectator_hud.RankingsVisible() &&
        !native_standings.Active())display_q_mode=bm::SpectatorPromptMode::None;
    UpdateCameraPrompts(aim_pressed,input_ready,display_q_mode);
    if(camera_director.last_cut!=bm::CameraDirector::Cut::None && camera_director.target){
        const auto cut=camera_director.last_cut;
        const char* cause=cut==bm::CameraDirector::Cut::Manual?"manual":
            cut==bm::CameraDirector::Cut::Fallback?"unavailable":
            cut==bm::CameraDirector::Cut::Follow?"follow":"cinematic entry";
        Log("spectator camera choice mode="+std::string(camera_director.automatic?"cinematic":"follow")+
            " rider="+std::to_string(*camera_director.target)+" cause="+cause+
            " viewed="+std::to_string((camera_director.automatic?
                camera_director.cinematic_pool:camera_director.follow_pool).viewed.size())+
            " eligible="+std::to_string(eligible.size()));
    }
    if(camera_failed && Now()-camera_failed_at>8)camera_failed.reset();
    if(!camera_director.target){
        if(camera_focus && !camera_blackout.Black()){
            if(!camera_blackout.FadingOut())camera_blackout.StartOut(280);
            if(Same(jockeys[*camera_focus].rider_id))
                CAM::SET_GAMEPLAY_CAM_FOLLOW_PED_THIS_UPDATE(jockeys[*camera_focus].rider);
            return;
        }
        StopLeaderCamera(false);
        camera_blackout.StartIn(300);
        return;
    }
    const auto index=*camera_director.target;
    auto& jockey=jockeys[index];
    if(camera_focus && *camera_focus!=index && !camera_blackout.Black()){
        // Keep rendering the old rider until the screen is fully black.
        // Switching the gameplay follow target before the fade completed
        // exposed John for several frames between two remote riders.
        if(!camera_blackout.FadingOut())camera_blackout.StartOut(300);
        if(Same(jockeys[*camera_focus].rider_id))
            CAM::SET_GAMEPLAY_CAM_FOLLOW_PED_THIS_UPDATE(jockeys[*camera_focus].rider);
        if(native_cinema_owned && camera_director.automatic)
            CAM::FORCE_CINEMATIC_RENDERING_THIS_UPDATE(true);
        return;
    }
    if(!camera_focus || *camera_focus!=index){
        camera_focus=index;camera_focus_at=Now();camera_motion_since=0;camera_revealed=false;
        STREAMING::SET_FOCUS_ENTITY(jockey.horse);leader_focus_owned=true;
        STREAMING::REQUEST_COLLISION_AT_COORD(ENTITY::GET_ENTITY_COORDS(jockey.horse,true,false));
        Log("native spectator target rider="+std::to_string(index)+
            (camera_director.automatic?" cinematic":" follow"));
    }
    // The hold period streams the target but leaves the ordinary balcony view
    // intact. After it fills, use the game's own ped-follow camera and its
    // own mouse/controller look, rather than a hand-integrated script orbit.
    if(follow && !aim_hold.ready)return;
    if(!camera_revealed && !camera_blackout.Black()){
        if(!camera_blackout.FadingOut())camera_blackout.StartOut(300);
        return;
    }
    CAM::SET_GAMEPLAY_CAM_FOLLOW_PED_THIS_UPDATE(jockey.rider);
    const bool automatic=camera_director.automatic;
    if(automatic){
        if(!native_cinema_owned){CAM::SET_CINEMATIC_MODE_ACTIVE(true);native_cinema_owned=true;}
        CAM::FORCE_CINEMATIC_RENDERING_THIS_UPDATE(true);
    }else if(native_cinema_owned){CAM::SET_CINEMATIC_MODE_ACTIVE(false);native_cinema_owned=false;}
    const bool awake=!ENTITY::GET_ENTITY_COLLISION_DISABLED(jockey.horse) &&
        ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(jockey.horse) &&
        !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(jockey.horse) &&
        ENTITY::GET_ENTITY_SPEED(jockey.horse)>=1.5F;
    if(awake){if(camera_motion_since==0)camera_motion_since=Now();}
    else camera_motion_since=0;
    if(!camera_revealed && camera_motion_since>0 && Now()-camera_motion_since>=.15){
        camera_blackout.StartIn(350);camera_revealed=true;
    }else if(!camera_revealed && Now()-camera_focus_at>5){
        Log("native spectator target unavailable rider="+std::to_string(index));
        camera_failed=index;camera_failed_at=Now();camera_director.target.reset();
        camera_focus.reset();
    }
}
