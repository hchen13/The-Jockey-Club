#pragma once
#include "race_destination.h"
#include "race_localization.h"
#include "race_countdown_hud.h"
#include "race_cues.h"
#include "race_circuit.h"
#include "race_breeds.h"
#include "game_natives.h"
#include <main.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <optional>
#include <functional>

namespace horse_races::circuit_start {
struct Grid {std::vector<Vector3> slots=std::vector<Vector3>(2);float heading=0;};
// Slot 0 is the player, slot 1 the host, the rest the field.
// Four or more entrants start in rows three abreast, the host front and
// centre. The player is ALWAYS in the REARMOST row - a deliberate handicap the
// user asked for, not a draw. Lateral spacing is wider than the old single
// line, and a row of two sits in the gaps of the row ahead, so the field is
// not shoulder to shoulder when the countdown releases them.
//
// Up to nine now start together, in three rows. Four to six entrants lay out
// exactly as they did when two rows were the maximum, so the Blackwater grid
// does not shift because New Austin wanted a bigger field.
inline constexpr float kGridSide=3.4F,kGridRow=4.2F;
// How long an unsafe ground reading under a slot is retried before the race
// is given up: collision streams in behind the fade, and the ground can read
// wrong for a moment (Pearson's Van Horn grid, 2026-09-16, had started seven
// races that night before one read unsafe on its first frame).
inline constexpr double kUnsafeGraceSeconds=2;
inline constexpr std::size_t kGridWidth=3,kMaxGridEntrants=9;
static_assert(kMaxGridEntrants==static_cast<std::size_t>(field_race::kMaxFieldRivals)+2,
    "the grid must seat the host, the largest field and the player");
// `width` is how many stand abreast: 0 keeps the shipped three, 2 makes the
// deeper grid a two-by-four race wants.
inline std::optional<Grid> GroupGrid(Vector3 centre,float heading,std::size_t count,int width,std::size_t rotation) {
    if(!destination::Finite(centre) || !std::isfinite(heading) || count<2 || count>kMaxGridEntrants)return {};
    const std::size_t abreast=width==2?2u:kGridWidth;
    Grid grid;grid.heading=heading;grid.slots.resize(count);
    const float radians=heading*.01745329252F;
    const Vector3 forward{-std::sin(radians),std::cos(radians),0};
    const auto place=[&](std::size_t index,float side,float back) {
        grid.slots[index]={centre.x-forward.y*side-forward.x*back,
            centre.y+forward.x*side-forward.y*back,centre.z};
    };
    if(count<4) { // Two-horse destination races keep their equal-progress line.
        for(std::size_t i=0;i<count;++i)
            place(i,(static_cast<float>((i+rotation)%count)-static_cast<float>(count-1)*.5F)*2.2F,0);
        return grid;
    }
    std::vector<std::size_t> field; // Everyone who is neither player nor host.
    for(std::size_t i=2;i<count;++i)field.push_back(i);
    const std::size_t turn=rotation%field.size();
    std::rotate(field.begin(),field.begin()+static_cast<std::ptrdiff_t>(turn),field.end());
    std::size_t next=2;
    if(abreast==2) {
        // Two abreast: the host on the left of the front row, one of the field
        // beside him, and the player in the rearmost row as always.
        place(1,-kGridSide*.5F,0);
        place(field[0],kGridSide*.5F,0);
        next=1;
    } else {
        place(1,0,0); // Host, front and centre.
        place(field[0],-kGridSide,0);
        place(field[1],kGridSide,0);
    }
    // Rows behind the front, three abreast until whatever is left over. With
    // six or fewer this is the single back row it has always been.
    std::vector<std::size_t> rows;
    for(std::size_t left=count-abreast;left>0;) {
        const std::size_t row=std::min(left,abreast);rows.push_back(row);left-=row;
    }
    const std::size_t player_slot=rotation%rows.back();
    for(std::size_t row=0;row<rows.size();++row) {
        const bool last_row=row+1==rows.size();
        for(std::size_t slot=0;slot<rows[row];++slot) {
            const float side=rows[row]==2?(slot==0?-kGridSide*.5F:kGridSide*.5F):
                (static_cast<float>(slot)-1)*kGridSide;
            place(last_row && slot==player_slot?0:field[next++],side,static_cast<float>(row+1)*kGridRow);
        }
    }
    return grid;
}
// Equal longitudinal progress, not two horses on the same coordinate. Random
// left/right assignment avoids always gifting the inside of the first turn.
inline std::optional<Grid> MakeGrid(Vector3 start,Vector3 next,bool swap) {
    const float dx=next.x-start.x,dy=next.y-start.y,len=std::hypot(dx,dy);
    if(!destination::Finite(start) || !destination::Finite(next) || len<4)return {};
    Grid grid;grid.heading=std::atan2(-dx,dy)*57.2957795F;
    const float along=std::min(4.0F,len*.4F),side=2.2F;
    for(int i=0;i<2;++i) {
        const float sign=(i==0?-1.0F:1.0F)*(swap?-1.0F:1.0F);
        grid.slots[i]={start.x+dx/len*along-dy/len*side*sign,
            start.y+dy/len*along+dx/len*side*sign,start.z+(next.z-start.z)*along/len};
    }
    return grid;
}
inline Grid RoadGrid(Vector3 centre,Vector3 forward,bool swap,float spacing) {
    Grid grid;grid.heading=std::atan2(-forward.x,forward.y)*57.2957795F;
    for(int i=0;i<2;++i) {
        const float side=(i==0?-1.0F:1.0F)*(swap?-1.0F:1.0F)*spacing;
        grid.slots[i]={centre.x-forward.y*side,centre.y+forward.x*side,centre.z};
    }
    return grid;
}
inline bool RoadGridWithin(const circuit::Layout& layout,const Grid& grid) {
    const auto a=layout.StrictProjection(grid.slots[0],0,layout.RoadStartLimit());
    const auto b=layout.StrictProjection(grid.slots[1],0,layout.RoadStartLimit());
    return a.valid && b.valid && a.along>=8 && b.along>=8 && std::abs(a.along-b.along)<=.75F;
}
struct Timer {
    double elapsed=0;bool visible=false;
    int Step(double dt,bool ready,bool paused) {
        if(!ready || paused)return std::max(1,3-static_cast<int>(elapsed));
        if(!visible){visible=true;return 3;} // Three FULL visible seconds.
        if(std::isfinite(dt))elapsed+=std::clamp(dt,0.0,.1);
        return elapsed>=3?0:3-static_cast<int>(elapsed);
    }
};
struct Identity {
    Ped ped=0;Hash model=0;BYTE* address=nullptr;bool frozen=false;float blend=0;
    bool Same() const {return ped && address && ENTITY::DOES_ENTITY_EXIST(ped) &&
        getScriptHandleBaseAddress(ped)==address && ENTITY::GET_ENTITY_MODEL(ped)==model;}
};
class Start {
    std::vector<Identity> horses_;
    Grid grid_{};Timer timer_{};
    bool active_=false,placed_=false,released_=false,held_=false,ready_=false;
    int last_digit_=-1;      // Last countdown second a cue was played for.
    double player_held_=0;   // Seconds the player's own controls stay disabled after GO.
    double since_go_=0;        // Seconds since the field was released.
    std::vector<double> rear_at_;   // Player: seconds after GO. -1 once done.
    std::vector<double> rear_lead_; // Rivals: seconds BEFORE GO. -1 once done.
    std::vector<int> rear_action_;  // Per rider, which verified combination it draws.
    std::vector<int> rear_variant_;
    // Countdown second (3/2/1) as it becomes visible, then 0 at the release.
    // The starter uses it to raise his revolver before the shot, so nothing
    // outside this class has to guess when the countdown actually began.
    std::function<void(int)> cue_;
    std::vector<bool> ground_placed_;
    std::vector<Vector3> previous_,anchors_;
    double placed_time_=0,stable_time_=0,next_ground_attempt_=0,next_report_=2;
    double unsafe_time_=0;bool unsafe_logged_=false;
    std::function<void(const std::string&)> log_;
    double go_left_=0;
    std::optional<circuit::Layout> road_;
    bool road_ready_=false,road_swap_=false;
    float road_along_=12;int road_width_=0;
    double road_time_=0;
    std::array<ScrHandle,2> road_probes_{};
    std::vector<ScrHandle> retired_road_probes_;
    std::array<bool,2> road_clear_{};
    Grid road_candidate_{};
    std::string road_rejection_;
    // Includes controller horse controls and the keyboard's general bindings.
    inline static constexpr std::array<const char*,7> kInputs{
        "INPUT_HORSE_MOVE_LR","INPUT_HORSE_MOVE_UD","INPUT_MOVE_LR","INPUT_MOVE_UD",
        "INPUT_MOVE_UP_ONLY","INPUT_HORSE_SPRINT","INPUT_SPRINT"};
    std::array<float,kInputs.size()> inputs_{};
    std::array<double,kInputs.size()> tap_age_{};
    static double FrameTime() {
        const float dt=MISC::GET_FRAME_TIME();
        return std::isfinite(dt)?std::clamp(static_cast<double>(dt),0.0,.1):0;
    }
#include "race_circuit_start_road_grid.inl"
    bool IdentitiesValid() {
        for(const auto& h:horses_)if(!h.Same() || ENTITY::IS_ENTITY_DEAD(h.ped)) {
            error="grid horse identity changed or horse died";return false;
        }
        return true;
    }
    void Report(const char* phase) const {
        if(!log_)return;
        for(std::size_t i=0;i<horses_.size();++i)if(horses_[i].Same()) {
            const auto p=ENTITY::GET_ENTITY_COORDS(horses_[i].ped,true,false);
            log_(std::string("circuit grid ")+phase+" side="+std::to_string(i)+
                " horse="+std::to_string(horses_[i].ped)+" ground_z="+std::to_string(grid_.slots[i].z)+
                " origin_z="+std::to_string(p.z)+" native_ground="+std::to_string(ground_placed_[i])+
                " height_above_ground="+std::to_string(ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(horses_[i].ped))+
                " speed="+std::to_string(ENTITY::GET_ENTITY_SPEED(horses_[i].ped))+
                " black_settle_s="+std::to_string(placed_time_)+" stable_s="+std::to_string(stable_time_));
        }
    }
    void Sample() {
        const double dt=FrameTime();
        for(std::size_t i=0;i<kInputs.size();++i) {
            const Hash action=MISC::GET_HASH_KEY(kInputs[i]);
            inputs_[i]=PAD::GET_DISABLED_CONTROL_NORMAL(0,action);
            if(!std::isfinite(inputs_[i]))inputs_[i]=0;
            inputs_[i]=std::clamp(inputs_[i],-1.0F,1.0F);
            tap_age_[i]=inputs_[i]>.2F?0:tap_age_[i]+dt;
        }
    }
    void Hold() {
        for(const char* action:kInputs)PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(action),true);
        for(const char* action:{"INPUT_HORSE_EXIT","INPUT_JUMP","INPUT_HORSE_JUMP","INPUT_ATTACK",
            "INPUT_AIM","INPUT_MELEE_ATTACK","INPUT_HORSE_MELEE_LEFT","INPUT_HORSE_MELEE_RIGHT"})
            PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(action),true);
        // Input-only per frame. Repeated freeze/velocity/ForceMotionState writes
        // fight the mounted task, ground contact and rider attachment animation.
    }
    void Unfreeze() {
        for(const auto& h:horses_)if(h.Same()) {
            if(held_)ENTITY::FREEZE_ENTITY_POSITION(h.ped,h.frozen);
            TASK::SET_PED_DESIRED_MOVE_BLEND_RATIO(h.ped,h.blend);
            // A horse released with its collision disabled runs on sleeping
            // physics: no force lands on it and it holds 54 km/h. Both horses
            // of the camp race did, for three minutes, on 2026-09-19 - the
            // flag was already set when they reached their slots. Put back
            // here, once, at the release.
            if(ENTITY::GET_ENTITY_COLLISION_DISABLED(h.ped)) {
                ENTITY::SET_ENTITY_COLLISION(h.ped,true,true);
                if(log_)log_("circuit grid release: horse "+std::to_string(h.ped)+" had its collision disabled; re-enabled");
            }
        }
        held_=false;
    }
