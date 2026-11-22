// Settings keyboard dispatch and page navigation.
// Included at the original definition site; declaration and initialization order are preserved.
std::optional<bool> HandleInput(bool speed_override, const std::array<bool, kKeys.size()>& pressed, bool foreground)
{
    PollFontSetting();
    if (!foreground || !WorldVisible()) {
        music_page=false;music_selected=0;music_saved=false;music_save_failed=false;
        encounter_page=false;difficulty_page=false;corridor_page=false;race_page=false;camp_page=false;
        menu_open = false;progress_page=false;membership_reset_confirm_until={};membership_reset_status.clear();
        control_request={};
        if(bookmaker_lap_mask)lap_board.Mask();else lap_board.Close();
        return {};
    }
    if (pressed[0]) {
        menu_open = !menu_open;
        membership_reset_confirm_until={};
        membership_reset_status.clear();
        AUDIO::PLAY_SOUND_FRONTEND(menu_open ? "MENU_ENTER" : "MENU_CLOSE", "HUD_PLAYER_MENU", true, 0);
    }
    const bool returning=menu_open&&pressed[4]&&(encounter_page || progress_page || difficulty_page || corridor_page || race_page || camp_page || music_page);
    if(returning){encounter_page=false;progress_page=false;difficulty_page=false;corridor_page=false;race_page=false;camp_page=false;music_page=false;music_selected=0;music_saved=false;music_save_failed=false;}
    else if (menu_open && pressed[4]) menu_open = false;
    if(!menu_open){encounter_page=false;progress_page=false;difficulty_page=false;corridor_page=false;race_page=false;camp_page=false;music_page=false;music_selected=0;music_saved=false;music_save_failed=false;membership_reset_confirm_until={};membership_reset_status.clear();}
    std::optional<bool> change;
    if (menu_open) {
        for (const char* action : {"INPUT_FRONTEND_UP", "INPUT_FRONTEND_DOWN", "INPUT_FRONTEND_ACCEPT",
                "INPUT_FRONTEND_CANCEL", "INPUT_FRONTEND_LEFT", "INPUT_FRONTEND_RIGHT", "INPUT_INTERACT_LOCKON_POS", "INPUT_INTERACT_LOCKON_NEG"})
            PAD::DISABLE_CONTROL_ACTION(0, MISC::GET_HASH_KEY(action), true);
        if(encounter_page && !returning) {
            // One selectable control. Opening, closing and returning never
            // pass their accept/arrow input through to the marker preference.
            if(!pressed[0] && (pressed[3] || pressed[5] || pressed[6]))ChangeEncounterMap();
        } else if(music_page && !returning) {
            if(!pressed[0]) {
                if(pressed[1])music_selected=(music_selected+static_cast<int>(music::kCount)-1)%static_cast<int>(music::kCount);
                if(pressed[2])music_selected=(music_selected+1)%static_cast<int>(music::kCount);
                if(pressed[5] || pressed[6])ChangeMusic(false,pressed[5]?-5:5);
                else if(pressed[3])ChangeMusic(true);
            }
        } else if(progress_page && !returning) {
            if(pressed[1])progress_selected=(progress_selected+kProgressRows-1)%kProgressRows;
            if(pressed[2])progress_selected=(progress_selected+1)%kProgressRows;
            if(pressed[3]&&!pressed[0]){
                using A=horse_races::progression::Action;
                if(progress_selected==ProgressReset)progress_action=A::Reset;
                else if(progress_selected==ProgressAll)progress_action=A::UnlockAll;
                else if(progress_selected==ProgressNext)progress_action=A::UnlockNext;
                else if(progress_selected==ProgressPrevious)progress_action=A::LockPrevious;
            }
        } else if(difficulty_page && !returning) {
            if(pressed[1])difficulty_selected=(difficulty_selected+kDifficultyRows-1)%kDifficultyRows;
            if(pressed[2])difficulty_selected=(difficulty_selected+1)%kDifficultyRows;
            if(pressed[5] || pressed[6])ChangeDifficulty(pressed[5]?-1:1);
            else if(pressed[3] && !pressed[0])ChangeDifficulty(1);
        } else if(corridor_page && !returning) {
            if(pressed[1])corridor_selected=(corridor_selected+kCorridorRows-1)%kCorridorRows;
            if(pressed[2])corridor_selected=(corridor_selected+1)%kCorridorRows;
            if(pressed[5] || pressed[6])ChangeCorridor(pressed[5]?-1:1);
            else if(pressed[3] && !pressed[0])ChangeCorridor(1);
        } else if(race_page && !returning) {
            if(pressed[1])race_selected=(race_selected+kRaceRows-1)%kRaceRows;
            if(pressed[2])race_selected=(race_selected+1)%kRaceRows;
            if(pressed[3] && !pressed[0]) {
                if(race_selected==RaceRouteRecord)route_recording=!route_recording;
                else if(race_selected==RaceAutoSurvey)auto_survey_request=true;
                else if(race_selected==RaceTrialForce) {
                    auto& probe=horse_motion::force_trial::probe;
                    probe.SetRequested(!probe.Requested());
                }
                else if(race_selected==RaceStartRear) {
                    auto& cue=horse_races::cues::Settings();cue.rear_enabled=!cue.rear_enabled;
                    ::horse_races::config::SetRaw(ini.c_str(),"Cues","StartRear",cue.rear_enabled?"1":"0");
                } else if(race_selected==RaceStarterShot) {
                    auto& cue=horse_races::cues::Settings();cue.starter_shot=!cue.starter_shot;
                    ::horse_races::config::SetRaw(ini.c_str(),"Cues","StarterShot",cue.starter_shot?"1":"0");
                }
            }
        } else if(camp_page && !returning) {
            if(pressed[1])camp_selected=(camp_selected+kCampRows-1)%kCampRows;
            if(pressed[2])camp_selected=(camp_selected+1)%kCampRows;
            if(camp_selected==CampMary && !mary_activities.empty()) {
                if(pressed[5])mary_activity=(mary_activity+mary_activities.size()-1)%mary_activities.size();
                if(pressed[6])mary_activity=(mary_activity+1)%mary_activities.size();
            }
            if(camp_selected==CampArthur && !camp_activities.empty()) {
                if(pressed[5])camp_activity=(camp_activity+camp_activities.size()-1)%camp_activities.size();
                if(pressed[6])camp_activity=(camp_activity+1)%camp_activities.size();
            }
            if(pressed[3] && !pressed[0]) {
                if(camp_selected==CampSurvey)survey_request=true;
                else if(camp_selected==CampRecording)scene_recording=!scene_recording;
                else if(camp_selected==CampAim)aim_mode=!aim_mode;
                else if(camp_selected==CampArthur && !camp_activities.empty())camp_activity_request=camp_activity;
                else if(camp_selected==CampMary && !mary_activities.empty())mary_activity_request=mary_activity;
                else if(camp_selected==CampSweep)camp_sweep_request=true;
            }
        } else if(!returning) {
        if (pressed[1]) selected = (selected + kPlayerRows - 1) % kPlayerRows;
        if (pressed[2]) selected = (selected + 1) % kPlayerRows;
        if(selected!=RowMembershipReset){membership_reset_confirm_until={};membership_reset_status.clear();}
        if(selected==RowTestHost && (pressed[5] || pressed[6]))ChangeTestHost(pressed[5]?-1:1);
        if(selected==RowTestListener && (pressed[5] || pressed[6]))ChangeTestListener(pressed[5]?-1:1);
        if (pressed[3] && !pressed[0]) {
            if (selected == RowSpeedOverride) {
                change = !speed_override;
                ::horse_races::config::SetRaw(ini.c_str(),"General","EnabledOnStartup",*change ? "1" : "0");
            } else if(selected==RowShowSpeed) {
                speed_visible = !speed_visible;
                ::horse_races::config::SetRaw(ini.c_str(),"UI","ShowSpeed",speed_visible ? "1" : "0");
            } else if(selected==RowCasualMode) {
                ChangeCasualMode();
            } else if(selected==RowEncounterPage) {
                encounter_page=true;encounter_map_save_failed=false;
            } else if(selected==RowDifficulty) {
                difficulty_page=true;difficulty_selected=0;difficulty_save_failed=false;
                difficulty_rule=difficulty::Read(ini,difficulty_opponent);cornering_envelope=ReadCorneringEnvelope();
            } else if(selected==RowCorridor) {
                corridor_page=true;corridor_selected=0;corridor_save_failed=false;corridor_saved=false;
                corridor_tracking_margin=tracking_margin::Read(ini);
            } else if(selected==RowProgressPage) {
                progress_page=true;progress_selected=ProgressReset;
            } else if(selected==RowMembershipReset) {
                membership_reset_status.clear();
                if(Clock::now()<membership_reset_confirm_until){membership_reset_request=true;membership_reset_confirm_until={};}
                else membership_reset_confirm_until=Clock::now()+std::chrono::seconds(5);
            } else if(selected==RowEndActivity || selected==RowShowcase) {
                control_request.end_activity=selected==RowEndActivity;
                control_request.toggle_showcase=selected==RowShowcase;
                menu_open=false;membership_reset_confirm_until={};
                membership_reset_status.clear();
                AUDIO::PLAY_SOUND_FRONTEND("MENU_CLOSE","HUD_PLAYER_MENU",true,0);
            } else if(selected==RowTestHost)ChangeTestHost(1);
            else if(selected==RowTestListener)ChangeTestListener(1);
            else if(selected==RowCapturePosition){
                capture_position_request=true;
                horse_races::cues::Go();
                AUDIO::PLAY_SOUND_FRONTEND("MENU_ENTER","HUD_PLAYER_MENU",true,0);
            }
            else if(selected==RowRacePage){race_page=true;race_selected=0;}
            else if(selected==RowCampPage){camp_page=true;camp_selected=0;}
            else if(selected==RowMusicPage){music_page=true;music_selected=0;music_save_failed=false;music_saved=false;music_values=music::Read(ini);}
        }
        }
        if(encounter_page)DrawEncounterPage();else if(music_page)DrawMusicPage();else if(progress_page)DrawProgressPage();else if(difficulty_page)DrawDifficulty();
        else if(corridor_page)DrawCorridor();else if(race_page)DrawRacePage();else if(camp_page)DrawCampPage();
        else if(menu_open)DrawMenu(change.value_or(speed_override));
    }
    AimMode();     // The crosshair and its ray, menu open or not.
    Speedometer(); // Independent from speed_override and from menu visibility.
    LapHud();
    return change;
}
