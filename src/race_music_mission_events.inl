struct RaceMusicState {
    bool requested=false,prepared=false,wanted=false,accepted=false,fade_requested=false,triggered=false;
} g_race_music;
constexpr const char* kRaceMusicStart="REHR_START";
constexpr const char* kRaceMusicEnd="REHR_END";

// A playlist of the game's own music events in place of the encounter score
// ([Cues] RaceMusicPlaylist), played in order and round again. An entry is a
// chain of one score's own stage events, each with the seconds it is given
// ("WNT1_START@20>WNT1_ACTION@220"; kPlaylistDefaultSeconds when unsaid - a
// mission's score loops until its next event, so a length has to be chosen).
// The chain is how a piece fades IN, the mission's own way, from its intro
// stage to its action stage, since no native ramps a music volume.
// How a piece ends matters as much as how it starts: after the game's global
// STOP_MUSIC_8S the next piece's stage event was accepted and nothing sounded
// (2026-09-20 18:34, BRT1_TO_SALOON) - the music system had been told to stop
// and a stage event alone did not start it again. So a piece ends with its own
// mission's stop event ("|WNT4_STOP" at the end of the entry; <code>_STOP by
// default, from the first stage's prefix; STOP_MUSIC_8S only when that is
// refused). Known endings retain their authored retirement time before
// the next entry starts; custom endings retain the eight-second default.
// RaceMusicPlaylistStations names the corridor stations it plays for; empty,
// every hosted race. An empty playlist is the vanilla score. The event names
// come from the game (femga/rdr3_discoveries music_events.lua); which carries
// which piece is a matter of listening: [Cues] AuditionEvent plays any one
// name outside a race.
constexpr double kPlaylistDefaultSeconds=180,kFadeSeconds=8;
struct PlaylistStage {std::string event;double seconds=kPlaylistDefaultSeconds;};
struct PlaylistEntry {
    std::vector<PlaylistStage> stages;std::string stop;
    std::string Code() const {const auto& e=stages.front().event;const auto u=e.find('_');return u==std::string::npos?e:e.substr(0,u);}
    std::string Stop() const {return stop.empty()?Code()+"_STOP":stop;}
    std::string Text() const {std::string t;for(const auto& s:stages)t+=(t.empty()?"":">")+s.event+"@"+std::to_string(static_cast<int>(s.seconds));return t+"|"+Stop();}
};
std::vector<PlaylistEntry> g_music_playlist;
std::vector<std::string> g_music_playlist_stations;
struct PlaylistState {
    bool active=false,fading=false,failed=false;std::size_t index=0,stage=0;int rounds=0;
    Clock::time_point started{},checked{},fade_started{};std::string playing;double gap=kFadeSeconds;
    int observations=0,native_samples=0;bool observed_sounding=false,observed_time_valid=false;
} g_playlist;
Clock::time_point g_mission_music_retired_until{};
struct AuditionState {std::string event,wanted;Clock::time_point polled{},started{};bool report_due=false;} g_audition;

