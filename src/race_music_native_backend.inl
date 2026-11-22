// Only original game resources. Selected native streams have an owned voice;
// Arthur's reviewed three pieces use the same owned music request path.
// Included after Clock and Log exist. No external audio player or copied WAVs.
struct NativeBgmBankLease {const char* name=nullptr;unsigned owners=0;bool ready=false;};
std::vector<NativeBgmBankLease> g_native_bgm_banks;
bool RequestNativeBgmBank(const char* name,bool new_owner) {
    auto it=std::find_if(g_native_bgm_banks.begin(),g_native_bgm_banks.end(),
        [&](const auto& bank){return std::string_view(bank.name)==name;});
    if(it==g_native_bgm_banks.end()){g_native_bgm_banks.push_back({name});it=std::prev(g_native_bgm_banks.end());}
    if(new_owner)++it->owners;
    if(!it->ready)it->ready=AUDIO::REQUEST_SCRIPT_AUDIO_BANK(name)!=0;
    return it->ready;
}
void ReleaseNativeBgmBank(const char* name) {
    const auto it=std::find_if(g_native_bgm_banks.begin(),g_native_bgm_banks.end(),
        [&](const auto& bank){return std::string_view(bank.name)==name;});
    if(it==g_native_bgm_banks.end())return;
    if(it->owners>1){--it->owners;return;}
    AUDIO::RELEASE_NAMED_SCRIPT_AUDIO_BANK(name);g_native_bgm_banks.erase(it);
}
struct RetiredNativeMusic {
    bgm::native_music::Token token;
    const bgm::Track* track=nullptr;
    bool bank_requested=false;
};
std::vector<RetiredNativeMusic> g_retired_native_music;
std::int64_t g_retired_native_music_polled=0;
struct NativeBgmBackend {
    const bgm::Track* track=nullptr;
    int stream=-1,sound=-1;
    bool bank_requested=false,bank_ready=false;
    bgm::audio::SoundOwner sound_owner;
    bgm::audio::StreamGain gain;
    bgm::audio::fade::Volume volume_voice;
    bool volume_voice_applied=false;
    std::optional<bgm::volume::Group> volume_group;
    int volume_percent=-1;float volume_effective_db=0;
    bgm::audio::StemSelection stems;
    bgm::native_music::Token music;
    bool music_prepared=false,music_started=false,music_fading=false;
    bool preparation_observed=false,failure_observed=false;
    bool source_failed=false;
    bool recovery_stopped=false;
    bool programme_started=false; // Survives per-track Stop/retries; Bind starts a new race.
    std::int64_t music_output_started=-1;
    unsigned music_output_samples=0;
    bgm::Fade tail;
    bgm::audio::fade::Volume tail_volume;
    bool tail_midpoint=false;
    std::int64_t tail_polled=0;
    bgm::native_music::Status music_status=bgm::native_music::Status::Finished;
    std::string music_reason;
    bgm::native_music::ResourceCheck music_resource_check;
    bool last_active=false,observed_pause=false;
    std::string race_key;
    std::uint32_t seed=0;
    std::uint64_t session=0,ordinal=0,last_wall=0;
    std::size_t count=0;
    bool has_opening=false;
    std::int64_t last_game=0,heartbeat=0,go_game=-1;
    int load_polls=0;
    static std::uint32_t CueHash(const char* text) {
        std::uint32_t h=0;if(!text)return h;
        for(;*text;++text){const auto c=static_cast<unsigned char>(*text);h+=(c>='A' && c<='Z'?c+32:c);h+=h<<10;h^=h>>6;}
        h+=h<<3;h^=h>>11;return h+(h<<15);
    }
    void Bind(const bgm::Playlist& list,std::uint32_t value) {
        static std::uint64_t next_session=0;
        session=++next_session;race_key=std::string(list.key);seed=value;count=list.tracks.size();ordinal=0;
        has_opening=list.opening>=0;
        programme_started=false;
        last_wall=0;last_game=heartbeat=0;go_game=-1;observed_pause=false;
        Report(has_opening?"programme_opening_then_shuffle":list.ordered?"programme_ordered":"programme_shuffle",nullptr,0,0);
    }
    bool Prepare(const bgm::Track& next) {
        source_failed=false;
        track=&next;
        ++load_polls;
        if(recovery_stopped && next.backend==bgm::Backend::MusicProfile)
            for(const auto& retired:g_retired_native_music)
                if(retired.track==&next && !NativeBgmMusic::BankUnused(retired.token))return false;
        recovery_stopped=false;
        if(next.bank && !bank_ready) {
            const bool first=!bank_requested;
            bank_requested=true;
            const bool ready=RequestNativeBgmBank(next.bank,first);
            if(first)Report("bank_requested",&next,0,0);
            if(!ready)return false;
            if(!bank_ready){bank_ready=true;Report("bank_ready",&next,0,0);}
        }
        if(next.backend==bgm::Backend::MusicProfile) {
            if(!music_prepared) {
                const auto kind=bgm::native_music::KindFor(next.id);
                music_prepared=kind && NativeBgmMusic::Prepare(*kind,music);
                if(!kind)music.reason="profile_id";
                const auto reason=std::string(music.reason);
                if(music_prepared || reason!=music_reason || music.resource_check!=music_resource_check) {
                    music_reason=reason;music_resource_check=music.resource_check;
                    std::ostringstream detail;
                    detail<<(music_prepared?"music_profile_ready":"music_profile_unavailable")<<"; reason="<<reason;
                    if(!music_prepared)detail<<"; resource=0x"<<std::hex<<music_resource_check.name<<std::dec
                        <<"; byte="<<music_resource_check.offset<<"; expected="<<music_resource_check.expected
                        <<"; actual="<<music_resource_check.actual<<"; child="<<music_resource_check.child
                        <<"; leaf="<<music_resource_check.leaf;
                    Report(detail.str().c_str(),&next,0,0);
                }
            }
            return music_prepared;
        }
        const bool ready=AUDIO::LOAD_STREAM(next.cue,next.soundset)!=0;
        // LOAD_STREAM may discard/recreate a pending graph. The getter is a
        // pure lookup in this build: refresh the tag even if we held one before.
        const int current=AUDIO::_GET_LOADED_STREAM_ID_FROM_CREATION(next.cue,next.soundset);
        if(current!=stream) {
            if(stream>=0){Report("preload_handle_replaced",&next,0,0);AUDIO::STOP_STREAM(stream);}
            stream=current;
        }
        return ready && stream>=0;
    }
    bool Play(const bgm::Track& next) {
        volume_group=bgm::volume::For(next.id);
        volume_percent=volume_group?bgm::volume::ReadPercent(g_ini_path,*volume_group):100;
        volume_effective_db=volume_group?bgm::volume::Gain(next.gain_db,*volume_group,volume_percent):next.gain_db;
        volume_voice={};volume_voice_applied=false;
        Report("volume_selected; applies_at_track_start",&next,0,0);
        if(next.backend==bgm::Backend::MusicProfile) {
            music_output_started=-1;music_output_samples=0;
            const bool accepted=NativeBgmMusic::Start(music,SCRIPTS::GET_ID_OF_THIS_THREAD(),next.offset_ms,
                volume_effective_db,!programme_started);
            if(accepted)programme_started=true;
            else if(music.status==bgm::native_music::Status::Replaced)source_failed=true;
            // Even an exceptional return may follow a partial native enqueue.
            // Keep the nonce so Stop can prove ownership and cancel it safely.
            music_started=music.identity!=0;
            Report((std::string(accepted?"music_start_requested":"music_start_rejected")+
                "; reason="+std::string(music.reason)+MusicDetail()).c_str(),&next,0,0);
            return accepted;
        }
        if(stream<0)return false;
        if(next.backend==bgm::Backend::Stream) {
            if(next.id=="native:american-venom/stem-09") {
                bgm::audio::StemCheck why;
                if(!NativeBgmAudio::SelectStems(stream,stems,&why) || !NativeBgmAudio::Verify(stems,&why)) {
                    StemFailure("stem_selection_unavailable",next,why);return false;
                }
                Report("stem_selection_verified; selected_slot=11; selected_db=0; other_stems_db=-100",&next,0,0);
            }
            if(next.gain_db>0) {
                const bool applied=NativeBgmAudio::Gain(stream,next.gain_db,gain,next.id);
                Report((std::string(applied?"gain_applied":"gain_unavailable")+"; reason="+
                    std::string(applied?std::string_view("none"):gain.failure)+"; before_db="+
                    std::to_string(gain.before)+"; after_db="+std::to_string(gain.after)).c_str(),&next,0,0);
            }
            // The ready stream has an owned root before PLAY_STREAM_FRONTEND.
            // Apply the user's attenuation there, so even 0% never starts at
            // the old volume. The established vocal boost remains the baseline.
            if(volume_group && volume_percent!=bgm::volume::Default(*volume_group)) {
                if(!NativeBgmFadeAudio::Capture(stream,false,volume_voice,true) ||
                   volume_voice.owner.thread!=static_cast<std::uint32_t>(SCRIPTS::GET_ID_OF_THIS_THREAD()) ||
                   !NativeBgmFadeAudio::Set(volume_voice,volume_percent==0?-100.0F:
                       volume_voice.initial+volume_effective_db-next.gain_db)) {
                    Report("volume_unavailable; ready_owner_or_gain_changed",&next,0,0);return false;
                }
                volume_voice_applied=true;
                Report("volume_applied_before_start",&next,0,0);
            }
            AUDIO::PLAY_STREAM_FRONTEND(stream);
            bgm::audio::StemCheck why;
            if(stems.applied && !NativeBgmAudio::Verify(stems,&why)) {
                AUDIO::STOP_STREAM(stream);StemFailure("stem_selection_changed_after_start",next,why);return false;
            }
            if(volume_voice_applied) {
                if(!NativeBgmFadeAudio::Set(volume_voice,volume_voice.current)) {
                    AUDIO::STOP_STREAM(stream);Report("volume_readback_changed",&next,0,0);return false;
                }
                Report("volume_verified_after_start",&next,0,0);
            } else if(gain.applied)Report(NativeBgmAudio::Verify(gain)?"gain_verified_after_start":"gain_readback_changed",&next,0,0);
        }
        else {
            // Public frontend creation supports a global cue, but its name-based
            // ID lookup searches a different namespace. Allocate ownership first.
            if(!NativeBgmAudio::OffsetSupported()){Report("offset_bridge_unsupported",&next,0,0);return false;}
            sound=AUDIO::GET_SOUND_ID();
            if(sound<0){Report("sound_id_unavailable",&next,0,0);return false;}
            if(!NativeBgmAudio::Reserve(sound,sound_owner)) {
                AUDIO::RELEASE_SOUND_ID(sound);sound=-1;Report("sound_reservation_unverified",&next,0,0);return false;
            }
            const bool started=NativeBgmAudio::PlayOffset(sound_owner,next.cue,next.offset_ms);
            bgm::audio::SoundOwner active;
            if(!started || !NativeBgmAudio::Capture(sound,CueHash(next.cue),active)) {
                if(NativeBgmAudio::Owns(sound_owner)){AUDIO::_STOP_SOUND_WITH_ID(sound);AUDIO::RELEASE_SOUND_ID(sound);}
                sound=-1;sound_owner={};Report(started?"sound_binding_unverified":"offset_start_rejected",&next,0,0);return false;
            }
            sound_owner=active;
        }
        programme_started=true;
        return true;
    }
    bool Playing() {
        if(!track)return false;
        if(track->backend==bgm::Backend::MusicProfile) {
            const auto status=NativeBgmMusic::Poll(music);
            const auto reason=std::string(music.reason);
            if(status!=music_status || reason!=music_reason) {
                music_status=status;music_reason=reason;
                Report(("music_state; state="+std::to_string(static_cast<int>(status))+"; reason="+reason+MusicDetail()).c_str(),track,0,0);
            }
            if(!preparation_observed && status==bgm::native_music::Status::Pending && reason=="preparing_source") {
                preparation_observed=true;
                Report(("music_source_preparing; "+NativeBgmDiagnostics::Describe(music)).c_str(),track,0,0);
            }
            if(!failure_observed && status==bgm::native_music::Status::Failed) {
                failure_observed=true;
                Report(("music_source_failure; "+NativeBgmDiagnostics::Describe(music)).c_str(),track,0,0);
            }
            if(status==bgm::native_music::Status::Playing && music_output_samples<2) {
                const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
                if(music_output_started<0)music_output_started=now;
                const auto elapsed=now-music_output_started;
                if(elapsed>=(music_output_samples?5000:500)) {
                    ++music_output_samples;
                    ObserveMusicOutput(elapsed);
                }
            }
            return last_active=status==bgm::native_music::Status::Playing;
        }
        if(track->backend==bgm::Backend::Stream) {
            const bool playing=stream>=0 && AUDIO::IS_STREAM_PLAYING(stream);
            bgm::audio::StemCheck why;
            if(playing && stems.applied && !NativeBgmAudio::Verify(stems,&why)) {
                AUDIO::STOP_STREAM(stream);StemFailure("stem_selection_lost",*track,why);return last_active=false;
            }
            return last_active=playing;
        }
        const bool active=sound>=0 && NativeBgmAudio::Owns(sound_owner) && !AUDIO::_HAS_SOUND_ID_FINISHED(sound);
        if(active && stream>=0){AUDIO::STOP_STREAM(stream);stream=-1;} // Silent preload is no longer needed.
        return last_active=active;
    }
    bool Failed() const {
        return source_failed || (music_started && (music.status==bgm::native_music::Status::Failed ||
            music.status==bgm::native_music::Status::Replaced));
    }
    bool Recoverable() const {
        return music_started && music.status==bgm::native_music::Status::Failed &&
            (music.reason=="native_error" || music.reason=="native_create_failed");
    }
    bool RecoverableStopped() const {return recovery_stopped;}
    std::string MusicDetail() const {
        std::ostringstream detail;
        detail<<"; native_state="<<music.observed_state<<"; observed_root=0x"<<std::hex<<music.observed_root
            <<"; owned_root=0x"<<music.root<<std::dec<<"; start_bound="<<music.root_started
            <<"; owned_start_ms="<<music.root_started_at<<"; observed_start_ms="<<music.observed.started_at
            <<"; root_type="<<static_cast<unsigned>(music.observed.type);
        return detail.str();
    }
    void ObserveMusicOutput(std::int64_t elapsed) {
        // Two read-only observations per track. These describe the native
        // wave clock and routed matrix, never claim decoded PCM or audibility,
        // and cannot change playback or trigger retries.
        const auto observed=NativeBgmOutput::Inspect(music);
        std::ostringstream detail;
        detail<<"music_output; sample="<<music_output_samples<<"; playing_ms="<<elapsed
            <<"; reason="<<observed.reason<<"; owned="<<observed.owned
            <<"; source_child_index="<<observed.source_child_index<<"; category_control="<<observed.control_number
            <<"; mood=0x"<<std::hex<<music.mood.name<<"; mix=0x"<<music.mix.name<<std::dec;
        detail<<"; requested_gain_db="<<volume_effective_db<<"; mixer_gain_available="<<observed.gain_available;
        if(observed.gain_available)detail<<"; mixer_gain_db="<<observed.applied_gain_db;
        if(observed.available)for(std::size_t i=0;i<observed.channels.size();++i) {
            const auto& channel=observed.channels[i];const char* side=i?"right":"left";
            detail<<"; "<<side<<"_wave="<<channel.wave<<"; "<<side<<"_voice="<<channel.voice
                <<"; "<<side<<"_cursor_samples="<<channel.cursor_samples<<"; "<<side<<"_rate="<<channel.rate
                <<"; "<<side<<"_metadata_gain="<<channel.metadata_gain<<"; "<<side<<"_matrix_peak="<<channel.matrix_peak
                <<"; "<<side<<"_buses=";
            bool first=true;
            for(const auto bus:channel.buses)if(bus!=127){detail<<(first?"":",")<<static_cast<unsigned>(bus);first=false;}
            if(first)detail<<"none";
        }
        Report(detail.str().c_str(),track,0,elapsed);
    }
    void StemFailure(const char* event,const bgm::Track& selected,const bgm::audio::StemCheck& why) {
        Report((std::string(event)+"; reason="+std::string(why.reason)+"; child_slot="+
            std::to_string(why.child<0?-1:why.child+1)).c_str(),&selected,0,0);
    }
    bool BeginTail(std::int64_t now,std::int64_t duration) {
        if(!track || duration<=0 || !Playing())return false;
        bool accepted=false;
        if(track->backend==bgm::Backend::MusicProfile) {
            accepted=NativeBgmMusic::Stop(music,static_cast<int>(duration));
            if(accepted){music_fading=true;tail.Begin(now,0,duration);}
        } else {
            const bool generic=sound>=0;
            accepted=NativeBgmFadeAudio::Capture(generic?sound:stream,generic,tail_volume) &&
                tail.Begin(now,tail_volume.initial,duration);
        }
        Report((std::string(accepted?"track_fade_started":"track_fade_unavailable")+
            "; duration_ms="+std::to_string(duration)+"; initial_db="+std::to_string(tail.initial_db)).c_str(),track,now,0);
        tail_polled=now;tail_midpoint=false;return accepted;
    }
    bool TailComplete(std::int64_t now) {
        if(music_fading) {
            if(now<tail_polled)return false;
            tail_polled=now+200;
            const auto status=NativeBgmMusic::Poll(music);
            // Native Stop owns the asynchronous tail. Never start another
            // track or release its bank merely because our timer elapsed.
            if(status==bgm::native_music::Status::Fading)return false;
            Report("track_fade_native_retired",track,now,now-tail.began);return true;
        }
        if(now>=tail_polled) {
            tail_polled=now+200;
            if(!Playing()){Report("track_fade_voice_ended",track,now,now-tail.began);return true;}
        }
        const auto db=tail.Gain(now);
        if(!NativeBgmFadeAudio::Set(tail_volume,db)) {
            Report("track_fade_owner_or_gain_changed",track,now,now-tail.began);return true;
        }
        if(tail.Complete(now)){Report("track_fade_completed",track,now,now-tail.began);return true;}
        if(!tail_midpoint && now-tail.began>=tail.length_ms/2) {
            tail_midpoint=true;
            Report(("track_fade_progress; gain_db="+std::to_string(db)).c_str(),track,now,now-tail.began);
        }
        return false;
    }
    void Stop() {
        recovery_stopped=false;
        const bool recoverable=Recoverable();
        const bool had_voice=stream>=0 || sound>=0 || bank_requested || music_started;
        if(music_started && !music_fading) {
            const bool stopped=NativeBgmMusic::Stop(music,0);
            recovery_stopped=recoverable && stopped;
            Report((std::string(stopped?"music_owned_stop":"music_stop_skipped")+
                "; reason="+std::string(music.reason)).c_str(),track,0,0);
        }
        source_failed=Failed(); // Preserve a lost owner discovered by Stop's final ownership check.
        int sound_after=-1,stream_after=-1;
        if(sound>=0) {
            // Generic IDs can be recycled. Check the captured context, source,
            // root and raw record, never a failed name lookup for a global cue.
            if(NativeBgmAudio::Owns(sound_owner)){AUDIO::_STOP_SOUND_WITH_ID(sound);sound_after=!AUDIO::_HAS_SOUND_ID_FINISHED(sound);AUDIO::RELEASE_SOUND_ID(sound);}
            else Report("stale_sound_skipped",track,0,0);
        }
        if(stream>=0){AUDIO::STOP_STREAM(stream);stream_after=AUDIO::IS_STREAM_PLAYING(stream)!=0;}
        if(gain.applied)Report(NativeBgmAudio::Restore(gain)?"gain_restored":"gain_restore_skipped",track,0,0);
        if(stems.applied)Report(NativeBgmAudio::Restore(stems)?"stem_selection_restored":"stem_selection_released_or_stale",track,0,0);
        if((music.identity && music.status!=bgm::native_music::Status::Finished &&
           music.status!=bgm::native_music::Status::Replaced) ||
           (bank_requested && music_prepared && !NativeBgmMusic::BankUnused(music))) {
            // Stop (including zero fade) retires its root asynchronously. A
            // new invitation/load may relinquish our player before that point.
            // Retain the bank lease until the engine retires/replaces the root.
            g_retired_native_music.push_back({music,track,bank_requested});
            bank_requested=false;
            Report("music_cleanup_deferred",track,0,0);
        }
        if(bank_requested && track && track->bank)ReleaseNativeBgmBank(track->bank);
        if(had_voice)Report(("released; stream_active_after="+std::to_string(stream_after)+
            "; sound_active_after="+std::to_string(sound_after)).c_str(),track,0,0);
        track=nullptr;stream=sound=-1;sound_owner={};gain={};stems={};bank_requested=bank_ready=false;
        volume_voice={};volume_voice_applied=false;volume_group.reset();volume_percent=-1;volume_effective_db=0;
        music={};music_prepared=music_started=music_fading=false;
        preparation_observed=failure_observed=false;
        music_output_started=-1;music_output_samples=0;
        tail={};tail_volume={};tail_midpoint=false;tail_polled=0;
        music_status=bgm::native_music::Status::Finished;music_reason.clear();
        music_resource_check={};
        last_active=false;load_polls=0;
    }
    void Report(const char* event,const bgm::Track* selected,std::int64_t game_ms,std::int64_t elapsed_ms) {
        if(!game_ms)game_ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        if(std::string_view(event)=="selected")++ordinal;
        const auto loop_ordinal=has_opening && ordinal?ordinal-1:ordinal;
        std::ostringstream out;
        out<<"[NativeBGM] session="<<GetCurrentProcessId()<<':'<<session<<" race="<<race_key
           <<" event="<<event<<" seed="<<seed<<" ordinal="<<ordinal
           <<" section="<<(!ordinal?"programme":has_opening && ordinal==1?"opening":"loop")
           <<" loop_ordinal="<<loop_ordinal<<" cycle="<<(count && loop_ordinal?(loop_ordinal-1)/count+1:0)<<" pool="<<count
           <<" game_ms="<<game_ms<<" phase_ms="<<elapsed_ms<<" go_ms="<<(go_game<0?-1:game_ms-go_game)<<" polls="<<load_polls
           <<" stream="<<stream<<" sound="<<sound<<" native_active="<<last_active;
        if(selected)out<<" track="<<selected->id<<" cue="<<selected->cue<<" set="<<(selected->soundset?selected->soundset:"<global>")
           <<" cue_hash=0x"<<std::hex<<CueHash(selected->cue)<<" set_hash=0x"<<CueHash(selected->soundset)<<std::dec
           <<" backend="<<(selected->backend==bgm::Backend::Stream?"stream":
                selected->backend==bgm::Backend::MusicProfile?"native_music":"offset_sound")
           <<" offset_ms="<<selected->offset_ms<<" duration_ms="<<selected->duration_ms
           <<" bank="<<(selected->bank?selected->bank:"<stream-managed>")<<" bank_ready="<<bank_ready
           <<" gain_db="<<selected->gain_db;
        if(volume_group && volume_percent>=0)out<<" volume_category="<<bgm::volume::specs[static_cast<std::size_t>(*volume_group)].key
            <<" volume_percent="<<volume_percent<<" effective_gain_db="<<volume_effective_db;
        // Sparse transitions must survive the normal two-lines/second folding.
        horse_races::log_sink::Shared().Write(out.str(),true);
    }
} g_native_bgm_backend;
