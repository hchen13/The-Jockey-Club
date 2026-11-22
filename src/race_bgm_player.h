#pragma once
#include "race_bgm_fade.h"
#include <algorithm>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace horse_races::bgm {
enum class Backend { Stream, SoundOffset, MusicProfile };
struct Track {
    std::string_view id, title;
    const char* cue;
    const char* soundset; // nullptr addresses an original globally indexed sound.
    Backend backend;
    int offset_ms, duration_ms;
    const char* bank=nullptr; // Optional script bank; authored music/camp stream containers manage their own source banks.
    float gain_db=0;
};
struct Playlist {
    std::string_view key; // kind/station; betting and duels may share a station name.
    std::span<const std::uint8_t> tracks;
    bool ordered;
    int opening=-1; // Once per race, outside the repeating pool.
};

// One bag per race. Crossing a cycle never repeats the just-finished song.
// An optional opening precedes the bag once and never joins any cycle.
// Ordered programmes restart at their first track for every new race.
class Sequence {
    std::vector<std::uint8_t> bag_;
    std::mt19937 random_;
    std::size_t next_=0;
    int last_=-1;
    int opening_=-1;
    bool ordered_=false;
public:
    void Reset(const Playlist& list,std::uint32_t seed) {
        bag_.assign(list.tracks.begin(),list.tracks.end());
        random_.seed(seed);next_=bag_.size();last_=-1;ordered_=list.ordered;opening_=list.opening;
    }
    int Next() {
        if(opening_>=0){last_=opening_;opening_=-1;return last_;}
        if(bag_.empty())return -1;
        if(next_==bag_.size()) {
            if(!ordered_) {
                std::shuffle(bag_.begin(),bag_.end(),random_);
                if(bag_.size()>1 && bag_.front()==last_)std::swap(bag_.front(),bag_[1]);
            }
            next_=0;
        }
        return last_=bag_[next_++];
    }
};