void LoadRaceMusicConfig() {
    g_music_playlist.clear();g_music_playlist_stations.clear();
    char list[1024]{};GetPrivateProfileStringA("Cues","RaceMusicPlaylist","",list,sizeof list,g_ini_path.c_str());
    std::istringstream in(list);std::string item;
    auto trim=[](std::string t){const auto f=t.find_first_not_of(" \t"),l=t.find_last_not_of(" \t");return f==std::string::npos?std::string():t.substr(f,l-f+1);};
    while(std::getline(in,item,',')) {
        PlaylistEntry e;
        const auto bar=item.find('|');
        if(bar!=std::string::npos){e.stop=trim(item.substr(bar+1));item=item.substr(0,bar);}
        std::istringstream chain(item);std::string word;
        while(std::getline(chain,word,'>')) {
            word=trim(word);if(word.empty())continue;
            PlaylistStage s;const auto at=word.find('@');   // The stage's seconds.
            s.event=trim(at==std::string::npos?word:word.substr(0,at));
            if(at!=std::string::npos){const double v=std::atof(word.c_str()+at+1);if(std::isfinite(v) && v>=5 && v<=1800)s.seconds=v;}
            if(!s.event.empty())e.stages.push_back(s);
        }
        if(!e.stages.empty())g_music_playlist.push_back(e);
    }
    char stations[512]{};GetPrivateProfileStringA("Cues","RaceMusicPlaylistStations","",stations,sizeof stations,g_ini_path.c_str());
    std::istringstream sin(stations);
    while(std::getline(sin,item,',')){const auto f=item.find_first_not_of(" \t"),l=item.find_last_not_of(" \t");if(f!=std::string::npos)g_music_playlist_stations.push_back(item.substr(f,l-f+1));}
    if(!g_music_playlist.empty()) {
        std::string names;for(const auto& e:g_music_playlist)names+=(names.empty()?"":", ")+e.Text();
        Log("BGM playlist: "+names+"; stations "+(stations[0]?std::string(stations):std::string("all"))+"; authored endings, custom ending default 8 s");
    }
}
bool PlaylistApplies() {
    if(g_music_playlist.empty())return false;
    if(g_music_playlist_stations.empty())return true;
    const char* station=CorridorStationKey();
    for(const auto& s:g_music_playlist_stations)if(station && s==station)return true;
    return false;
}
bool PrepareArthurMusicProfile() {
    if(!PlaylistApplies() || g_music_playlist.size()!=3)return false;
    const auto* list=bgm::catalogue::Find("duel/arthur-camp-duel");
    constexpr const char* endings[]{"WNT4_STOP","RGNG01_RIDE_STOP","FIN1_STOP"};
    for(std::size_t i=0;i<3;++i) {
        const auto& entry=g_music_playlist[i];const auto& track=bgm::catalogue::tracks[list->tracks[i]];
        if(entry.stages.size()!=1 || entry.stages[0].event!=track.cue || entry.Stop()!=endings[i])return false;
    }
    // Keep each configured budget; source identity, rather than an exact
    // spelling of duration/whitespace in the INI, selects the owned backend.
    for(std::size_t i=0;i<3;++i)
        g_arthur_music_tracks[list->tracks[i]].duration_ms=static_cast<int>(g_music_playlist[i].stages[0].seconds*1000);
    return true;
}
double PlaylistBudgetSeconds(const PlaylistEntry& entry,std::size_t stage);
void PlaylistReport(const char* event,const std::string& detail={}) {
    const auto& p=g_playlist;
    std::ostringstream out;
    out<<"[EventBGM] event="<<event<<" cue="<<(p.playing.empty()?"none":p.playing)
        <<" piece="<<p.index+1<<" stage="<<p.stage+1<<" round="<<p.rounds+1
        <<" elapsed_ms="<<(p.playing.empty()?0:std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-p.started).count());
    if(p.index<g_music_playlist.size() && p.stage<g_music_playlist[p.index].stages.size()) {
        const auto& entry=g_music_playlist[p.index];
        const auto effective=static_cast<int>(PlaylistBudgetSeconds(entry,p.stage)*1000);
        out<<" budget_ms="<<effective<<" requested_budget_ms="<<static_cast<int>(entry.stages[p.stage].seconds*1000)
           <<" effective_budget_ms="<<effective;
    }
    out<<" global_any_music="<<AUDIO::AUDIO_IS_MUSIC_PLAYING()<<" global_playtime="<<AUDIO::GET_MUSIC_PLAYTIME()
        <<" global_status_only=1; horse_race_scene="<<AUDIO::IS_AUDIO_SCENE_ACTIVE("horse_race_scene")
        <<"; "<<NativeBgmDiagnostics::DescribeEvent();
    if(!detail.empty())out<<"; "<<detail;
    horse_races::log_sink::Shared().Write(out.str(),true);
}
double PlaylistEndingSeconds(const PlaylistEntry& entry) {
    // Exact authored StopTrack durations in the original mission actions.
    // Unknown/custom events retain the previous eight-second policy.
    const auto stop=entry.Stop();
    if(stop=="WNT4_STOP")return 15;
    if(stop=="RGNG01_RIDE_STOP" || stop=="FIN1_STOP")return 10;
    return kFadeSeconds;
}
double PlaylistBudgetSeconds(const PlaylistEntry& entry,std::size_t stage) {
    const auto requested=entry.stages[stage].seconds;
    const auto stop=entry.Stop();
    if(stage+1==entry.stages.size() &&
       (stop=="WNT4_STOP" || stop=="RGNG01_RIDE_STOP" || stop=="FIN1_STOP"))
        return std::max(requested,PlaylistEndingSeconds(entry));
    return requested;
}
void PlaylistBeginEnding(const char* why) {
    auto& p=g_playlist;
    if(p.fading || p.playing.empty())return;
    const auto stop=g_music_playlist[p.index].Stop();
    bool accepted=AUDIO::TRIGGER_MUSIC_EVENT(stop.c_str());
    std::string ending=stop;
    if(!accepted){accepted=AUDIO::TRIGGER_MUSIC_EVENT("STOP_MUSIC_8S");ending+=" refused, STOP_MUSIC_8S";}
    AUDIO::CANCEL_MUSIC_EVENT(p.playing.c_str());
    p.gap=ending==stop?PlaylistEndingSeconds(g_music_playlist[p.index]):kFadeSeconds;
    p.fading=true;p.fade_started=Clock::now();
    // If neither ending was accepted, starting another score could overlap
    // the current one. Exhaust the programme instead of assuming silence.
    if(!accepted)p.failed=true;
    PlaylistReport(accepted?"ending_started":"ending_rejected",std::string("reason=")+why+" ending="+ending+
        " accepted="+std::to_string(accepted)+" fade_ms="+std::to_string(static_cast<int>(p.gap*1000)));
}
// Trigger stage `stage` of entry `index`. Only an accepted event replaces
// the current stage; a rejected request ends this race's programme once.
void PlaylistTrigger(std::size_t index,std::size_t stage,const char* why) {
    auto& p=g_playlist;
    if(g_music_playlist.empty())return;
    index%=g_music_playlist.size();
    const auto& e=g_music_playlist[index];
    stage=std::min(stage,e.stages.size()-1);
    const auto& s=e.stages[stage];
    const bool prepared=AUDIO::PREPARE_MUSIC_EVENT(s.event.c_str());
    const bool accepted=AUDIO::TRIGGER_MUSIC_EVENT(s.event.c_str());
    if(!accepted) {
        AUDIO::CANCEL_MUSIC_EVENT(s.event.c_str());
        PlaylistReport("trigger_rejected","requested="+s.event+" prepared="+std::to_string(prepared)+" trigger_accepted=0; programme stopped");
        PlaylistBeginEnding("next_stage_rejected");
        p.active=true;p.failed=true;return;
    }
    p.index=index;p.stage=stage;
    p.playing=s.event;p.started=Clock::now();p.checked=p.started+std::chrono::seconds(5);
    p.active=true;p.fading=false;p.failed=false;p.observations=0;p.native_samples=0;
    if(PlaylistBudgetSeconds(e,stage)>s.seconds)PlaylistReport("budget_adjusted","reason=authored_ending_minimum");
    PlaylistReport("stage_started",std::string("reason=")+why+" prepared="+std::to_string(prepared)+" trigger_accepted=1");
}
void TickPlaylist() {
    auto& p=g_playlist;
    const auto now=Clock::now();
    if(!p.active){p.rounds=0;PlaylistTrigger(0,0,"race start");return;}
    if(p.failed)return;
    if(p.fading) {
        // The piece is fading out; the next starts once the fade is done.
        if(std::chrono::duration<double>(now-p.fade_started).count()<p.gap)return;
        const std::size_t next=(p.index+1)%g_music_playlist.size();
        if(next==0)++p.rounds;
        PlaylistTrigger(next,0,"after the fade");
        return;
    }
    const auto& e=g_music_playlist[p.index];
    const auto& s=e.stages[p.stage];
    const double elapsed=std::chrono::duration<double>(now-p.started).count();
    // Six sparse snapshots cover startup, the captured 6.7 s dropout and
    // FIN1's authored 16 s transition. They never drive playback decisions.
    constexpr double sample_seconds[]{1,5,8,16,20,40};
    if(p.native_samples<6 && elapsed>=sample_seconds[p.native_samples]) {
        while(p.native_samples<6 && elapsed>=sample_seconds[p.native_samples])++p.native_samples;
        PlaylistReport("native_snapshot","sample="+std::to_string(p.native_samples));
    }
    // The final stage's budget includes its ending, just like an owned
    // native track. Send the mission's STOP before the source runs out.
    const bool last_stage=p.stage+1==e.stages.size();
    const double budget=PlaylistBudgetSeconds(e,p.stage);
    const double cutoff=last_stage?std::max(0.0,budget-PlaylistEndingSeconds(e)):budget;
    // These natives query the global music manager, not this accepted event.
    // Its auxiliary clock can become -1 while its sound remains active.
    // Neither a negative time nor a transient global silence owns our cue or
    // proves completion. Keep bounded observations for diagnosis ONLY; the
    // configured stage budget is the sequencing authority for mission events.
    if(now>=p.checked && p.observations<6) {
        p.checked=now+std::chrono::milliseconds(200);
        const bool sounding=AUDIO::AUDIO_IS_MUSIC_PLAYING()!=0;
        const bool time_valid=AUDIO::GET_MUSIC_PLAYTIME()>=0;
        if(!p.observations || sounding!=p.observed_sounding || time_valid!=p.observed_time_valid) {
            p.observed_sounding=sounding;p.observed_time_valid=time_valid;++p.observations;
            PlaylistReport("global_observation","observation="+std::to_string(p.observations));
        }
    }
    if(elapsed<cutoff)return;
    if(!last_stage){PlaylistTrigger(p.index,p.stage+1,"stage_budget");return;}
    PlaylistBeginEnding("stage_budget");
}
void StopPlaylist(const char* why) {
    auto& p=g_playlist;
    if(!p.active)return;
    if(p.playing.empty()){PlaylistReport("programme_closed",why);p={};return;}
    if(p.fading) {
        g_mission_music_retired_until=p.fade_started+std::chrono::milliseconds(static_cast<long long>(p.gap*1000));
        // A race result or subsequent cleanup must not restart the authored
        // ending already in flight, nor advance this playlist afterwards.
        for(const auto& e:g_music_playlist)for(const auto& stage:e.stages)
            AUDIO::CANCEL_MUSIC_EVENT(stage.event.c_str());
        PlaylistReport("ending_retained",why);p={};return;
    }
    // Cancelling an event alone does not stop its instantiated score. End
    // the accepted current piece once; unrelated future stop actions can
    // also reset music state and must not run merely because they are listed.
    PlaylistBeginEnding(why);
    if(!p.failed)g_mission_music_retired_until=p.fade_started+std::chrono::milliseconds(static_cast<long long>(p.gap*1000));
    PlaylistReport("programme_closed",why);
    p={};
}
// Outside a race: [Cues] AuditionEvent, read every two seconds; a changed name
// is played (the previous cancelled) and four seconds later the log says
// whether any music is playing. Blank it to stop.
void TickMusicAudition() {
    auto& a=g_audition;
    const auto now=Clock::now();
    if(g_state==State::Racing || g_playlist.active || g_native_bgm.Active() || g_race_music.wanted)return;
    if(a.report_due && now>=a.started+std::chrono::seconds(4)) {
        a.report_due=false;
        Log("BGM audition: "+a.event+" after 4 s: any_music_playing="+std::to_string(AUDIO::AUDIO_IS_MUSIC_PLAYING())+" playtime="+std::to_string(AUDIO::GET_MUSIC_PLAYTIME()));
    }
    if(now<a.polled)return;
    a.polled=now+std::chrono::seconds(2);
    char value[128]{};GetPrivateProfileStringA("Cues","AuditionEvent","",value,sizeof value,g_ini_path.c_str());
    std::string wanted(value);
    const auto f=wanted.find_first_not_of(" \t"),l=wanted.find_last_not_of(" \t");
    wanted=f==std::string::npos?std::string():wanted.substr(f,l-f+1);
    if(wanted==a.wanted)return;
    a.wanted=wanted;
    if(!a.event.empty()){Log("BGM audition: "+a.event+" cancelled="+std::to_string(AUDIO::CANCEL_MUSIC_EVENT(a.event.c_str())));a.event.clear();}
    if(wanted.empty()){a.report_due=false;return;}
    const bool prepared=AUDIO::PREPARE_MUSIC_EVENT(wanted.c_str());
    const bool accepted=AUDIO::TRIGGER_MUSIC_EVENT(wanted.c_str());
    a.event=wanted;a.started=now;a.report_due=true;
    Log("BGM audition: "+wanted+" prepared="+std::to_string(prepared)+" trigger_accepted="+std::to_string(accepted));
}

