#pragma once

#include "race_character_policy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace horse_races::bookmaking {

struct NativeStandingRow {
    bool out=false;
    float fraction=0;
    const char* portrait="";
    const char* portrait_dict="l_016e22bcpp";
};

// These are original Story Mode HUD textures, not exported or mod-owned art.
// Rains Fall has no headshot in this dictionary; his original Story Mode map
// character monogram provides an unambiguous native icon instead.
inline const char* StandingPortrait(characters::Id id){
    switch(id){
    case characters::Id::MaryBeth:return "headshot_marybeth";
    case characters::Id::Charles:return "headshot_charles";
    case characters::Id::Sadie:return "headshot_sadie";
    case characters::Id::Arthur:return "headshot_arthur";
    case characters::Id::John:return "headshot_john";
    case characters::Id::Uncle:return "headshot_uncle";
    case characters::Id::Karen:return "headshot_karen";
    case characters::Id::Pearson:return "headshot_pearson";
    case characters::Id::Trelawny:return "headshot_trelawny";
    case characters::Id::Tilly:return "headshot_tilly";
    case characters::Id::Javier:return "headshot_javier";
    case characters::Id::Bill:return "headshot_bill";
    case characters::Id::RainsFall:return "blip_scm_rains";
    default:return "";
    }
}
inline const char* StandingPortraitDict(characters::Id id){
    return id==characters::Id::RainsFall?"blips":"l_016e22bcpp";
}

// Rockstar's cTimer_two_meters_plus_eight_meters style provides meter0..9:
// two principal meters, then eight subordinate meters. Original camp-request
// The original SP preloader omitted the dynamic_icon template dependency.
// The opt-in native resource composition inlines its image and circle mask
// between the stock background and progress images, in the SAME layout box.
// Keep the previously visible sprite renderer available until live acceptance.
// Binding/resource tests cannot certify rendered pixels. Left-to-right is rank.
// Rings show race progress and eliminated riders receive a grey cross. This
// native layout has no legible room for rider names, rank numbers or lap text.
template<class Api>
class NativeStandingsHudT {
    using Handle=typename Api::Handle;
    static constexpr int kFlowBlock=-119209833;
    static constexpr int kStateMachine=1546991729;
    static constexpr const char* kRoot="centralInfoDatastore";
    static constexpr const char* kOwner="tjcNativeStandingsOwner";
    // The game's streamed TXD at hash 0x7709A245. This hash-equivalent name
    // and its headshot_* texture names are catalogued in rdr3_discoveries.
    static constexpr const char* kPortraitDict="l_016e22bcpp";
    static constexpr const char* kOverlayDict="menu_textures";
    static constexpr const char* kMonogramDict="blips";
    enum class Phase {Idle,Loading,Running,Exiting};

    Api api_{};
    Phase phase_=Phase::Idle;
    Handle root_=0,tag_=0,flow_=0;
    Handle timer_visible_=0,timer_message_=0;
    std::array<Handle,10> meter_visible_{},meter_value_{},
        meter_color_{},icon_txd_{},icon_txn_{},icon_color_{},
        overlay_visible_{},overlay_txd_{},overlay_txn_{},portrait_visible_{};
    bool native_portraits_=true;
    bool desired_=false,machine_owned_=false,hud_context_owned_=false,textures_requested_=false;
    int token_=0,exit_attempts_=0;
    double phase_since_=0,next_exit_attempt_=0,retry_after_=0;
    std::string title_;
    std::array<std::string,10> portraits_{},portrait_dicts_{};
    std::array<float,10> fractions_{};
    std::array<bool,10> out_{};
    std::size_t count_=0;
    bool content_visible_=false;
    std::string written_title_;
    std::array<std::string,10> written_portraits_{},written_portrait_dicts_{};
    std::array<std::string,10> written_overlay_txd_{},written_overlay_txn_{};
    std::array<float,10> written_fractions_{};
    std::array<bool,10> written_out_{};
    std::array<bool,10> written_visible_{},written_overlay_visible_{},written_portrait_visible_{};
    std::string status_="idle";
    inline static unsigned serial_=0;

