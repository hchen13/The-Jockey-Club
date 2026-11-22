#pragma once

#include "game_clock.h"
#include "game_natives.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace horse_races::race_help {
// hunting1.c's original main-horse tutorial uses this same native tooltip.
// The on-screen duration is controlled by the native feed, in milliseconds.
inline constexpr std::uint64_t kDurationMs=4000,kRecoveryMs=2000,kRetryMs=250;
inline constexpr std::uint64_t kPollMs=100,kVisibleWaitMs=10000;
inline constexpr std::size_t kMaxPending=32;

struct Native {
    int Post(const std::string& text) const {
        const char* token=MISC::VAR_STRING(10,"LITERAL_STRING",text.c_str());
        if(!token)return 0;
        // Script structs use eight-byte cells. The native SP tooltip's first
        // block has 13 slots and the second places its opaque text in slot 1.
        std::array<std::uint64_t,13> params{};
        std::array<std::uint64_t,8> content{};
        params[0]=kDurationMs;content[1]=reinterpret_cast<std::uint64_t>(token);
        return UIFEED::_UI_FEED_POST_HELP_TEXT(reinterpret_cast<Any*>(params.data()),
            reinterpret_cast<Any*>(content.data()),true);
    }
    int State(int handle) const{return UIFEED::_UI_FEED_GET_MESSAGE_STATE(handle);}
    int Current() const{return UIFEED::UI_FEED_GET_CURRENT_MESSAGE(1);}
    void Clear(int handle) const{UIFEED::_UI_FEED_CLEAR_HELP_TEXT_FEED(handle,false);}
    bool Paused() const{return HUD::IS_PAUSE_MENU_ACTIVE();}
};