public:
    std::string error;
    void Background() {
        std::erase_if(retired_road_probes_,[](ScrHandle handle) {
            BOOL hit=false;Vector3 end{},normal{};Entity entity=0;
            return SHAPETEST::GET_SHAPE_TEST_RESULT(handle,&hit,&end,&normal,&entity)!=1;
        });
    }
    bool Begin(Ped player_horse,Ped rival_horse,Vector3 first,Vector3 next,bool swap,
        std::function<void(const std::string&)> log={},const circuit::Layout* road_layout=nullptr) {
        const auto grid=MakeGrid(first,next,swap);
        return BeginGroup({player_horse,rival_horse},grid,std::move(log),road_layout,swap);
    }
    void SetCue(std::function<void(int)> cue){cue_=std::move(cue);}
    // True once a countdown second has actually been shown, which is the first
    // moment anything spoken can be heard rather than eaten by the fade.
    bool CountdownVisible() const {return last_digit_>0;}
    bool GridReady() const {return placed_;}
    const Grid& CurrentGrid() const {return grid_;}
    bool BeginGroup(const std::vector<Ped>& horses,const std::optional<Grid>& grid,
        std::function<void(const std::string&)> log={},const circuit::Layout* road_layout=nullptr,bool swap=false) {
        Close();Background();
        if(!grid){error="invalid circuit start tangent";return false;}grid_=*grid;
        log_=std::move(log);
        if(horses.size()<2 || horses.size()>kMaxGridEntrants || grid_.slots.size()!=horses.size()) {error="invalid starting roster";return false;}
        for(Ped p:horses) {
            if(!p || std::count(horses.begin(),horses.end(),p)!=1){error="duplicate starting horse";horses_.clear();return false;}
            horses_.push_back({p,ENTITY::GET_ENTITY_MODEL(p),getScriptHandleBaseAddress(p),
                ENTITY::_IS_ENTITY_FROZEN(p)!=0,TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(p)});
        }
        ground_placed_.assign(horses.size(),false);previous_.resize(horses.size());anchors_.resize(horses.size());
        for(const auto& h:horses_)if(!h.Same() || h.frozen || !std::isfinite(h.blend)) {
            error="start horse identity/freeze state unavailable";horses_={};return false;
        }
        if(road_layout) {
            if(horses.size()!=2 || retired_road_probes_.size()>4 || !road_layout->road_start || !road_layout->Ready() || road_layout->RoadStartLimit()<16) {
                error="invalid road starting section";horses_={};return false;
            }
            road_=*road_layout;road_swap_=swap;
        }
        tap_age_.fill(100);active_=true;return true;
    }
    bool AtPlacedGrid(Vector3 position) const {
        return placed_ && destination::Finite(position) && destination::Distance(position,grid_.slots[0])<=6 &&
            std::abs(position.z-grid_.slots[0].z)<=4;
    }
    // Called by the scene ONLY while black, before fade-in. No visible snapping.