    bool OwnsRoot() const{
        return RootStillCurrent() && api_.Valid(tag_) &&
            api_.ReadInt(tag_)==token_;
    }
    bool RootStillCurrent() const{
        return api_.Valid(root_) && api_.FindRoot(kRoot)==root_;
    }
    void Forget(){
        if(textures_requested_){
            api_.ReleaseTexture(kPortraitDict);api_.ReleaseTexture(kOverlayDict);
            api_.ReleaseTexture(kMonogramDict);
            textures_requested_=false;
        }
        root_=tag_=flow_=timer_visible_=timer_message_=0;
        meter_visible_.fill(0);meter_value_.fill(0);
        meter_color_.fill(0);icon_txd_.fill(0);icon_txn_.fill(0);
        icon_color_.fill(0);overlay_visible_.fill(0);
        overlay_txd_.fill(0);overlay_txn_.fill(0);
        portrait_visible_.fill(0);written_portrait_visible_.fill(false);
        content_visible_=false;written_title_.clear();
        written_portraits_.fill({});written_portrait_dicts_.fill({});
        written_overlay_txd_.fill({});written_overlay_txn_.fill({});
        written_fractions_.fill(0);written_out_.fill(false);written_visible_.fill(false);
        written_overlay_visible_.fill(false);
        machine_owned_=hud_context_owned_=false;exit_attempts_=0;phase_=Phase::Idle;
    }
    void AbandonForeign(){
        // Another owner now has the global datastore or UI state machine.
        // Never remove its bindings or alter its HUD context.
        if(flow_ && !machine_owned_)api_.ReleaseFlow(flow_);
        Forget();status_="foreign MP HUD ownership";
    }
    bool BuildBindings(){
        const auto add_bool=[&](Handle parent,const char* key,bool value){
            const Handle entry=api_.AddBool(parent,key,value);return api_.Valid(entry)?entry:Handle{};
        };
        const auto add_string=[&](Handle parent,const char* key,const char* value){
            const Handle entry=api_.AddString(parent,key,value);return api_.Valid(entry)?entry:Handle{};
        };
        const auto add_hash=[&](Handle parent,const char* key,const char* value){
            const Handle entry=api_.AddHash(parent,key,value);return api_.Valid(entry)?entry:Handle{};
        };
        const auto add_float=[&](Handle parent,const char* key,float value){
            const Handle entry=api_.AddFloat(parent,key,value);return api_.Valid(entry)?entry:Handle{};
        };
        const auto add_int=[&](Handle parent,const char* key,int value){
            return api_.Valid(api_.AddInt(parent,key,value));
        };
        // Bind the first frame's artwork before creating the native state
        // machine. Some MP HUD artwork fields are only sampled on entry.
        timer_visible_=add_bool(root_,"isVisible",desired_);
        const bool timer_ok=api_.Valid(timer_visible_) &&
            api_.Valid(add_string(root_,"timerString","")) &&
            api_.Valid(add_bool(root_,"isTimerLow",false));
        timer_message_=add_string(root_,"timerMessageString",title_.c_str());
        if(!timer_ok || !api_.Valid(timer_message_) ||
            !api_.Valid(add_bool(root_,"isTimerMessageLow",false)))return false;

        const Handle score=api_.AddContainer(root_,"score");
        if(!api_.Valid(score) || !api_.Valid(add_bool(score,"visible",false)) ||
            !api_.Valid(add_string(score,"leftScore","")) ||
            !api_.Valid(add_hash(score,"leftScoreColor","COLOR_BLUE")) ||
            !api_.Valid(add_hash(score,"leftScoreTextColor","COLOR_BLACK")) ||
            !api_.Valid(add_string(score,"rightScore","")) ||
            !api_.Valid(add_hash(score,"rightScoreColor","COLOR_RED")) ||
            !api_.Valid(add_hash(score,"rightScoreTextColor","COLOR_BLACK")))return false;
        const Handle fisting=api_.AddContainer(root_,"fistingMeter");
        if(!api_.Valid(fisting) || !api_.Valid(add_bool(fisting,"isVisible",false)) ||
            !add_int(fisting,"setLeftMeter",0) ||
            !api_.Valid(add_hash(fisting,"setLeftMeterColor","COLOR_BLUE")) ||
            !add_int(fisting,"setRightMeter",0) ||
            !api_.Valid(add_hash(fisting,"setRightMeterColor","COLOR_RED")))return false;
        const Handle countdown=api_.AddContainer(root_,"countDownTimer");
        const Handle wrong_way=api_.AddContainer(root_,"wrongWay");
        if(!api_.Valid(countdown) || !api_.Valid(wrong_way) ||
            !api_.Valid(add_bool(countdown,"isVisible",false)) ||
            !api_.Valid(add_bool(wrong_way,"isVisible",false)))return false;

        for(std::size_t i=0;i<16;++i){
            const bool meter=i<10;
            const std::string name=std::string(meter?"meter":"dynamicIcon")+
                std::to_string(meter?i:i-10);
            const Handle item=api_.AddContainer(root_,name.c_str());
            if(!api_.Valid(item))return false;
            const bool first_frame_visible=meter && i<count_;
            const Handle visible=add_bool(item,"visible",first_frame_visible);
            if(meter)meter_visible_[i]=visible;
            if(meter){
                portrait_visible_[i]=add_bool(item,"portraitVisible",native_portraits_ && first_frame_visible);
                if(!api_.Valid(portrait_visible_[i]))return false;
            }
            if(!api_.Valid(visible) ||
                !api_.Valid(add_bool(item,"enabled",true)) ||
                !api_.Valid(add_bool(item,"iconEnabled",true)) ||
                !api_.Valid(add_bool(item,"isIconBackgroundVisible",false)) ||
                !api_.Valid(add_bool(item,"showBlinkIcon",false)) ||
                !api_.Valid(add_bool(item,"showPulse",false)))return false;
            if(meter && (!api_.Valid(add_bool(item,"meterVisible",true)) ||
                !api_.Valid(meter_value_[i]=add_float(item,"meterValue",fractions_[i])) ||
                !api_.Valid(meter_color_[i]=add_hash(item,"meterColor",
                    out_[i]?"COLOR_GREY":"COLOR_PURE_WHITE")) ||
                !api_.Valid(add_bool(item,"meterThirdsVisible",false))))return false;
            // Original fme_archery::func_1039 explicitly creates the primary
            // TXD/texture AND color fields as HASH bindings (func_1181), and
            // enables the icon. String colors are not the native schema.
            // These fields are inherited by the inlined native image. Their
            // successful creation is NOT proof that LML loaded the resource.
            const Handle txd=add_hash(item,"txd",meter && first_frame_visible?
                portrait_dicts_[i].c_str():"");
            const Handle txn=add_hash(item,"txn",meter && first_frame_visible?
                portraits_[i].c_str():"");
            const Handle color=add_hash(item,"imgColor",meter && out_[i]?
                "COLOR_GREY":"COLOR_PURE_WHITE");
            if(meter){icon_txd_[i]=txd;icon_txn_[i]=txn;icon_color_[i]=color;}
            if(!api_.Valid(txd) || !api_.Valid(txn) || !api_.Valid(color) ||
                !api_.Valid(add_bool(item,"showAlternateIcons",false)) ||
                !api_.Valid(add_hash(item,"secondaryTxd","")) ||
                !api_.Valid(add_hash(item,"secondaryTxn","")) ||
                !api_.Valid(add_hash(item,"secondaryImgColor","COLOR_PURE_WHITE")) ||
                !api_.Valid(meter?(overlay_visible_[i]=add_bool(item,"overlayVisible",out_[i])):
                    add_bool(item,"overlayVisible",false)) ||
                !api_.Valid(meter?(overlay_txd_[i]=add_string(item,"overlayTxd",
                    kOverlayDict)):
                    add_string(item,"overlayTxd","")) ||
                !api_.Valid(meter?(overlay_txn_[i]=add_string(item,"overlayTxn",
                    "cross")):
                    add_string(item,"overlayTxn","")) ||
                !api_.Valid(add_hash(item,"overlayColor","COLOR_PURE_WHITE")))return false;
            if(!api_.Valid(add_string(item,"text","")))return false;
        }
        return true;
    }
    void WriteContent(){
        if(!OwnsRoot())return;
        if(!content_visible_){api_.WriteBool(timer_visible_,true);content_visible_=true;}
        if(written_title_!=title_){api_.WriteString(timer_message_,title_.c_str());written_title_=title_;}
        for(std::size_t i=0;i<10;++i){
            const bool visible=i<count_;
            if(written_visible_[i]!=visible){
                api_.WriteBool(meter_visible_[i],visible);written_visible_[i]=visible;
            }
            const bool portrait_visible=native_portraits_ && visible;
            if(written_portrait_visible_[i]!=portrait_visible){
                api_.WriteBool(portrait_visible_[i],portrait_visible);
                written_portrait_visible_[i]=portrait_visible;
            }
            if(!visible)continue;
            if(written_portraits_[i]!=portraits_[i] ||
                written_portrait_dicts_[i]!=portrait_dicts_[i]){
                api_.WriteHash(icon_txd_[i],portraits_[i].empty()?"":portrait_dicts_[i].c_str());
                api_.WriteHash(icon_txn_[i],portraits_[i].c_str());
                written_portraits_[i]=portraits_[i];
                written_portrait_dicts_[i]=portrait_dicts_[i];
            }
            if(written_out_[i]!=out_[i]){
                const char* color=out_[i]?"COLOR_GREY":"COLOR_PURE_WHITE";
                api_.WriteHash(icon_color_[i],color);
                api_.WriteHash(meter_color_[i],color);
                written_out_[i]=out_[i];
            }
            const bool overlay_visible=out_[i];
            const std::string overlay_txd=kOverlayDict;
            const std::string overlay_txn="cross";
            if(written_overlay_visible_[i]!=overlay_visible){
                api_.WriteBool(overlay_visible_[i],overlay_visible);
                written_overlay_visible_[i]=overlay_visible;
            }
            if(written_overlay_txd_[i]!=overlay_txd){
                api_.WriteString(overlay_txd_[i],overlay_txd.c_str());
                written_overlay_txd_[i]=overlay_txd;
            }
            if(written_overlay_txn_[i]!=overlay_txn){
                api_.WriteString(overlay_txn_[i],overlay_txn.c_str());
                written_overlay_txn_[i]=overlay_txn;
            }
            if(std::abs(written_fractions_[i]-fractions_[i])>=.002F){
                api_.WriteFloat(meter_value_[i],fractions_[i]);
                written_fractions_[i]=fractions_[i];
            }
        }
    }
    void RefreshTextureStatus(){
        status_=api_.TextureLoaded(kPortraitDict) &&
            api_.TextureLoaded(kOverlayDict) && api_.TextureLoaded(kMonogramDict)?
            "running":"running; native portrait textures pending";
    }
    void HideContent(){
        if(!OwnsRoot())return;
        if(content_visible_){api_.WriteBool(timer_visible_,false);content_visible_=false;}
        for(std::size_t i=0;i<10;++i)if(written_visible_[i]){
            api_.WriteBool(meter_visible_[i],false);written_visible_[i]=false;
        }
    }
    void FinishOwned(){
        if(!OwnsRoot()){AbandonForeign();return;}
        if(machine_owned_ && api_.MachineExists(kStateMachine))api_.DestroyMachine(kStateMachine);
        if(hud_context_owned_)api_.DisableHudContext();
        api_.RemoveRoot(root_);
        if(flow_)api_.ReleaseFlow(flow_);
        Forget();status_="closed";
    }
    void BeginExit(double now){
        if(!OwnsRoot()){AbandonForeign();return;}
        HideContent();
        if(hud_context_owned_){api_.DisableHudContext();hud_context_owned_=false;}
        phase_=Phase::Exiting;phase_since_=now;
        exit_attempts_=0;next_exit_attempt_=now;
    }
public:
    explicit NativeStandingsHudT(Api api={}):api_(std::move(api)){}
    const char* LastStatus() const{return status_.c_str();}
    bool Active() const{return phase_==Phase::Running && OwnsRoot();}
    void NativePortraits(bool enabled){native_portraits_=enabled;}