void PrepareRaceMusic(const char* venue=nullptr)
{
    if(g_race_music.requested)return;
    EndNativeRaceFade("fade_replaced_by_new_race");
    LoadRaceMusicConfig();
    g_race_music.requested=true;
    if(const auto* list=SelectedNativeBgm(venue)) {
        static std::mt19937 seeds{std::random_device{}()};
        const auto seed=static_cast<std::uint32_t>(seeds());
        g_native_bgm_backend.Bind(*list,seed);
        const std::span<const bgm::Track> tracks=list->key=="duel/arthur-camp-duel"?
            g_arthur_music_tracks:bgm::catalogue::tracks;
        g_native_bgm.Prepare(*list,tracks,seed,NativeBgmNow(),g_native_bgm_backend);
        Log("native BGM: selected "+std::string(list->key)+"; tracks="+std::to_string(list->tracks.size())+
            "; mode="+(list->opening>=0?"opening once then shuffle bag":list->ordered?"ordered":"shuffle bag")+"; invitation preload only");
        return;
    }
    if(PlaylistApplies()){const auto& first=g_music_playlist.front().stages.front().event;g_race_music.prepared=AUDIO::PREPARE_MUSIC_EVENT(first.c_str());Log("BGM prepare playlist first event="+first+" ready="+std::to_string(g_race_music.prepared));return;}
    g_race_music.prepared=AUDIO::PREPARE_MUSIC_EVENT(kRaceMusicStart);
    Log("BGM prepare event=REHR_START ready="+std::to_string(g_race_music.prepared)+
        "; preloading only, no playback during invitation");
}