// Owns a single native voice. Backend methods only ever address that voice.
// Time is supplied by the game clock; no worker thread makes native calls.
class Player {
public:
    enum class Phase { Idle, Loading, Ready, Starting, Playing, Gap, Exhausted, Fading, Retry };
private:
    const Playlist* list_=nullptr;
    std::span<const Track> catalogue_;
    Sequence sequence_;
    int current_=-1;
    std::size_t failures_=0;
    Phase phase_=Phase::Idle;
    bool wanted_=false;
    bool tail_attempted_=false;
    unsigned retries_=0;
    std::int64_t began_=0,polled_=0;
    static constexpr std::int64_t kLoadTimeout=8000,kStartTimeout=3000,kGap=400;
    template<class Native> void Select(std::int64_t now,Native& native) {
        retries_=0;current_=sequence_.Next();
        if(current_<0 || static_cast<std::size_t>(current_)>=catalogue_.size()) {
            phase_=Phase::Exhausted;native.Report("invalid_selection",nullptr,now,0);return;
        }
        began_=now;polled_=now;phase_=Phase::Loading;tail_attempted_=false;
        native.Report("selected",Current(),now,0);
    }
    template<class Native> void Advance(std::int64_t now,Native& native,bool failed,const char* reason) {
        if(!failed){native.Playing();if(SourceFailed(now,native))return;}
        native.Report(reason,Current(),now,now-began_);
        native.Stop();
        if(native.Failed()) {
            phase_=Phase::Exhausted;native.Report("programme_unavailable",Current(),now,0);return;
        }
        if(failed) {
            ++failures_;
            // Do not silently replace a mandatory opening with a random song.
            if(list_->ordered || current_==list_->opening || failures_>=list_->tracks.size()) {
                phase_=Phase::Exhausted;native.Report("programme_unavailable",Current(),now,0);return;
            }
        } else failures_=0;
        phase_=Phase::Gap;began_=now;
    }
    template<class Native> bool SourceFailed(std::int64_t now,Native& native) {
        if(!native.Failed())return false;
        if(retries_<3 && native.Recoverable()) {
            native.Report(("native_retry_scheduled; attempt="+std::to_string(retries_+1)+
                "; limit=3; delay_ms=1000").c_str(),Current(),now,now-began_);
            native.Stop();
            // Stop must still prove ownership. A raced replacement cancels
            // recovery, just as it cancels normal programme advancement.
            if(native.RecoverableStopped()) {
                ++retries_;phase_=Phase::Retry;began_=now;return true;
            }
        }
        // A failed or replaced owned source is not a completed song. Retain
        // the exhausted programme so later race ticks cannot start over an
        // unrelated mission's replacement or fall back to event playback.
        native.Report("native_failed",Current(),now,now-began_);native.Stop();
        phase_=Phase::Exhausted;native.Report("programme_unavailable",Current(),now,0);return true;
    }
public:
    const Track* Current() const {return current_<0 || static_cast<std::size_t>(current_)>=catalogue_.size()?nullptr:&catalogue_[current_];}
    const Playlist* List() const {return list_;}
    Phase State() const {return phase_;}
    std::int64_t PhaseElapsed(std::int64_t now) const {return now-began_;}
    bool Active() const {return list_!=nullptr;}
    template<class Native> void Stop(Native& native) {
        native.Stop();list_=nullptr;catalogue_={};current_=-1;phase_=Phase::Idle;wanted_=false;
    }
    template<class Native> void Prepare(const Playlist& list,std::span<const Track> catalogue,
                                        std::uint32_t seed,std::int64_t now,Native& native) {
        Stop(native);list_=&list;catalogue_=catalogue;sequence_.Reset(list,seed);failures_=0;
        Select(now,native);Tick(now,native);
    }
    template<class Native> void Start(std::int64_t now,Native& native) {
        if(!list_)return;
        // Resource preparation may have completed since the last invitation
        // poll. Check at GO itself instead of spending another 200 ms waiting
        // for the background polling deadline.
        wanted_=true;if(phase_==Phase::Loading)polled_=now;
        Tick(now,native);
    }
    template<class Native> void Tick(std::int64_t now,Native& native) {
        if(!list_ || phase_==Phase::Idle || phase_==Phase::Exhausted)return;
        if(phase_==Phase::Retry) {
            if(now-began_<1000)return;
            phase_=Phase::Loading;began_=polled_=now;tail_attempted_=false;
            native.Report("native_retry_preparing",Current(),now,0);
        }
        if(phase_==Phase::Gap) {
            if(!wanted_ || now-began_<kGap)return;
            Select(now,native);
        }
        if(phase_==Phase::Loading && now>=polled_) {
            polled_=now+200;
            if(native.Prepare(*Current())){phase_=Phase::Ready;native.Report("preload_ready",Current(),now,now-began_);}
            else if(now-began_>=kLoadTimeout){Advance(now,native,true,"preload_timeout");return;}
        }
        if(phase_==Phase::Ready && wanted_) {
            if(!native.Play(*Current())){Advance(now,native,true,"start_rejected");return;}
            native.Report("start_requested",Current(),now,0);
            phase_=Phase::Starting;began_=now;polled_=now;
        }
        if(phase_==Phase::Fading) {
            if(native.TailComplete(now) && !SourceFailed(now,native))Advance(now,native,false,"duration_limit");
            return;
        }
        if(phase_!=Phase::Starting && phase_!=Phase::Playing)return;
        if(phase_==Phase::Playing && now-began_>=Current()->duration_ms){Advance(now,native,false,"duration_limit");return;}
        if(phase_==Phase::Playing && !tail_attempted_ &&
           now-began_>=Current()->duration_ms-Fade::TailDuration(Current()->duration_ms)) {
            tail_attempted_=true;
            // A late frame shortens the tail instead of extending the excerpt.
            if(native.BeginTail(now,Current()->duration_ms-(now-began_))) {
                phase_=Phase::Fading;return;
            }
        }
        if(now<polled_)return;
        polled_=now+200;
        const bool playing=native.Playing();
        if(!playing && SourceFailed(now,native))return;
        if(playing && phase_==Phase::Starting) {
            native.Report("voice_active",Current(),now,now-began_);phase_=Phase::Playing;began_=now;
        } else if(!playing && phase_==Phase::Playing)Advance(now,native,false,"native_ended");
        else if(!playing && now-began_>=kStartTimeout)Advance(now,native,true,"start_timeout");
    }
};
} // namespace horse_races::bgm
