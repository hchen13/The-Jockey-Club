#include "horse_market_scene.h"
#include "game_natives.h"
#include "race_system.h"
#include "race_appearance.h"
#include "race_bookmaker_reception.h"
#include "world/identity.h"
#include <array>
#include <cmath>
#include <chrono>
#include <optional>
#include <limits>

namespace horse_races::horse_market_scene {
namespace {
using Clock=horse_time::GameClock;
constexpr float kX=2944.874756F,kY=760.405151F,kZ=51.638931F,kHeading=267.516876F;
// beat_horse_race.c:817-820 supplies this model, voice and authored outfit.
constexpr const char* kBroker="RE_HORSERACE_FEMALES_01";
constexpr const char* kVoice="0847_A_F_M_Civ_Black_AVOID_02";
// All scenery names verified in analysis/reference/object_list.lua.
struct Piece {const char* model;float right,forward,up;bool ped;};
constexpr std::array<Piece,6> kPieces{{
    {kBroker,0,0,0,true},
    {"u_m_m_bwmstablehand_01",-3.4F,-1.5F,0,true},
    {"p_table06x",1.7F,-.6F,0,false},
    {"p_ledger01x",1.7F,-.6F,0,false},
    {"p_hitchingpost01x",-3.4F,-3.0F,0,false},
    {"p_hitchingpost01x",-1.0F,-3.0F,0,false}
}};
struct Owned {world::Identity id;bool revealed=false;Clock::time_point created{},stable{};appearance::Assets assets;};
std::array<Owned,kPieces.size()> owned{};
std::array<bool,kPieces.size()> requested{};
LogFn logger=nullptr;
Clock::time_point retry{};
bookmaking::ReceptionGreeting greeting;
bool nearby=false;
// Verified offline from installed levels_3.rpf: p_table06x drawable at
// yft+0x10180/190 and p_ledger01x drawable at yft+0x22c0/d0; ytyp agrees.
// Bounds only bound the ray; the actual table collision supplies support Z.
constexpr float kTableMinZ=.000764F,kTableMaxZ=.802925F,kLedgerMinZ=-.034198F;
struct LedgerSupport {
    world::Identity table;
    int probe=0;
    Clock::time_point started{},retry_at{};
    bool audited=false;
};
LedgerSupport ledger_support;
void Log(const std::string& s){if(logger)logger("horse market scene: "+s);}
Vector3 Point(const Piece& p){
    constexpr float a=kHeading*3.14159265358979323846F/180.0F;
    return {kX+std::cos(a)*p.right-std::sin(a)*p.forward,
        kY+std::sin(a)*p.right+std::cos(a)*p.forward,kZ};
}
void Remove(std::size_t i){
    if(world::Still(owned[i].id)){
        int entity=owned[i].id.entity;
        if(kPieces[i].ped)PED::DELETE_PED(&entity);else OBJECT::DELETE_OBJECT(&entity);
    }
    owned[i]={};
}
void AuditDimensions(Hash model,const char* name){
    // Raw storage safely accepts both a packed 12-byte output and the SDK's
    // padded 24-byte Vector3. Do not use either interpretation for placement.
    alignas(16) std::array<float,8> low,high;
    low.fill(std::numeric_limits<float>::quiet_NaN());high=low;
    MISC::GET_MODEL_DIMENSIONS(model,reinterpret_cast<Vector3*>(low.data()),reinterpret_cast<Vector3*>(high.data()));
    Log(std::string("dimension ABI ")+name+" min_floats="+
        std::to_string(low[0])+","+std::to_string(low[1])+","+std::to_string(low[2])+","+
        std::to_string(low[3])+","+std::to_string(low[4])+" max_floats="+
        std::to_string(high[0])+","+std::to_string(high[1])+","+std::to_string(high[2])+","+
        std::to_string(high[3])+","+std::to_string(high[4]));
}
std::optional<Vector3> LedgerSurface(Vector3 point){
    const auto table=owned[2].id;
    if(!world::Still(table))return {};
    if(ledger_support.table!=table){ledger_support={};ledger_support.table=table;}
    if(!ledger_support.audited){
        AuditDimensions(table.model,"p_table06x");
        AuditDimensions(MISC::GET_HASH_KEY("p_ledger01x"),"p_ledger01x");
        ledger_support.audited=true;
    }
    const auto now=Clock::now();
    if(now<ledger_support.retry_at)return {};
    if(!ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(table.entity))return {};
    if(!ledger_support.probe){
        const auto lower=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(table.entity,0,0,kTableMinZ);
        const auto upper=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(table.entity,0,0,kTableMaxZ);
        const float extent=kTableMaxZ-kTableMinZ;
        ledger_support.probe=SHAPETEST::START_SHAPE_TEST_LOS_PROBE(point.x,point.y,upper.z+extent,
            point.x,point.y,lower.z-extent,16,0,7);
        ledger_support.started=now;return {};
    }
    BOOL hit=false;Vector3 end{},normal{};Entity entity=0;
    const int state=SHAPETEST::GET_SHAPE_TEST_RESULT(ledger_support.probe,&hit,&end,&normal,&entity);
    if(state==1 && now-ledger_support.started<std::chrono::seconds(2))return {};
    ledger_support.probe=0;
    if(state!=2 || !hit || entity!=table.entity || !world::Still(table) ||
        !std::isfinite(end.z)||!std::isfinite(normal.z)||normal.z<.8F){
        ledger_support.retry_at=now+std::chrono::seconds(2);
        Log("ledger support not confirmed on owned table; retrying without spawning a floating book");return {};
    }
    // Parallel planes: at fixed world XY, local bottomZ translates to a
    // vertical distance bottomZ / normalZ. Align the book to the table below.
    point.z=end.z-kLedgerMinZ/normal.z;
    const auto origin=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(table.entity,0,0,0);
    Log("ledger measured table_z="+std::to_string(end.z)+" table_origin_z="+std::to_string(origin.z)+
        " normal_z="+std::to_string(normal.z)+" ledger_origin_z="+std::to_string(point.z));
    return point;
}
bool Render(std::size_t i){
    auto& o=owned[i];const int ped=world::Handle(o.id);
    if(!ped)return false;
    const auto now=Clock::now();
    const auto assets=PED::IS_PED_READY_TO_RENDER(ped)?appearance::ReadAssets(ped):appearance::Assets{};
    if(PED::IS_PED_READY_TO_RENDER(ped)&&assets.error.empty()&&assets.count>=2){
        if(o.assets!=assets||o.stable==Clock::time_point{}){o.assets=assets;o.stable=now;}
        else if(now-o.stable>=std::chrono::milliseconds(250)){
            ENTITY::SET_ENTITY_VISIBLE(ped,true);o.revealed=ENTITY::IS_ENTITY_VISIBLE(ped);
        }
    }else {o.assets={};o.stable={};o.revealed=false;ENTITY::SET_ENTITY_VISIBLE(ped,false);}
    if(!o.revealed&&now-o.created>std::chrono::seconds(8)){
        Log("appearance timeout; retrying owned scene");return false;
    }
    return true;
}
}
void Initialize(LogFn log){Cleanup();logger=log;}
Pose Location(){return {kX,kY,kZ,kHeading};}
void Cleanup(){
    for(std::size_t i=0;i<owned.size();++i){
        Remove(i);
        if(requested[i])STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(MISC::GET_HASH_KEY(kPieces[i].model));
    }
    requested.fill(false);greeting={};nearby=false;retry={};ledger_support={};
}
int Broker(){return Ready()?world::Handle(owned[0].id):0;}
bool Ready(){return nearby&&world::Still(owned[0].id)&&owned[0].revealed&&
    PED::IS_PED_READY_TO_RENDER(owned[0].id.entity)&&ENTITY::IS_ENTITY_VISIBLE(owned[0].id.entity);}
void Tick(bool world_ready){
    if(!world_ready){Cleanup();return;}
    const auto player=PLAYER::PLAYER_PED_ID();
    if(!ENTITY::DOES_ENTITY_EXIST(player)){Cleanup();return;}
    const auto at=ENTITY::GET_ENTITY_COORDS(player,true,false);
    const float dx=at.x-kX,dy=at.y-kY,dz=at.z-kZ;
    const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!std::isfinite(distance)||distance>100){Cleanup();return;}
    nearby=distance<=65;
    if(!nearby||Clock::now()<retry)return;
    STREAMING::REQUEST_COLLISION_AT_COORD(kX,kY,kZ);
    for(std::size_t i=0;i<kPieces.size();++i){
        if(world::Still(owned[i].id)){
            if(kPieces[i].ped&&!Render(i)){
                Cleanup();retry=Clock::now()+std::chrono::seconds(5);return;
            }
            continue;
        }
        owned[i]={};const auto& p=kPieces[i];const Hash model=MISC::GET_HASH_KEY(p.model);
        if(!STREAMING::IS_MODEL_VALID(model))continue;
        if(!requested[i]){STREAMING::REQUEST_MODEL(model,false);requested[i]=true;}
        if(!STREAMING::HAS_MODEL_LOADED(model))continue;
        auto point=Point(p);STREAMING::REQUEST_COLLISION_AT_COORD(point);
        Vector3 normal{};float ground=0;
        // Require actual local collision, no roof-derived or guessed floor fallback.
        if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(point.x,point.y,kZ+1.0F,&ground,&normal)||
            !std::isfinite(ground)||!std::isfinite(normal.z)||normal.z<.8F||
            kZ-ground<-.3F||kZ-ground>3.5F)continue;
        if(i==3&&!world::Still(owned[2].id))continue;
        point.z=ground+p.up;
        if(i==3){
            const auto supported=LedgerSurface(point);if(!supported)continue;
            point=*supported;
        }
        const int entity=p.ped?horse_races::SpawnBookmakerPed(model,point.x,point.y,point.z+.15F,kHeading):
            horse_races::SpawnMarketObject(model,point.x,point.y,point.z);
        if(!entity||!ENTITY::DOES_ENTITY_EXIST(entity))continue;
        auto& o=owned[i];o.id=world::Identify(entity);o.created=Clock::now();
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(entity,true,true);
        ENTITY::SET_ENTITY_HEADING(entity,kHeading);
        if(i==3){
            // Explicit no-offset final placement: creation must not add a radius.
            const auto rotation=ENTITY::GET_ENTITY_ROTATION(owned[2].id.entity,2);
            ENTITY::SET_ENTITY_ROTATION(entity,rotation.x,rotation.y,rotation.z,2,true);
            ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity,point,false,false,false);
            const auto actual=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(entity,0,0,0);
            Log("ledger final requested_z="+std::to_string(point.z)+" actual_origin_z="+std::to_string(actual.z));
        }
        if(p.ped){
            ENTITY::SET_ENTITY_VISIBLE(entity,false);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(entity,true);
            if(i==0)PED::_EQUIP_META_PED_OUTFIT(entity,static_cast<Hash>(-1918933390));
            appearance::InitializeRider(entity,i!=0,false);
            PED::SET_PED_CONFIG_FLAG(entity,297,true);
        }else {
            if(i!=3 && p.up==0)OBJECT::PLACE_OBJECT_ON_GROUND_PROPERLY(entity,true);
            o.revealed=true;
        }
        ENTITY::FREEZE_ENTITY_POSITION(entity,true);
        Log(std::string("created ")+p.model);
    }
}
void Greet(bool focused,bool eligible){
    const auto now=std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    if(greeting.Observe(focused&&eligible&&Ready(),now)){
        // Native V2 female racer approach. Exact subtitle-to-event mapping awaits a live audition.
        const bool spoken=horse_races::PlayCampSpeech(Broker(),PLAYER::PLAYER_PED_ID(),
            "RE_HR_UNI_V2_CONFIDENT_APPROACH_A",kVoice,0);
        greeting.Attempt(now,spoken);
    }
}
}
