#pragma once
#include "game_natives.h"
#include "game_clock.h"
#include "race_lap_view.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <string>

namespace horse_races::lap_hud {
// Story-mode beat_sharp_shooter.c: funcs 414 (bindings), 218 (show this
// frame), 47 (release). This is the stock two-competitor score display, not
// an online UI app or debug text dressed up with a custom background.
inline constexpr Hash kContext=static_cast<Hash>(-66088566);
inline constexpr const char* kRoot="helperTextfields";

class Board {
public:
    using Clock=horse_time::GameClock;
private:
    Any root_=0,owner_=0;
    int token_=0;
    inline static unsigned serial_=0;
    std::array<Any,4> fields_{};
    std::array<std::string,4> values_{};
    Clock::time_point retry_at_{};

    static bool Valid(Any entry) {
        return entry && entry<=std::numeric_limits<Hash>::max() &&
            DATABINDING::DATABINDING_IS_ENTRY_VALID(static_cast<Hash>(entry));
    }
    bool RootMatches() const {
        return Valid(root_) && DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH(kRoot)==root_;
    }
    bool OwnsRoot() const {
        return RootMatches() && Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)==token_;
    }
    void Forget() { root_=owner_=0;token_=0;fields_={};values_={}; }
    bool Create(const std::array<std::string,4>& values) {
        using namespace DATABINDING;
        // Never replace a shooting challenge's or another mod's live data.
        if(Valid(_DATABINDING_GET_DATA_CONTAINER_FROM_PATH(kRoot)))return false;
        root_=_DATABINDING_ADD_DATA_CONTAINER_FROM_PATH("",kRoot);
        if(!RootMatches()){Forget();return false;}
        token_=static_cast<int>(1U+(++serial_%0x7FFFFFFFU));
        owner_=_DATABINDING_ADD_DATA_INT(root_,"hslLapHudOwner",token_);
        constexpr std::array<const char*,4> names{"rawLabel0","rawLabel1","rawValue0","rawValue1"};
        bool ok=OwnsRoot();
        for(std::size_t i=0;ok && i<fields_.size();++i) {
            fields_[i]=_DATABINDING_ADD_DATA_STRING(root_,names[i],values[i].c_str());
            ok=Valid(fields_[i]);
        }
        if(!ok) {
            // No script yield occurred since creation and the context was never
            // enabled. Roll back partial bindings without disabling any HUD.
            if(RootMatches())_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(root_));
            Forget();return false;
        }
        values_=values;
        return true;
    }
public:
    // Explicit script-thread cleanup only: no native calls from static DLL
    // destructors / loader-lock detach callbacks.
    void Close() {
        if(OwnsRoot()) {
            // The MP standings context can render helperTextfields too. Clear
            // cached text before removing our root so a later context cannot
            // redisplay the previous duel's final frame.
            for(std::size_t i=0;i<fields_.size();++i)if(Valid(fields_[i]) && !values_[i].empty())
                DATABINDING::_DATABINDING_WRITE_DATA_STRING(fields_[i],"");
            HUD::_DISABLE_HUD_CONTEXT(kContext);
            DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(root_));
        }
        Forget();retry_at_={};
    }
    // Bookmaker races enable a different native HUD context which also reads
    // helperTextfields. Keep an owned, empty binding for the entire handoff;
    // simply closing this board lets the engine resurrect cached duel text.
    void Mask(Clock::time_point now=Clock::now()) {
        if(root_ && !OwnsRoot()){Forget();retry_at_=now+std::chrono::seconds(1);return;}
        if(root_ && !std::all_of(fields_.begin(),fields_.end(),Valid)){
            Close();retry_at_=now+std::chrono::seconds(1);return;
        }
        if(!root_){
            if(now<retry_at_)return;
            retry_at_=now+std::chrono::seconds(1);
            if(!Create({}))return;
        }
        for(std::size_t i=0;i<fields_.size();++i)if(!values_[i].empty()){
            DATABINDING::_DATABINDING_WRITE_DATA_STRING(fields_[i],"");
            values_[i].clear();
        }
        HUD::_DISABLE_HUD_CONTEXT(kContext);
    }
    bool Show(const circuit::View& view,const std::string& player_name,
        const std::string& rival_name,bool chinese,Clock::time_point now=Clock::now()) {
        if(!view.visible || view.total<1){Close();return false;}
        // Values remain plain strings. The native widget owns fonts, layout and
        // safe-zone placement; BG/Scaleform FONT markup must not leak into it.
        // The rows are the view's: the board and the fallback text say the same words.
        const std::array<std::string,4> values{view.PlayerRow(player_name,chinese),
            view.RivalRow(rival_name,chinese),view.Place(),view.Gap()};
        if(root_ && !OwnsRoot()) {
            // A replaced/recycled root belongs to somebody else. Do not hide
            // their context, delete their bindings or keep writing old handles.
            Forget();retry_at_=now+std::chrono::seconds(1);return false;
        }
        if(root_ && !std::all_of(fields_.begin(),fields_.end(),Valid)) {
            Close();retry_at_=now+std::chrono::seconds(1);return false;
        }
        if(!root_) {
            if(now<retry_at_)return false;
            retry_at_=now+std::chrono::seconds(1);
            if(!Create(values))return false;
        }
        for(std::size_t i=0;i<fields_.size();++i)if(values[i]!=values_[i]) {
            DATABINDING::_DATABINDING_WRITE_DATA_STRING(fields_[i],values[i].c_str());
            values_[i]=values[i];
        }
        HUD::_ENABLE_HUD_CONTEXT_THIS_FRAME(kContext);
        return true;
    }
};
}