void TickRaceMusic()
{
    horse_diagnostics::Scope stage_cost("race: music");
    auto& music=g_race_music;
    if(!music.wanted || music.fade_requested || HUD::IS_PAUSE_MENU_ACTIVE())return;
    if(g_native_bgm.Active()){TickNativeRaceMusic();return;}
    if(PlaylistApplies()){
        if(Clock::now()<g_mission_music_retired_until)return;
        TickPlaylist();music.accepted=!g_playlist.playing.empty();return;
    }
    // One trigger per race. The retry budget this used to carry - twelve
    // attempts 750 ms apart - never spent a second one: all 104 races in the
    // 2026-09-23 log read "attempt=1 trigger_accepted=1", and the budget was
    // never exhausted. It was development scaffolding around a native that
    // takes the event first time.
    if(music.triggered)return;
    music.triggered=true;
    if(!music.prepared)music.prepared=AUDIO::PREPARE_MUSIC_EVENT(kRaceMusicStart);
    // Preload is best-effort: vanilla beat_horse_race triggers REHR directly.
    // A false PREPARE must not suppress that known-working start path (some
    // transition events need no preparable resource).
    music.accepted=AUDIO::TRIGGER_MUSIC_EVENT(kRaceMusicStart);
    Log("BGM start event=REHR_START prepared="+std::to_string(music.prepared)+
        " trigger_accepted="+std::to_string(music.accepted));
}

