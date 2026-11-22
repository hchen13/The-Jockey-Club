bgm::Player g_native_bgm;
// Stable addresses also outlive a retired native tail from an earlier race.
auto g_arthur_music_tracks=bgm::catalogue::tracks;
bool PrepareArthurMusicProfile();
bgm::Fade g_native_bgm_fade;
bgm::audio::fade::Volume g_native_bgm_fade_volume;
bool g_native_bgm_fade_midpoint=false;
std::int64_t g_native_bgm_fade_polled=0;
std::int64_t NativeBgmNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
void EndNativeRaceFade(const char* reason) {
    if(!g_native_bgm_fade.active)return;
    const auto now=NativeBgmNow();
    g_native_bgm_backend.Report(reason,g_native_bgm.Current(),now,now-g_native_bgm_fade.began);
    // Original music Stop already owns its eight-second asynchronous tail.
    // Stop clears its ownership nonce immediately. Even on load/new race,
    // observing the remaining tail cannot authorize a second global stop.
    if(g_native_bgm_backend.music_fading)
        g_native_bgm_backend.music_started=false;
    g_native_bgm.Stop(g_native_bgm_backend);
    g_native_bgm_fade={};g_native_bgm_fade_volume={};g_native_bgm_fade_midpoint=false;g_native_bgm_fade_polled=0;
}
void TickNativeRaceMusic(){
    const auto now=NativeBgmNow();const auto wall=GetTickCount64();
    if(now>=g_retired_native_music_polled && !g_retired_native_music.empty()) {
        g_retired_native_music_polled=now+200;
        for(auto it=g_retired_native_music.begin();it!=g_retired_native_music.end();) {
            auto status=it->token.identity?NativeBgmMusic::Poll(it->token):bgm::native_music::Status::Finished;
            if(status==bgm::native_music::Status::Pending || status==bgm::native_music::Status::Playing ||
               status==bgm::native_music::Status::Failed) {
                NativeBgmMusic::Stop(it->token,0);status=it->token.status;
            }
            if(status!=bgm::native_music::Status::Finished && status!=bgm::native_music::Status::Replaced){++it;continue;}
            // Losing the request nonce does not retire the old source: a new
            // request can be queued while its predecessor is still fading.
            if(it->bank_requested && !NativeBgmMusic::BankUnused(it->token)){++it;continue;}
            if(it->bank_requested && it->track && it->track->bank)ReleaseNativeBgmBank(it->track->bank);
            horse_races::log_sink::Shared().Write("[NativeBGM] event=retired_music_released; track="+
                std::string(it->track?it->track->id:std::string_view{})+"; reason="+std::string(it->token.reason),true);
            it=g_retired_native_music.erase(it);
        }
    }
    if(!g_native_bgm.Active())return;
    auto& native=g_native_bgm_backend;
    const bool paused=HUD::IS_PAUSE_MENU_ACTIVE()!=0;
    if(paused!=native.observed_pause) {
        native.Playing();native.Report(paused?"pause_observed":"resume_observed",g_native_bgm.Current(),now,now-native.last_game);
        native.observed_pause=paused;
    }
    if(native.last_wall && wall-native.last_wall>2000 &&
       static_cast<std::int64_t>(wall-native.last_wall)-(now-native.last_game)>1000)
        native.Report(("script_gap; wall_ms="+std::to_string(wall-native.last_wall)).c_str(),g_native_bgm.Current(),now,now-native.last_game);
    native.last_wall=wall;native.last_game=now;
    if(g_native_bgm_fade.active) {
        if(paused)return;
        if(native.music_fading) {
            if(now>=g_native_bgm_fade_polled) {
                g_native_bgm_fade_polled=now+200;
                const auto status=NativeBgmMusic::Poll(native.music);
                if(status==bgm::native_music::Status::Finished){EndNativeRaceFade("fade_voice_ended");return;}
                if(status==bgm::native_music::Status::Replaced || status==bgm::native_music::Status::Failed){
                    EndNativeRaceFade("fade_music_replaced");return;
                }
            }
            // The engine owns this tail and may retire its root on a later
            // audio tick. Keep the bank until Poll confirms completion instead
            // of treating our eight-second timer as a native completion signal.
            return;
        }
        if(now>=g_native_bgm_fade_polled) {
            g_native_bgm_fade_polled=now+200;
            if(!native.Playing()){EndNativeRaceFade("fade_voice_ended");return;}
        }
        const auto db=g_native_bgm_fade.Gain(now);
        if(!NativeBgmFadeAudio::Set(g_native_bgm_fade_volume,db)) {
            EndNativeRaceFade("fade_owner_or_gain_changed");return;
        }
        if(g_native_bgm_fade.Complete(now)){EndNativeRaceFade("fade_completed");return;}
        if(!g_native_bgm_fade_midpoint && now-g_native_bgm_fade.began>=g_native_bgm_fade.length_ms/2) {
            g_native_bgm_fade_midpoint=true;
            native.Report(("fade_progress; gain_db="+std::to_string(db)).c_str(),g_native_bgm.Current(),now,now-g_native_bgm_fade.began);
        }
        return; // A finished race never advances to the next song while fading.
    }
    if(!paused)g_native_bgm.Tick(now,native);
    if(now>=native.heartbeat) {
        native.heartbeat=now+15000;
        const auto detail="heartbeat; phase="+std::to_string(static_cast<int>(g_native_bgm.State()))+
            "; global_music_active="+std::to_string(AUDIO::AUDIO_IS_MUSIC_PLAYING())+
            "; global_music_time="+std::to_string(AUDIO::GET_MUSIC_PLAYTIME());
        native.Report(detail.c_str(),g_native_bgm.Current(),now,g_native_bgm.PhaseElapsed(now));
    }
}
const bgm::Playlist* SelectedNativeBgm(const char* venue) {
    if(GetPrivateProfileIntA("Cues","NativeRacePlaylist",0,g_ini_path.c_str())==0)return nullptr;
    if(venue)return bgm::catalogue::Find(std::string("betting/")+venue);
    const auto* station=StationOf(g_variant);
    if(!station)return nullptr;
    if(std::string_view(station->key)=="arthur-camp-duel" && !CurrentRules().breed) {
        // Restore the established mission-event programme. Its private
        // replacement fails to load WNT4 in the captured regression.
        return nullptr;
    }
    return bgm::catalogue::Find(std::string(CurrentRules().breed?"challenge/":"duel/")+station->key);
}
