#pragma once
#include "game_natives.h"
#include "race_map_textures.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace horse_races::progression_toast {
inline constexpr int kDefaultDurationMs=6000;
inline constexpr int kMinDurationMs=4000;
inline constexpr int kHonorDurationMs=8000;
using Icon=std::optional<map_textures::Dictionary>;
struct Entry {std::string title,body;int duration_ms=kDefaultDurationMs;Icon icon;};
// Evidence: local SUHM.asi RVA 0x3B6F4-0x3B81A uses this native and
// two literal VAR_STRING tokens. rcm_chain_gang2.c func_377/func_690 use
// the same structure for mission completion, including this medal and cue.
inline int Post(const Entry& entry){
    const char* title=MISC::VAR_STRING(10,"LITERAL_STRING",entry.title.c_str());
    const char* body=MISC::VAR_STRING(10,"LITERAL_STRING",entry.body.c_str());
    // Engine player-string tokens are opaque; never dereference them.
    if(!title||!body)return 0;
    static constexpr char sound_set[]="Mission_Complete_Sounds";
    static constexpr char sound_name[]="Mission_Complete_Enter";
    std::array<std::uint64_t,13> params{};
    params[0]=static_cast<std::uint64_t>(entry.duration_ms);
    params[1]=reinterpret_cast<std::uint64_t>(sound_set);
    params[2]=reinterpret_cast<std::uint64_t>(sound_name);
    params[3]=1;
    std::array<std::uint64_t,8> data{};
    data[1]=reinterpret_cast<std::uint64_t>(title);
    data[2]=reinterpret_cast<std::uint64_t>(body);
    data[4]=entry.icon?map_textures::DictionaryHash(*entry.icon):MISC::GET_HASH_KEY("HUD_TOASTS");
    data[5]=entry.icon?map_textures::HorseSprite(*entry.icon):MISC::GET_HASH_KEY("TOAST_MEDAL_COLLECTABLE");
    data[7]=1;
    // The native plays its cue on presentation. Never separately PLAY_SOUND,
    // or play anything merely because an entry was placed in our queue.
    return invoke<int>(0x26E87218390E6729,params.data(),data.data(),true,true);
}
class Presenter {
    std::deque<Entry> pending_;
    std::optional<Entry> current_;
    int handle_=0;
    std::uint64_t retry_after_=0;
    std::function<void(const std::string&)> log_;
    std::string last_status_;
    std::optional<map_textures::Dictionary> map_icon_;
    void MapIcon(std::optional<map_textures::Dictionary> icon){
        if(map_icon_==icon)return;
        if(map_icon_)map_textures::Release(*map_icon_,map_textures::Owner::ProgressionToast);
        map_icon_=icon;
        if(map_icon_)map_textures::Acquire(*map_icon_,map_textures::Owner::ProgressionToast);
    }
    void Status(std::string value){
        if(value==last_status_)return;
        last_status_=std::move(value);
        if(log_)log_("progression toast "+last_status_);
    }
public:
    void SetLogger(std::function<void(const std::string&)> logger){log_=std::move(logger);last_status_.clear();}
    bool Show(std::string title,std::string body,int duration_ms=kDefaultDurationMs,Icon icon={}){
        if(title.empty()||body.empty()||title.size()>1024||body.size()>4096)return false;
        pending_.push_back({std::move(title),std::move(body),duration_ms<kMinDurationMs?kMinDurationMs:duration_ms,std::move(icon)});
        return true;
    }
    // Drop only our not-yet-posted events. An already posted native toast
    // naturally finishes; keep its handle as a fence instead of clearing a
    // shared UI channel or applying a help-text removal native to a toast.
    void Clear(){pending_.clear();if(!handle_)MapIcon({});retry_after_=0;Status("pending cleared");}
    // Only after a confirmed save timeline/protagonist change. Old-world feed
    // handles may never reach state6; relinquish ours without touching native UI.
    void ResetWorld(){pending_.clear();current_.reset();handle_=0;MapIcon({});retry_after_=0;Status("world reset");}
    void TickAt(std::uint64_t now,bool available=true){
        if(handle_){
            // Vanilla act_fishing03.c:21190 posts the next feed only at state6.
            const int state=UIFEED::_UI_FEED_GET_MESSAGE_STATE(handle_);
            Status("feed handle="+std::to_string(handle_)+" state="+std::to_string(state));
            if(state!=6)return;
            Status("dismissed handle="+std::to_string(handle_));
            handle_=0;current_.reset();
            MapIcon({});
        }
        if(pending_.empty())return;
        // Caller supplies only toast presentation conflicts, separately from
        // progress-edit/sale guards. A completed race Result is toast-safe.
        if(!available){Status("hold external pending="+std::to_string(pending_.size()));return;}
        if(HUD::IS_PAUSE_MENU_ACTIVE()){Status("hold pause pending="+std::to_string(pending_.size()));return;}
        if(now<retry_after_)return;
        MapIcon(pending_.front().icon);
        if(map_icon_&&!map_textures::HorseIconReady(*map_icon_)){
            Status("hold race icon dictionary="+std::string(map_textures::Name(*map_icon_)));return;
        }
        const int posted=Post(pending_.front());
        if(!posted){Status("retry native post returned zero");retry_after_=now+500;return;}
        handle_=posted;current_=std::move(pending_.front());pending_.pop_front();retry_after_=0;
        Status("posted handle="+std::to_string(handle_)+" pending="+std::to_string(pending_.size())+
            " duration_ms="+std::to_string(current_->duration_ms)+" title="+current_->title+" body="+current_->body);
    }
    std::size_t Pending()const{return pending_.size();}
    int OwnedHandle()const{return handle_;}
};
inline Presenter presenter;
inline std::uint64_t Now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline void Initialize(std::function<void(const std::string&)> logger={}){presenter.SetLogger(std::move(logger));presenter.ResetWorld();}
inline void SetLogger(std::function<void(const std::string&)> logger){presenter.SetLogger(std::move(logger));}
inline void Clear(){presenter.Clear();}
inline void ResetWorld(){presenter.ResetWorld();}
inline bool Show(std::string title,std::string body,int duration_ms=kDefaultDurationMs,Icon icon={}){return presenter.Show(std::move(title),std::move(body),duration_ms,std::move(icon));}
inline void Tick(bool available=true){presenter.TickAt(Now(),available);}
}