void StartRaceMusic(const char* venue=nullptr)
{
    PrepareRaceMusic(venue);
    if(g_race_music.wanted || g_race_music.fade_requested)return;
    g_race_music.wanted=true;
    if(!AUDIO::IS_AUDIO_SCENE_ACTIVE("horse_race_scene")) {
        const bool scene=AUDIO::START_AUDIO_SCENE("horse_race_scene");
        Log("BGM race audio scene start accepted="+std::to_string(scene));
    }
    if(g_native_bgm.Active()) {
        const auto now=NativeBgmNow();
        g_native_bgm_backend.go_game=now;
        g_native_bgm_backend.Report(("race_go; preload_phase="+std::to_string(static_cast<int>(g_native_bgm.State()))).c_str(),
            g_native_bgm.Current(),now,g_native_bgm.PhaseElapsed(now));
        g_native_bgm.Start(now,g_native_bgm_backend);
        return;
    }
    TickRaceMusic();
}

void StopRaceMusic(bool result_fade,const char* reason="race_cleanup")
{
    // Result/departure cleanup can run before the eight-second tail is done.
    // Only a new race, load or explicit session boundary may interrupt it.
    if(g_native_bgm_fade.active) {
        if(std::string_view(reason)!="race_cleanup")EndNativeRaceFade(reason);
        return;
    }
    auto& music=g_race_music;
    if(!music.requested)return; // Unrelated loading/cleanup must not stop another score.
    music.wanted=false;
    if(g_native_bgm.Active()) {
        g_native_bgm_backend.Report(result_fade?"race_result":reason,g_native_bgm.Current(),NativeBgmNow(),
            g_native_bgm.PhaseElapsed(NativeBgmNow()));
        const auto& native=g_native_bgm_backend;
        if(result_fade && native.tail.active) {
            // Continue the exact existing envelope; starting a fresh fade
            // here would raise volume or extend a music-owned native tail.
            g_native_bgm_fade=native.tail;g_native_bgm_fade_volume=native.tail_volume;
            g_native_bgm_fade_midpoint=native.tail_midpoint;g_native_bgm_fade_polled=native.tail_polled;
            g_native_bgm_backend.Report("fade_continued_from_track_end",g_native_bgm.Current(),NativeBgmNow(),
                NativeBgmNow()-native.tail.began);
            music={};return;
        }
        if(result_fade && g_native_bgm_backend.Playing()) {
            if(native.track && native.track->backend==bgm::Backend::MusicProfile) {
                if(NativeBgmMusic::Stop(g_native_bgm_backend.music,static_cast<int>(bgm::Fade::duration_ms))) {
                    g_native_bgm_backend.music_fading=true;g_native_bgm_fade.Begin(NativeBgmNow(),0);
                    g_native_bgm_backend.Report("fade_started; duration_ms=8000; native_music=1",g_native_bgm.Current(),NativeBgmNow(),0);
                    music={};return;
                }
            } else {
            const bool sound=native.sound>=0;
            if(NativeBgmFadeAudio::Capture(sound?native.sound:native.stream,sound,g_native_bgm_fade_volume) &&
               g_native_bgm_fade.Begin(NativeBgmNow(),g_native_bgm_fade_volume.initial)) {
                g_native_bgm_backend.Report(("fade_started; duration_ms=8000; initial_db="+
                    std::to_string(g_native_bgm_fade_volume.initial)).c_str(),g_native_bgm.Current(),NativeBgmNow(),0);
                music={};return;
            }
            }
            g_native_bgm_backend.Report("fade_unavailable; owned_voice_released",g_native_bgm.Current(),NativeBgmNow(),0);
        }
        g_native_bgm.Stop(g_native_bgm_backend);music={};
        Log("native BGM: race/session ended; owned voices released");
        return;
    }
    if(g_playlist.active) {
        if(result_fade) {
            PlaylistBeginEnding("race_result");
            music.fade_requested=true;return;
        }
        StopPlaylist(reason);music={};return;
    }
    if(music.accepted && result_fade) {
        if(!music.fade_requested) {
            music.fade_requested=true;
            Log("BGM finish fade STOP_MUSIC_8S accepted="+
                std::to_string(AUDIO::TRIGGER_MUSIC_EVENT("STOP_MUSIC_8S")));
        }
        return; // REHR_END is issued once at the original later cleanup boundary.
    }
    StopPlaylist("race over");
    if(music.accepted)Log("BGM cleanup REHR_END accepted="+std::to_string(AUDIO::TRIGGER_MUSIC_EVENT(kRaceMusicEnd)));
    music={};
}