// Only handles issued by this presenter can be cleared. Race exits drop
// pending entries but let the already-issued native message finish naturally.
// Boundary events are immediate and may interrupt our own elimination; that
// elimination is then replayed, so an urgent warning does not lose its name.
template<class Api> class Presenter {
    struct Entry {std::string text;std::uint64_t generation=0;bool boundary=false;};
    struct Active {
        Entry entry;int handle=0;std::uint64_t posted_at=0,until=0,next_poll=0;
        bool started=false,shown=false;
    };
    Api& api_;
    std::deque<Entry> pending_;
    std::optional<Entry> urgent_;
    std::optional<Active> active_;
    std::uint64_t generation_=0,retry_at_=0;

    bool Duplicate(const Entry& entry) const {
        if(active_ && active_->entry.generation==entry.generation &&
            active_->entry.boundary==entry.boundary && active_->entry.text==entry.text)return true;
        if(urgent_ && urgent_->generation==entry.generation && urgent_->text==entry.text)return true;
        for(const auto& existing:pending_)if(existing.generation==entry.generation &&
            existing.boundary==entry.boundary && existing.text==entry.text)return true;
        return false;
    }
    void Observe(std::uint64_t now){
        if(!active_ || now<active_->next_poll || (active_->shown && now<active_->until))return;
        const int state=api_.State(active_->handle);
        if(!active_->shown){
            if(state==3)active_->started=true; // Entering may already be visible.
            // Vanilla state 4 is Showing. State 5 is already Exiting; do not
            // add a fresh four-second fence if we first saw the tail end.
            if(state==4){
                active_->started=active_->shown=true;active_->until=now+kDurationMs;active_->next_poll=active_->until;
            }else if(state==5){
                active_->started=active_->shown=true;active_->until=now;active_->next_poll=now+kPollMs;
            }else if(state==6){
                active_.reset();
            }else if(now-active_->posted_at>=kVisibleWaitMs){
                // A queued, recognised native handle may be removed only by
                // its own ID. Unknown states are never cleared as if valid.
                if(state>=0 && state<3)api_.Clear(active_->handle);
                const bool replay=!active_->entry.boundary && active_->entry.generation==generation_;
                if(replay)pending_.push_front(std::move(active_->entry));
                active_.reset();retry_at_=now+kRetryMs;
            }else active_->next_poll=std::min(now+kPollMs,active_->posted_at+kVisibleWaitMs);
            return;
        }
        // Keep our own four-second fence even if the engine ends it early.
        // A never-ending stale handle is relinquished without channel clear.
        if(state==6 || now-active_->until>=kRecoveryMs)active_.reset();
        else active_->next_poll=std::min(now+kPollMs,active_->until+kRecoveryMs);
    }
    void Pump(std::uint64_t now){
        if(!active_ && !urgent_ && pending_.empty())return;
        Observe(now);
        if(active_ || now<retry_at_ || (!urgent_ && pending_.empty()) || api_.Paused())return;
        const Entry* next=urgent_?&*urgent_:pending_.empty()?nullptr:&pending_.front();
        if(!next)return;
        const int handle=api_.Post(next->text);
        // Vanilla scripts test feed handles against zero, not positivity.
        if(!handle){retry_at_=now+kRetryMs;return;}
        active_=Active{*next,handle,now,0,now,false,false};
        if(urgent_)urgent_.reset();else pending_.pop_front();
        retry_at_=0;
        Observe(now);
    }
public:
    explicit Presenter(Api& api):api_(api){}
    bool Boundary(std::string text,std::uint64_t now){
        if(text.empty() || text.size()>4096)return false;
        Entry incoming{std::move(text),generation_,true};
        // The caller invokes this only on an outside edge. If our previous
        // warning has already begun showing, leave it uninterrupted rather
        // than flashing or queueing a duplicate warning after a short return.
        Observe(now);
        if(active_ && !api_.Paused()){
            const int state=api_.State(active_->handle);
            if(active_->entry.boundary && state>=3 && state<=5){
                active_->started=true;
                if(!active_->shown && state==4){
                    active_->shown=true;active_->until=now+kDurationMs;active_->next_poll=active_->until;
                }else if(!active_->shown && state==5){
                    active_->shown=true;active_->until=now;active_->next_poll=now+kPollMs;
                }
                return true;
            }
            // Only our own nonfinished, recognised state may be cleared.
            // Unknown/stale states are relinquished without a native clear.
            const bool fresh=active_->shown?now<active_->until+kRecoveryMs:
                now-active_->posted_at<kVisibleWaitMs;
            const bool owned_active=(state>=0 && state<3) ||
                (state>=3 && state<6 && api_.Current()==active_->handle);
            if(owned_active && fresh)
                api_.Clear(active_->handle);
            if(!active_->entry.boundary && state!=6 && active_->entry.generation==generation_)
                pending_.push_front(std::move(active_->entry));
            active_.reset();
        }
        urgent_=std::move(incoming);retry_at_=0;Pump(now);return true;
    }
    bool Elimination(std::string text,std::uint64_t now){
        if(text.empty() || text.size()>4096)return false;
        Entry incoming{std::move(text),generation_,false};
        if(Duplicate(incoming))return true;
        // Reserve a slot for the currently displayed elimination: an urgent
        // warning may need to put it back without discarding anyone.
        if(pending_.size()+(active_ && !active_->entry.boundary?1U:0U)>=kMaxPending)return false;
        pending_.push_back(std::move(incoming));Pump(now);return true;
    }
    void Tick(std::uint64_t now){Pump(now);}
    void CancelPendingBoundary(std::uint64_t now){
        if(urgent_){urgent_.reset();retry_at_=0;}
        if(active_ && active_->entry.boundary && !active_->started){
            const int state=api_.State(active_->handle);
            if(state==4){
                active_->started=active_->shown=true;active_->until=now+kDurationMs;active_->next_poll=active_->until;
            }else if(state==5){
                active_->started=active_->shown=true;active_->until=now;active_->next_poll=now+kPollMs;
            }else if(state==3){
                active_->started=true;active_->next_poll=now;
            }else if(state>=0 && state<3){
                api_.Clear(active_->handle);active_.reset();
            }else if(state==6)active_.reset();
            // Unknown states cannot safely be cleared; leave the owned
            // handle fenced until bounded recovery.
        }
        Pump(now);
    }
    void Reset(std::uint64_t now){
        if(!HasResetWork())return;
        pending_.clear();urgent_.reset();retry_at_=0;
        if(active_ && active_->entry.generation==generation_ && !active_->started){
            const int state=api_.State(active_->handle);
            if(state>=0 && state<3){
                api_.Clear(active_->handle);active_.reset();
            }else if(state==3){
                active_->started=true;active_->next_poll=now;
            }else if(state==4){
                active_->started=active_->shown=true;active_->until=now+kDurationMs;active_->next_poll=active_->until;
            }else if(state==5){
                active_->started=active_->shown=true;active_->until=now;active_->next_poll=now+kPollMs;
            }else if(state==6)active_.reset();
            // Unknown states remain fenced and are never cleared blindly.
        }
        ++generation_;
    }
    bool HasWork() const{return active_.has_value() || urgent_.has_value() || !pending_.empty();}
    bool HasResetWork() const{return !pending_.empty() || urgent_.has_value() ||
        (active_ && active_->entry.generation==generation_);}
    bool HasPendingBoundary() const{return urgent_.has_value() ||
        (active_ && active_->entry.boundary && !active_->started);}
    std::size_t Pending() const{return pending_.size()+(urgent_?1:0);}
    int OwnedHandle() const{return active_?active_->handle:0;}
};

inline Native native;
inline Presenter<Native> presenter(native);
inline std::uint64_t Now(){
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        horse_time::GameClock::now().time_since_epoch()).count());
}
inline bool Boundary(std::string text){return presenter.Boundary(std::move(text),Now());}
inline bool Elimination(std::string text){return presenter.Elimination(std::move(text),Now());}
inline void Tick(){if(presenter.HasWork())presenter.Tick(Now());}
inline void CancelPendingBoundary(){if(presenter.HasPendingBoundary())presenter.CancelPendingBoundary(Now());}
inline void Reset(){if(presenter.HasResetWork())presenter.Reset(Now());}
} // namespace horse_races::race_help