#include "race_circuit_start_staging.inl"
    bool Tick(bool fully_visible) {
        if(!active_ || released_)return false;
        if(!IdentitiesValid())return false;
        if(ready_)for(std::size_t i=0;i<horses_.size();++i) {
            const auto p=ENTITY::GET_ENTITY_COORDS(horses_[i].ped,true,false);
            if(!ENTITY::_IS_ENTITY_FROZEN(horses_[i].ped) || !destination::Finite(p) ||
                std::hypot(p.x-anchors_[i].x,p.y-anchors_[i].y,p.z-anchors_[i].z)>.2F) {
                error="starting grid hold lost before GO";Report("hold-lost");return false;
            }
        }
        const bool paused=HUD::IS_PAUSE_MENU_ACTIVE();
        if(!paused)Sample();
        else {inputs_.fill(0);tap_age_.fill(100);} // Menu-time presses do not launch a horse on return.
        const bool visible=fully_visible && ready_;
        const int digit=timer_.Step(FrameTime(),visible,paused);
        if(!digit) {
            Report("release");
            Unfreeze();released_=true;active_=false;go_left_=.75;
            cues::Go();
            if(cue_)cue_(0);
            // Everyone rears as they are let go, but not in perfect unison:
            // each rider draws its own small delay. The player's mount uses the
            // other documented rear animation, so his is a different length
            // rather than the same one held artificially.
            const auto& settings=cues::Settings();
            since_go_=0;rear_at_.assign(horses_.size(),-1);
            rear_action_.assign(horses_.size(),settings.player_rear_action);
            rear_variant_.assign(horses_.size(),settings.player_rear_variant);
            // Rivals have already reared during the countdown; only the player
            // is scheduled here, on the instant it ends. Once: a second command
            // cut the first animation short and read as two head-tosses.
            if(settings.rear_enabled)rear_at_[0]=settings.player_rear_delay;
            player_held_=settings.player_rear_seconds;
            TickAfterGo(); // The player leads: his mount is already going up.
            // Held axes + a recent sprint press reach the ordinary horse controls
            // at the release boundary. No scripted speed/velocity boost or AI task.
            for(std::size_t i=0;i<kInputs.size();++i) {
                const float value=i>=5 && tap_age_[i]<=.75?std::max(1.0F,inputs_[i]):inputs_[i];
                if(value!=0)PAD::SET_CONTROL_VALUE_NEXT_FRAME(0,MISC::GET_HASH_KEY(kInputs[i]),value);
            }
            countdown_hud::Draw(0,3,go_left_);return true;
        }
        if(visible && !paused && digit!=last_digit_){last_digit_=digit;cues::Tick();if(cue_)cue_(digit);}
        if(visible && !paused)TickCountdownRears();
        Hold();if(visible && !paused)countdown_hud::Draw(digit,timer_.elapsed);return false;
    }
    // Rivals rear BEFORE the countdown ends, each on its own draw of up to
    // npc_rear_lead seconds early. Delaying them past GO had only handed the
    // player a second of free riding; starting them early keeps the release
    // itself simultaneous while still breaking up the unison.
    void TickCountdownRears() {
        const auto& settings=cues::Settings();
        if(!settings.rear_enabled || horses_.empty())return;
        if(rear_lead_.size()!=horses_.size()) {
            rear_lead_.assign(horses_.size(),-1);
            rear_action_.assign(horses_.size(),settings.player_rear_action);
            rear_variant_.assign(horses_.size(),settings.player_rear_variant);
            const int steps=static_cast<int>(settings.npc_rear_lead*100);
            for(std::size_t i=1;i<horses_.size();++i) {
                rear_lead_[i]=steps>0?MISC::GET_RANDOM_INT_IN_RANGE(0,steps+1)/100.0:0;
                if(settings.npc_rear_action_override) { // One action for the whole field.
                    rear_action_[i]=settings.npc_rear_action_override;
                    rear_variant_[i]=settings.npc_rear_variant_override;
                } else {
                    // Each draws its own of the verified combinations, so a row
                    // of rivals is not a row of synchronised puppets.
                    const auto& drawn=cues::kVerifiedRears[static_cast<std::size_t>(
                        std::clamp(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(cues::kVerifiedRears.size())),
                            0,static_cast<int>(cues::kVerifiedRears.size())-1))];
                    rear_action_[i]=drawn[0];rear_variant_[i]=drawn[1];
                }
            }
        }
        const double remaining=3.0-timer_.elapsed;
        for(std::size_t i=1;i<rear_lead_.size() && i<horses_.size();++i) {
            if(rear_lead_[i]<0 || remaining>rear_lead_[i])continue;
            cues::Rear(horses_[i].ped,rear_action_[i],rear_variant_[i]);
            rear_lead_[i]=-1;
        }
    }
    // Called every frame after GO: releases each staggered rear as its moment
    // arrives, and keeps the player's own controls disabled while his mount is
    // still up. He keeps the horse throughout; only the input is held.
    void TickAfterGo() {
        if(HUD::IS_PAUSE_MENU_ACTIVE())return;
        const auto& settings=cues::Settings();
        for(std::size_t i=0;i<rear_at_.size() && i<horses_.size();++i) {
            if(rear_at_[i]<0 || since_go_<rear_at_[i])continue;
            rear_at_[i]=-1;
            cues::Rear(horses_[i].ped,rear_action_[i],rear_variant_[i]);
        }
        since_go_+=FrameTime();
        if(player_held_<=0)return;
        for(const char* input:kInputs)PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(input),true);
        player_held_-=FrameTime();
    }
    void DrawGo() {
        if(go_left_<=0 || HUD::IS_PAUSE_MENU_ACTIVE())return;
        countdown_hud::Draw(0,3,go_left_);go_left_-=FrameTime();
    }
    void Close() {
        if(active_ && placed_)Unfreeze();
        for(auto handle:road_probes_)if(handle)retired_road_probes_.push_back(handle);
        horses_={};timer_={};inputs_.fill(0);tap_age_.fill(100);
        active_=placed_=released_=held_=ready_=false;ground_placed_={};previous_=anchors_={};
        placed_time_=stable_time_=next_ground_attempt_=0;next_report_=2;go_left_=0;error.clear();log_={};
        unsafe_time_=0;unsafe_logged_=false;
        last_digit_=-1;player_held_=0;since_go_=0;rear_at_.clear();rear_lead_.clear();rear_action_.clear();rear_variant_.clear();
        // cue_ deliberately survives Close(): the owner installs it once when
        // the race is accepted, and BeginGroup closes a previous grid first.
        road_.reset();road_ready_=road_swap_=false;road_along_=12;road_width_=0;road_time_=0;
        road_probes_={};road_clear_={};road_candidate_={};road_rejection_.clear();
        Background();
    }
};
}
