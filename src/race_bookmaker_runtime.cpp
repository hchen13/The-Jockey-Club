#include "race_bookmaker_runtime.h"
#include "runtime_paths.h"
#include "bg_literal_text.h"
#include "race_progression_runtime.h"

#include "game_natives.h"
#include "race_appearance.h"
#include "race_bookmaker_field.h"
#include "race_bookmaker_exit.h"
#include "race_bookmaker_pursuit.h"
#include "race_bookmaker_camera.h"
#include "race_bookmaker_horse_info.h"
#include "race_bookmaker_spectator_prompts.h"
#include "race_bookmaker_grid.h"
#include "race_bookmaker_seeding.h"
#include "race_bookmaker_reception.h"
#include "race_bookmaker_results.h"
#include "race_bookmaker_blips.h"
#include "race_bookmaker_title.h"
#include "race_bookmaker_line.h"
#include "race_tracking_margin_config.h"
#include "race_bookmaker_obstacles.h"
#include "race_strawberry_door_geometry.h"
#include "race_bookmaker_map.h"
#include "race_bookmaker_menu.h"
#include "race_bookmaker_rule_description.h"
#include "race_bookmaker_native_shop.h"
#include "race_bookmaker_odds.h"
#include "race_bookmaker_start.h"
#include "race_bookmaker_venues.h"
#include "race_bookmaker_native_hud.h"
#include "race_bookmaker_portrait_layout.h"
#include "marybeth_clips.h"
#include "sadie_clips.h"
#include "charles_clips.h"
#include "gang_clips.h"
#include "tilly_clips.h"
#include "javier_clips.h"
#include "bill_clips.h"
#include "race_circuit_scene.h"
#include "race_countdown_hud.h"
#include "race_cash_receipt.h"
#include "race_far_catchup.h"
#include "race_far_handoff.h"
#include "race_speed_policy.h"
#include "race_native_far_probe.h"
#include "race_firearm_guard.h"
#include "race_host_profiles.h"
#include "race_host_horses.h"
#include "horse_catalog_runtime.h"
#include "race_localization.h"
#include "race_live_overtake_native.h"
#include "race_native_follower.h"
#include "race_following.h"
#include "race_horse_avoidance.h"
#include "race_collision_learner.h"
#include "race_passage_learner.h"
#include "race_obstacle_survey.h"
#include "race_opponent_difficulty.h"
#include "race_route_config.h"
#include "race_system.h"
#include "race_starter.h"
#include "race_transaction_feed.h"
#include "world/frame.h"
#include "world/identity.h"
#include "world/menu_time.h"
#include "world/camera_blackout.h"
#include "kernel/files.h"
#include "horse_inspection.h"
#include "horse_story_support.h"
#include "fixed_horse_pool.h"
#include <main.h>
#include <enums.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace horse_races::bookmaker_runtime {
namespace {
#include "race_bookmaker_runtime/state.inl"
#include "race_bookmaker_runtime/prompts.inl"
#include "race_bookmaker_runtime/catalog_presentation.inl"
#include "race_bookmaker_runtime/finish_reception.inl"
#include "race_bookmaker_runtime/spectator.inl"
#include "race_bookmaker_runtime/course_roster.inl"
#include "race_bookmaker_runtime/catalog_interaction.inl"
#include "race_bookmaker_runtime/settlement.inl"
#include "race_bookmaker_runtime/actors.inl"
#include "race_bookmaker_runtime/spectator_exit.inl"
#include "race_bookmaker_runtime/race_execution.inl"
#include "race_bookmaker_runtime/results.inl"
#include "race_bookmaker_runtime/reception.inl"
// Called only at the recorder's 100 ms sampling boundary. These read the
// bookmaker's own roster, never the previous hosted race's follower state.
template<class Telemetry>
void FillDriverTelemetry(Telemetry& t,const Jockey& j) {
    t.pace_braking=j.command.braking;t.pace_target_mps=j.command.target_mps;t.pace_effort=j.command.effort;
    t.terrain_limited=j.command.terrain_limited;
    t.following_limited=j.command.following_limited;t.following_horse=j.command.following_horse;
    t.following_gap_m=j.command.following_gap_m;
    t.driver_mode=j.follower.control?2:j.follower.starts>0?3:0;
    t.native_fallback_reason=static_cast<int>(j.follower.failure);
    t.native_waypoint_progress=j.follower.progress;t.native_entry_index=j.follower.entry;
    t.native_effort_readback=j.follower.override_readback;
    t.native_absolute_speed=j.follower.override_absolute;
    t.native_requested_mps=j.follower.override_absolute?j.follower.override_request:0;
    t.native_table=j.follower.speed_table.Observe();t.native_rate=j.follower.move_rate.observation;
    t.native_vector=j.follower.native_vector.Observe();
}
struct TelemetryPosition {float deviation=0,lateral=0,height_error=0;};
TelemetryPosition JockeyTelemetryPosition(const Jockey& j,float along) {
    TelemetryPosition result;
    const auto& path=plan.course.path;
    if(path.points.size()<2 || path.metres.size()!=path.points.size())return result;
    // One binary search and one segment, not a projection scan of all laps.
    const auto upper=std::upper_bound(path.metres.begin(),path.metres.end(),along);
    const auto index=std::min(upper==path.metres.begin()?std::size_t{0}:
        static_cast<std::size_t>(upper-path.metres.begin()-1),path.points.size()-2);
    const auto a=path.points[index],b=path.points[index+1];
    const auto p=ENTITY::GET_ENTITY_COORDS(j.horse,true,false);
    const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
    if(length2<=.0001F || !destination::Finite(p))return result;
    const float fraction=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length2,0.0F,1.0F);
    result.lateral=(dx*(p.y-a.y)-dy*(p.x-a.x))/std::sqrt(length2);
    result.height_error=p.z-(a.z+fraction*(b.z-a.z));
    result.deviation=std::hypot(std::hypot(p.x-a.x-fraction*dx,p.y-a.y-fraction*dy),
        std::max(0.F,std::abs(result.height_error)-6.F));
    return result;
}
} // namespace