    void Update(const char* title,std::span<const NativeStandingRow> rows,double now){
        title_=title?title:"";count_=std::min<std::size_t>(rows.size(),10);
        for(std::size_t i=0;i<count_;++i){
            portraits_[i]=rows[i].portrait?rows[i].portrait:"";
            portrait_dicts_[i]=rows[i].portrait_dict?rows[i].portrait_dict:"";
            fractions_[i]=std::isfinite(rows[i].fraction)?std::clamp(rows[i].fraction,0.0F,1.0F):0.0F;
            out_[i]=rows[i].out;
        }
        desired_=true;Tick(now);
    }
    void Hide(double now){desired_=false;Tick(now);}
    void Close(double now){desired_=false;title_.clear();count_=0;Tick(now);}

    void Tick(double now){
        if(!std::isfinite(now))return;
        if(phase_!=Phase::Idle && !OwnsRoot()){AbandonForeign();return;}
        if(phase_==Phase::Idle){
            if(!desired_)return;
            if(now<retry_after_)return;
            if(api_.Valid(api_.FindRoot(kRoot)) || api_.MachineExists(kStateMachine)){
                status_="native MP HUD already in use";retry_after_=now+1;return;
            }
            root_=api_.AddRoot(kRoot);
            if(!api_.Valid(root_)){
                Forget();status_="native MP datastore unavailable";retry_after_=now+1;return;
            }
            token_=static_cast<int>(1U+(++serial_%0x7FFFFFFFU));
            tag_=api_.AddInt(root_,kOwner,token_);
            if(!api_.Valid(tag_)){
                // The root was just created by us; a failed tag must not leave
                // an orphan that blocks every later attempt.
                if(RootStillCurrent())api_.RemoveRoot(root_);
                Forget();status_="native MP owner tag unavailable";retry_after_=now+1;return;
            }
            if(!OwnsRoot() || !BuildBindings()){
                if(OwnsRoot())api_.RemoveRoot(root_);
                Forget();status_="native MP bindings unavailable";retry_after_=now+1;return;
            }
            api_.RequestTexture(kPortraitDict);api_.RequestTexture(kOverlayDict);
            api_.RequestTexture(kMonogramDict);
            textures_requested_=true;
            flow_=api_.RequestFlow(kFlowBlock);
            if(!flow_){
                FinishOwned();status_="native MP flow unavailable";retry_after_=now+1;return;
            }
            phase_=Phase::Loading;phase_since_=now;status_="loading native MP HUD";
            return;
        }
        if(phase_==Phase::Loading){
            if(!desired_ || now-phase_since_>5){
                FinishOwned();status_=desired_?"native MP flow timed out":"closed";
                if(desired_)retry_after_=now+1;
                return;
            }
            if(!api_.FlowLoaded(flow_))return;
            // A meter icon may be resolved only when this flow is entered.
            // Creating the MP HUD before all original dictionaries are ready
            // leaves permanent blank circles even after streaming finishes.
            if(!api_.TextureLoaded(kPortraitDict) ||
                !api_.TextureLoaded(kOverlayDict) ||
                !api_.TextureLoaded(kMonogramDict)){
                status_="loading native portrait textures";return;
            }
            if(api_.MachineExists(kStateMachine)){
                FinishOwned();status_="native MP state machine occupied";retry_after_=now+1;return;
            }
            api_.EnterFlow(flow_,api_.StyleHash("cTimer_two_meters_plus_eight_meters"));
            api_.CreateMachine(kStateMachine,flow_);
            if(!api_.MachineExists(kStateMachine)){
                FinishOwned();status_="native MP state machine refused";retry_after_=now+1;return;
            }
            machine_owned_=true;
            api_.EnableHudContext();hud_context_owned_=true;
            phase_=Phase::Running;RefreshTextureStatus();
            WriteContent();return;
        }
        if(phase_==Phase::Running){
            if(!desired_){BeginExit(now);return;}
            if(!api_.MachineExists(kStateMachine)){
                // The engine/another script already removed the machine.
                machine_owned_=false;FinishOwned();status_="native MP state machine lost";return;
            }
            RefreshTextureStatus();WriteContent();return;
        }
        if(phase_==Phase::Exiting){
            if(!api_.MachineExists(kStateMachine) || api_.MachineExited(kStateMachine)){
                FinishOwned();return;
            }
            if(exit_attempts_<3 && now>=next_exit_attempt_){
                ++exit_attempts_;next_exit_attempt_=now+.35;
                if(api_.MachineCanExit(kStateMachine))api_.RequestExit(kStateMachine);
            }
            if(now-phase_since_>3 && exit_attempts_>=3)
                status_="native MP exit pending; owned bindings retained";
        }
    }
};

} // namespace horse_races::bookmaking