void Initialize(const std::string& root,LogFn log){
    directory=root;paths=runtime_paths::Resolve(root);logger=log;phase=Phase::Idle;LoadOdds();
    markers.Reset();
    Log("native standings portrait composition: native inline resource trial; UI.NativeStandingsPortraits=0 restores legacy sprites live");
}
bool Active(){return phase!=Phase::Idle;}
namespace {
// JSON text here is produced only at recording boundaries, never per frame.
void RecordingString(std::ostream& out,std::string_view value){
    static constexpr char hex[]="0123456789abcdef";
    out<<'"';
    for(const unsigned char c:value){
        if(c=='"' || c=='\\')out<<'\\'<<static_cast<char>(c);
        else if(c<0x20)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else out<<static_cast<char>(c);
    }
    out<<'"';
}
}
std::uint64_t RecordingToken(){return token;}
std::string RecordingMetadata(){
    if(phase!=Phase::Racing || jockeys.empty())return {};
    std::ostringstream out;out<<std::boolalpha<<"{\"kind\":\"bookmaker\",\"venue\":";
    RecordingString(out,Venue().key);out<<",\"venue_name\":";RecordingString(out,Venue().title);
    out<<",\"venue_name_zh\":";RecordingString(out,Venue().chinese);
    const bool wagered=candidate_bet && ticket.Stake()>0;
    out<<",\"laps\":"<<plan.Total()<<",\"wagered\":"<<wagered
       <<",\"stake_cents\":"<<(wagered?ticket.Stake():0)<<",\"market\":";
    if(wagered)RecordingString(out,ticket.BetMarket()==bm::Market::Win?"win":"place");else out<<"null";
    out<<",\"odds_hundredths\":";if(wagered)out<<ticket.LockedNetOdds();else out<<"null";
    out<<",\"entrants\":[";
    for(std::size_t i=0;i<jockeys.size();++i){
        const auto& j=jockeys[i];if(i)out<<',';
        out<<"{\"index\":"<<i<<",\"key\":";RecordingString(out,j.profile?j.profile->key:"");
        out<<",\"name\":";RecordingString(out,j.profile?j.profile->name:"");
        out<<",\"name_zh\":";RecordingString(out,j.profile?j.profile->chinese:"");
        out<<",\"horse_model\":";RecordingString(out,j.horse_model);
        out<<",\"horse\":"<<j.horse<<",\"rider\":"<<j.rider<<",\"grid_slot\":"<<j.grid_slot
           <<",\"speed\":"<<(j.ranks.speed+1)<<",\"acceleration\":"<<(j.ranks.acceleration+1)
           <<",\"selected\":"<<(wagered && j.profile && j.profile->id==ticket.Selection())<<'}';
    }
    out<<"]}";return out.str();
}
std::string RecordingResult(){
    if((phase!=Phase::Results && phase!=Phase::ReturnFade) || !settle_requested || !scene_started || !token)return {};
    const bool wagered=candidate_bet && ticket.Stake()>0;
    std::ostringstream out;out<<std::boolalpha<<"{\"completed\":"<<(phase==Phase::Results || abort_reason=="spectator left final results")
        <<",\"finish_code\":"<<static_cast<int>(pending_finish)<<",\"selected_place\":";
    if(wagered && pending_result_place>0)out<<pending_result_place;else out<<"null";
    out<<",\"settlement_credit_cents\":";
    if(wagered)out<<pending_result_credit;else out<<"null";
    out<<",\"credit_state\":";
    if(!wagered)out<<"null";
    else if(ticket.Phase()==bm::Ticket::State::CreditPending)RecordingString(out,"pending");
    else if(ticket.Phase()==bm::Ticket::State::Closed)RecordingString(out,pending_result_credit>0?"credited":"none_due");
    else RecordingString(out,"unresolved");
    out<<",\"reason\":";
    if(abort_reason.empty())out<<"null";else RecordingString(out,abort_reason);
    out<<",\"entrants\":[";
    const auto& riders=field.Riders();
    for(std::size_t i=0;i<jockeys.size() && i<riders.size();++i){
        const auto& r=riders[i];const auto& j=jockeys[i];if(i)out<<',';
        out<<"{\"index\":"<<i<<",\"status\":";
        RecordingString(out,r.status==bm::Field::Status::Finished?"finished":r.status==bm::Field::Status::Out?"out":"racing");
        out<<",\"place\":"<<r.place<<",\"finish_seconds\":";
        if(r.finish_at>=go_at && std::isfinite(r.finish_at))out<<(r.finish_at-go_at);else out<<"null";
        out<<",\"out_reason\":";if(j.recording_out_reason.empty())out<<"null";else RecordingString(out,j.recording_out_reason);
        out<<",\"survival_win\":"<<r.survival_win<<'}';
    }
    out<<"]}";return out.str();
}
void SetMotionBoostState(int horse,bool earned,bool awake){
    for(auto& j:jockeys)if(j.horse==horse){
        j.burst_earned=earned&&phase==Phase::Racing;
        if(awake)j.follower.native_vector.Release();
        return;
    }
}
#include "race_bookmaker_runtime/motion_context.inl"
NavigationTelemetry MainNavigationTelemetry(){
    NavigationTelemetry t;
    if(phase!=Phase::Racing || jockeys.empty() || field.Riders().empty() || !plan.course.Ready())return t;
    const auto& j=jockeys.front();const auto& state=field.Riders().front();
    if(!Same(j.horse_id) || !Same(j.rider_id) || state.progress.total_laps<=0)return t;
    t.valid=true;t.recorded=true;t.circuit=true;
    FillDriverTelemetry(t,j);
    const double now=Now();const float along=state.progress.Metres(plan.course);
    t.driving=drive_diagnostics::Capture(j.command,j.follower,j.navigation,j.avoidance.path,
        j.avoidance.reference,now,plan.course.strict_half_width);
    const auto position=JockeyTelemetryPosition(j,along);
    t.npc_along_m=along;t.npc_continuity_lost=state.progress.continuity_lost;
    t.npc_deviation_m=position.deviation;t.npc_line_offset_m=position.lateral;
    t.terrain_height_error_m=position.height_error;t.course_length_m=plan.course.path.Length();
    t.line_max_offset_m=j.personal_line.max_offset;
    t.line_planned_offset_m=PlannedOffsetTelemetry(j.personal_line.reference,j.personal_line.offsets,along);
    // The exhibition has no traffic lookahead/host reference-speed provider.
    // JSON Number emits null for unknown values instead of asserting zero.
    t.reference_speed_mps=t.nominal_lookahead_m=t.advance_threshold_m=std::numeric_limits<float>::quiet_NaN();
    t.node_count=static_cast<int>(j.avoidance.path.points.size());
    t.command_serial=static_cast<std::uint64_t>(j.follower.starts);
    t.command_age_s=j.navigation.issued_at>=0?std::max(0.0,now-j.navigation.issued_at):0;
    t.route_retries=j.retries.total;t.nav_speed_mps=ENTITY::GET_ENTITY_SPEED(j.horse);
    t.total_laps=plan.Total();t.npc_lap=std::min(plan.Total(),plan.Completed(state.progress)+1);
    t.npc_next_gate=static_cast<int>(state.progress.next_gate);
    if(state.progress.next_gate<plan.course.gates.size())
        t.next_gate_along_m=plan.course.gates[state.progress.next_gate].along;
    t.creeping=state.status==bm::Field::Status::Racing && j.navigation.Creeping(now);t.progress_anchor_m=j.navigation.anchor;
    t.progress_age_s=j.navigation.progress_at>=0?std::max(0.0,now-j.navigation.progress_at):0;
    if(j.follower.control && j.follower.progress>=0 && !j.avoidance.path.points.empty()){
        t.target=std::min(j.follower.progress+1,static_cast<int>(j.avoidance.path.points.size())-1);
        const auto p=j.avoidance.path.points[static_cast<std::size_t>(t.target)];t.goal={p.x,p.y,p.z};
        if(static_cast<std::size_t>(t.target)<j.avoidance.reference.size())
            t.remaining_to_target_m=j.avoidance.reference[static_cast<std::size_t>(t.target)]-along;
        if(t.target>0 && static_cast<std::size_t>(t.target+1)<j.avoidance.path.points.size()){
            const auto a=j.avoidance.path.points[static_cast<std::size_t>(t.target-1)];
            const auto c=j.avoidance.path.points[static_cast<std::size_t>(t.target+1)];
            const float product=destination::Distance(a,p)*destination::Distance(p,c);
            if(product>.0001F)t.target_adjacent_cos=std::clamp(((p.x-a.x)*(c.x-p.x)+(p.y-a.y)*(c.y-p.y))/product,-1.F,1.F);
        }
    }
    // No player competes in an exhibition. Player lap/gate/progress fields
    // retain their empty defaults rather than borrowing a spectator's mount.
    return t;
}
std::vector<EntrantTelemetry> ExtraEntrantTelemetry(){
    std::vector<EntrantTelemetry> result;
    if(phase!=Phase::Racing || !plan.course.Ready())return result;
    result.reserve(jockeys.size()>0?jockeys.size()-1:0);
    const double now=Now();
    for(std::size_t i=1;i<jockeys.size() && i<field.Riders().size();++i){
        const auto& j=jockeys[i];const auto& state=field.Riders()[i];
        if(!Same(j.horse_id) || !Same(j.rider_id))continue;
        EntrantTelemetry t;t.index=static_cast<int>(i);t.horse=j.horse;t.rider=j.rider;
        t.out=state.status==bm::Field::Status::Out;
        FillDriverTelemetry(t,j);t.follower_control=j.follower.control;
        t.driving=drive_diagnostics::Capture(j.command,j.follower,j.navigation,j.avoidance.path,
            j.avoidance.reference,now,plan.course.strict_half_width);
        t.along_m=state.progress.Metres(plan.course);t.next_gate=static_cast<int>(state.progress.next_gate);
        const auto position=JockeyTelemetryPosition(j,t.along_m);
        t.route_deviation_m=position.deviation;t.line_offset_m=position.lateral;
        t.terrain_height_error_m=position.height_error;t.line_max_offset_m=j.personal_line.max_offset;
        t.restarts=j.retries.total;
        t.stalled_s=state.status==bm::Field::Status::Racing?j.below_walking.Seconds(now):-1;
        t.raw_target={j.ranks.speed,j.ranks.acceleration};
        t.line_aggression=j.line_utilization;
        result.push_back(t);
    }
    return result;
}
void Tick(){
    TickFollowHorseInfo(); // Drain the owned card even during results, return and idle.
    // Continue asynchronous HUD exit in every phase, including idle/return.
    // The multiplayer context must never survive into another game activity.
    const bool network_session=NETWORK::NETWORK_IS_SESSION_ACTIVE();
    if(phase!=Phase::Racing || network_session)native_standings.Hide(Now());
    if(native_standings_status!=native_standings.LastStatus()){
        native_standings_status=native_standings.LastStatus();
        Log("native standings HUD: "+native_standings_status);
    }
    if(return_release_pending && horse_races::BookmakerReturnIsVisible()){
        UnlockPlayer();return_release_pending=false;
        Log("return ready; player released after fade-in");
    }
    if(catalog.Closing())catalog.Tick();
    scene.Background(Now());FlushCredit();DrawNotice();
    if(network_session){markers.Reset();return;}
    // Venue icons outlive clerks and races. Maintain their assets during host
    // duels and bookmaker menus/results too, before phase-specific returns.
    TickMarkers();
    // Results/cinematic contexts hide the native transaction feed and cash.
    // Keep the actual payment immediate; post only its receipt once the player
    // is back, the fade is finished and no stock menu owns the HUD.
    const bool cash_hud_ready=cash_receipts.NeedsHud(Now()) && phase==Phase::Idle && !return_release_pending && !return_fade_owned &&
        !catalog.Active() && !catalog.Closing() && Foreground() && PlayerOkay() &&
        horse_races::BookmakerReturnIsVisible() && !HUD::IS_PAUSE_MENU_ACTIVE() && !UIAPPS::IS_ANY_UIAPP_ACTIVE();
    if(const auto posted=cash_receipts.Tick(Now(),cash_hud_ready))
        Log("native cash receipt posted cents="+std::to_string(posted->cents)+" handle="+std::to_string(posted->handle));
    if(phase==Phase::Racing || phase==Phase::Results)cues::CalmRearedHorses();
    if(race_starter.fired && Now()-go_at>4 && race_starter.Retire())race_starter.Release();
    if(phase==Phase::Idle){TickClerk();return;}
    if(phase==Phase::Menu || phase==Phase::Loading || phase==Phase::Preparing || phase==Phase::Countdown)
        TickObstacleProfiles(false);
    else if(phase==Phase::Results || phase==Phase::ReturnFade)TickObstacleProfiles(true);
    if(phase==Phase::Menu){MenuInputTick();return;}
    if(phase==Phase::ReturnFade){
        if(!horse_races::BookmakerReturnIsBlack()){
            if(Now()-return_at>5){Log("return fade timeout");horse_races::BeginBookmakerReturnFade();return_at=Now();}
            return;
        }
        if(!return_prepared){
            scene.Finish(Now());DropActors();
            if(PlayerOkay() && player_at_accept==PLAYER::PLAYER_PED_ID()){
                LockPlayer();
                STREAMING::REQUEST_COLLISION_AT_COORD(return_point);
                ENTITY::SET_ENTITY_VELOCITY(player_at_accept,0,0,0);
                // The captured location is the ped origin. SET_ENTITY_COORDS
                // adds a bounds offset and produced the visible falling pose.
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player_at_accept,return_point,false,false,false);
                ENTITY::SET_ENTITY_HEADING(player_at_accept,return_heading);
            }
            return_prepared=true;return_prepared_at=Now();
            Log("return blackout: actors removed and player placed");return;
        }
        bool player_ready=true,horse_ready=true;
        if(PlayerOkay() && player_at_accept==PLAYER::PLAYER_PED_ID()){
            STREAMING::REQUEST_COLLISION_AT_COORD(return_point);
            player_ready=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(player_at_accept) &&
                !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(player_at_accept);
            if(player_ready){
                ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(player_at_accept,false);
                ENTITY::SET_ENTITY_VELOCITY(player_at_accept,0,0,0);
                ENTITY::SET_ENTITY_HEADING(player_at_accept,return_heading);
            }
        }
        if(Same(return_horse)){
            const auto spot=ENTITY::GET_ENTITY_COORDS(return_horse.horse,true,false);
            STREAMING::REQUEST_COLLISION_AT_COORD(spot);
            horse_ready=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(return_horse.horse) &&
                !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(return_horse.horse) &&
                PED::IS_PED_READY_TO_RENDER(return_horse.horse);
        }
        if((!player_ready || !horse_ready) && Now()-return_prepared_at<7.0)return;
        if(!player_ready || !horse_ready)Log("return readiness timeout player="+
            std::to_string(player_ready)+" horse="+std::to_string(horse_ready));
        Log("exhibition ended: "+abort_reason);
        return_release_pending=bool(PlayerOkay() && spectator_lock);
        ResetRace(return_release_pending);
        horse_races::EndBookmakerReturnFade();return_fade_owned=false;
        return;
    }
    if(!PlayerOkay() || PLAYER::PLAYER_PED_ID()!=player_at_accept){
        ReturnFromRace(bm::Finish::AccidentalVoid,0,"player unavailable");return;
    }
    if((phase==Phase::Loading || phase==Phase::Preparing || phase==Phase::Countdown) && TickLeaveRace())return;
    if(phase==Phase::Loading){
        if(!ClaimMission()){ReturnFromRace(bm::Finish::AccidentalVoid,0,"other mission active");return;}
        RequestModels();
        if(!ModelsReady()){
            if(Now()-entered_at>kLoadTimeout)ReturnFromRace(bm::Finish::AccidentalVoid,0,"model timeout");
            return;
        }
        if(!scene_started){
            scene.RememberPlayerMount(PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID()));
            // The clearance engine requires a nonempty initial roster. Clay
            // and the player are real, owned/verified by this interaction;
            // the ten mounted pairs join only under the scene's blackout.
            if(!Same(clerk_id) || !scene.Begin(definition,plan,{player_at_accept,clerk},Now(),Log,true)){
                ReturnFromRace(bm::Finish::AccidentalVoid,0,"scene refused "+scene.error);return;
            }
            scene_started=true;
        }
        phase=Phase::Preparing;
    }
    if(phase==Phase::Preparing){
        const auto result=scene.Prepare(Now(),RevealActors,StageActors);
        if(result==circuit_scene::Preparation::Failed){
            ReturnFromRace(bm::Finish::AccidentalVoid,0,"scene failed "+scene.error);return;
        }
        if(result==circuit_scene::Preparation::Ready){
            std::vector<Ped> mounts;for(const auto& j:jockeys)mounts.push_back(j.horse);
            countdown.Begin(mounts);phase=Phase::Countdown;
            Log("exhibition countdown ready mounted_racers="+std::to_string(mounts.size())+
                " starter="+std::to_string(race_starter.Alive()));
        }
    }
    if(phase==Phase::Countdown){
        LockPlayer();
        const bool paused=HUD::IS_PAUSE_MENU_ACTIVE() || !Foreground();
        const bool visible=horse_races::BookmakerReturnIsVisible();
        const auto step=countdown.Tick(MISC::GET_FRAME_TIME(),visible,paused);
        if(step.release)LaunchRace();
        else if(visible && !paused){race_starter.Aim();countdown_hud::Draw(step.digit,step.elapsed);}
    }
    if(phase==Phase::Racing){
        countdown.DrawGo(MISC::GET_FRAME_TIME(),horse_races::BookmakerReturnIsVisible(),
            HUD::IS_PAUSE_MENU_ACTIVE() || !Foreground());
        LockPlayer();AdvanceRace();
        // Adjudicate this frame first: crossing the finish and leaving on the
        // same tick must preserve the earned result. Never drive after exit.
        if(phase!=Phase::Racing){if(phase==Phase::Results)TickResults();return;}
        if(TickLeaveRace())return;
        TickNativeSpectatorCamera();TickBurstVoices();
        const bool follow_requested=!camera_director.automatic &&
            (PAD::IS_CONTROL_PRESSED(0,INPUT_AIM)||PAD::IS_DISABLED_CONTROL_PRESSED(0,INPUT_AIM));
        if(spectator_hud.ShouldShowRankings(camera_director.automatic,follow_requested,rider_horse_info.Busy()))
            DrawStandings();
        else native_standings.Hide(Now());
    }
    if(phase==Phase::Results)TickResults();
}
void BurstChanged(std::size_t index,bool active,bool announce_start){
    if(index>=jockeys.size())return;
    auto& j=jockeys[index];
    if(phase!=Phase::Racing){
        j.burst_active=j.burst_pending=false;j.burst_expires=0;return;
    }
    if(j.burst_active==active)return;
    j.burst_active=active;
    j.burst_pending=active && announce_start;
    j.burst_expires=j.burst_pending?Now()+bm::kBurstDurationSeconds:0;
    j.dot.Update(Same(j.rider_id)?j.rider:0,
        ticket.Stake()>0 && j.profile->id==ticket.Selection(),active,
        tr::Text(j.profile->name,j.profile->chinese));
}
} // namespace horse_races::bookmaker_runtime
