#pragma once

// The ONLY appearance-preparation path for race riders and F9 previews. Selection
// and world placement belong to their callers; dressing, audit, fallback, mount
// confirmation and reveal do not. A ready audit is not human visual approval.
#include "horse_hair.h"
#include <span>
#include "game_natives.h"
#include "game_clock.h"
#include "race_character_policy.h"
#include "race_host_profiles.h"
#include "rider_outfits.h"
#include <main.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace horse_races::appearance {
using Clock=horse_time::GameClock;
using LogFn=void (*)(const std::string&);
enum class Status { Waiting, Ready, Failed };
struct Assets {
    int count=0;
    std::vector<std::array<Hash,4>> guids;
    std::string error;
    bool operator==(const Assets& other) const {return count==other.count && guids==other.guids && error==other.error;}
};
inline Assets ReadAssets(Ped ped)
{
    Assets result;
    result.count=PED::_GET_NUM_COMPONENTS_IN_PED(ped);
    // All currently supported race humans/horses are composite metapeds. One
    // asset is the observed Charles failure, not evidence of a complete body.
    if(result.count<2 || result.count>128) {result.error="component_count="+std::to_string(result.count);return result;}
    for(int i=0;i<result.count;++i) {
        std::array<Hash,4> asset{};
        if(!PED::GET_META_PED_ASSET_GUIDS(ped,i,&asset[0],&asset[1],&asset[2],&asset[3]) || !asset[0]) {
            result.error="unreadable_drawable_index="+std::to_string(i);return result;
        }
        result.guids.push_back(asset);
    }
    if(PED::_GET_NUM_COMPONENTS_IN_PED(ped)!=result.count) {result.error="component_list_changed";return result;}
    std::sort(result.guids.begin(),result.guids.end()); // Native asset ordering is not stable.
    return result;
}
inline Hash FixedOutfit(characters::Id id) {
    // Named full metaped outfits, not wardrobe slot numbers or transplanted
    // clothing components. Original model outfit tables (femga discoveries):
    // PLAYER_ZERO cool-weather Gunslinger; PLAYER_THREE Gunslinger = Cowboy.
    if(id==characters::Id::Arthur)return 0xA6CB81F9; // META_OUTFIT_COOL_WEATHER
    if(id==characters::Id::John)return 0x074EABA5;   // META_OUTFIT_GUNSLINGER
    return 0;
}
inline std::vector<int> AllowedPool(const std::string& ini,characters::Id id,int count)
{
    if(FixedOutfit(id))return {0}; // One named whole outfit; no indexed/random fallback.
    // Which outfits a host may wear is a column of the host table, not a case here: adding a host
    // used to mean finding every switch that knew about hosts.
    const auto* profile=hosts::Find(id);
    if(!profile || !profile->outfits_key)return {};
    const char* key=profile->outfits_key;const char* fallback=profile->outfits_fallback;
    char value[512]{};
    const DWORD size=GetPrivateProfileStringA("RacePrototype",key,fallback,value,sizeof(value),ini.c_str());
    if(size>=sizeof(value)-1)return {};
    if(id==characters::Id::Tilly && size==0 && count>0 && count<=64){
        std::vector<int> all;all.reserve(static_cast<std::size_t>(count));
        for(int outfit=0;outfit<count;++outfit)all.push_back(outfit);
        return all;
    }
    return rider_outfits::Pool(value,count,id==characters::Id::MaryBeth);
}
inline int Preferred(characters::Id id)
{
    return id==characters::Id::Charles?8:(id==characters::Id::MaryBeth?1:(id==characters::Id::Sadie?2:-1));
}
inline void InitializeRider(Ped rider,bool randomize,bool remove_hat)
{
    if(randomize)PED::_SET_RANDOM_OUTFIT_VARIATION(rider,true);
    if(remove_hat)PED::REMOVE_TAG_FROM_META_PED(rider,MISC::GET_HASH_KEY("HATS"),1);
    WEAPON::REMOVE_ALL_PED_WEAPONS(rider,true,true);
    PED::_UPDATE_PED_VARIATION(rider,false,true,true,true,false);
}
inline void InitializeHorse(Ped horse)
{
    PED::_SET_RANDOM_OUTFIT_VARIATION(horse,true);
    PED::_EQUIP_META_PED_OUTFIT(horse,MISC::GET_HASH_KEY("META_HORSE_SADDLE_ONLY"));
    PED::_UPDATE_PED_VARIATION(horse,false,true,true,true,false);
}
// A field horse's mane and tail, drawn at random from the two shop categories
// (2026-09-17: seven jockeys on one mane and one tail read as one horse). The
// item goes on the way the game's stable puts it on - _APPLY_SHOP_ITEM_TO_PED,
// then the variation refreshed - and the horse is asked whether it wears it;
// an item the game will not dress it in is not there afterwards, and another
// is drawn, a few times. Returns what it wears, 0 for nothing that took.
struct Hair { std::uint32_t mane=0,tail=0; };
inline std::uint32_t DrawHair(Ped horse,std::span<const std::uint32_t> pool)
{
    for(int attempt=0;attempt<4;++attempt) {
        const int i=MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(pool.size()));
        if(i<0 || static_cast<std::size_t>(i)>=pool.size())break;
        const auto item=pool[static_cast<std::size_t>(i)];
        invoke<Void>(0xD3A7B003ED343FD9,horse,static_cast<Hash>(item),true,true,true);   // _APPLY_SHOP_ITEM_TO_PED
        if(invoke<BOOL>(0xFB4891BD7578CDC1,horse,static_cast<Hash>(item)))return item;      // _IS_META_PED_USING_COMPONENT
    }
    return 0;
}
inline Hair RandomHair(Ped horse)
{
    Hair hair;
    hair.mane=DrawHair(horse,std::span<const std::uint32_t>(horse_hair::kManes));
    hair.tail=DrawHair(horse,std::span<const std::uint32_t>(horse_hair::kTails));
    PED::_UPDATE_PED_VARIATION(horse,false,true,true,true,false);
    return hair;
}
inline void HidePair(Ped rider,Ped horse)
{
    for(Ped ped:{rider,horse})if(ped && ENTITY::DOES_ENTITY_EXIST(ped)) {
        ENTITY::SET_ENTITY_VISIBLE(ped,false);
        ENTITY::SET_ENTITY_COLLISION(ped,false,false);
        ENTITY::FREEZE_ENTITY_POSITION(ped,true);
    }
}
struct Failure {int preset=-1;std::string reason;};
struct Session {
    Ped rider=0,horse=0;
    Hash rider_model=0,horse_model=0;
    BYTE* rider_address=nullptr;BYTE* horse_address=nullptr;
    int requested=-1,current=-1,preferred=-1,expected_type=-1;
    std::vector<int> pool,attempted;
    std::vector<Failure> failures;
    Assets rider_assets,horse_assets;
    Status status=Status::Waiting;
    bool active=false,applied=false,revealed=false;
    Hash fixed_outfit=0;int outfit_request=0;bool outfit_requested=false;
    Clock::time_point applied_at{},began{},stable_since{},mount_started{},last_mount_request{};
    std::string issue;
    LogFn log=nullptr;

    void ReleaseNamed() {
        if(outfit_requested && PED::_IS_META_PED_OUTFIT_REQUEST_VALID(outfit_request))
            PED::_RELEASE_META_PED_OUTFIT_REQUEST(outfit_request);
        outfit_request=0;outfit_requested=false;
    }
    void Reset(){ReleaseNamed();*this=Session{};}
    void Begin(Ped r,Ped h,Hash rm,Hash hm,int preset,std::vector<int> allowed,int favorite,int type,
        LogFn logger,Clock::time_point now=Clock::now(),Hash named_outfit=0)
    {
        Reset();rider=r;horse=h;rider_model=rm;horse_model=hm;requested=current=preset;
        pool=std::move(allowed);preferred=favorite;expected_type=type;log=logger;active=true;began=now;
        fixed_outfit=named_outfit;
        rider_address=getScriptHandleBaseAddress(rider);horse_address=getScriptHandleBaseAddress(horse);
    }
    bool IdentityValid() const
    {
        return rider && horse && rider_address && horse_address && ENTITY::DOES_ENTITY_EXIST(rider) &&
            ENTITY::DOES_ENTITY_EXIST(horse) && !ENTITY::IS_ENTITY_DEAD(rider) && !ENTITY::IS_ENTITY_DEAD(horse) &&
            getScriptHandleBaseAddress(rider)==rider_address && getScriptHandleBaseAddress(horse)==horse_address &&
            ENTITY::GET_ENTITY_MODEL(rider)==rider_model && ENTITY::GET_ENTITY_MODEL(horse)==horse_model;
    }
    void Emit(const std::string& event) const
    {
        if(log)log("appearance rider="+std::to_string(rider)+" requested="+std::to_string(requested)+
            " actual="+std::to_string(current)+" "+event);
    }
    Status Fail(const std::string& reason)
    {
        ReleaseNamed();
        issue=reason;status=Status::Failed;failures.push_back({current,reason});Emit("FAILED "+reason);return status;
    }
    Status Retry(const std::string& reason,Clock::time_point now)
    {
        failures.push_back({current,reason});Emit("candidate rejected: "+reason);
        int replacement=-1;
        const auto available=[&](int p) {return p>=0 && p<64 &&
            std::find(attempted.begin(),attempted.end(),p)==attempted.end();};
        if(current>=0 && attempted.size()<3) {
            if(std::find(pool.begin(),pool.end(),preferred)!=pool.end() && available(preferred))replacement=preferred;
            else for(int p:pool)if(available(p)){replacement=p;break;}
        }
        if(replacement<0) {issue=reason+"; no eligible fallback (max 3 candidates)";status=Status::Failed;Emit("FAILED "+issue);return status;}
        current=replacement;applied=false;stable_since={};mount_started={};last_mount_request={};began=now;
        rider_assets={};horse_assets={};status=Status::Waiting;
        Emit("fallback selected; original candidate NOT passed");return status;
    }
    Status Tick(Clock::time_point now=Clock::now())
    {
        if(!active || status==Status::Failed || revealed)return status;
        if(!IdentityValid())return Fail("actor identity changed/lost");
        if(!PED::IS_PED_READY_TO_RENDER(rider) || !PED::IS_PED_READY_TO_RENDER(horse)) {
            stable_since={};status=Status::Waiting;
            if(now-began>=std::chrono::seconds(15))return Fail("native render readiness timeout");
            return status;
        }
        if(expected_type>=0 && PED::_GET_META_PED_TYPE(rider)!=expected_type)return Fail("unexpected metaped type");
        if(!applied) {
            if(fixed_outfit) {
                if(!PED::_DOES_META_PED_OUTFIT_EXIST_FOR_PED_MODEL(fixed_outfit,rider_model))
                    return Fail("required protagonist outfit unavailable; no wardrobe substitution");
                if(!outfit_requested) {
                    outfit_request=PED::_REQUEST_META_PED_OUTFIT(rider_model,fixed_outfit);outfit_requested=true;
                }
                if(!PED::_IS_META_PED_OUTFIT_REQUEST_VALID(outfit_request))return Fail("named outfit request invalid");
                if(!PED::_HAS_META_PED_OUTFIT_LOADED(outfit_request)) {
                    if(now-began>=std::chrono::seconds(15))return Fail("named outfit load timeout");
                    return Status::Waiting;
                }
                if(!PED::_APPLY_PED_META_PED_OUTFIT(outfit_request,rider,true,false))return Fail("named outfit apply failed");
                PED::_UPDATE_PED_VARIATION(rider,false,true,true,true,false);ReleaseNamed();
                Emit("fixed complete protagonist outfit="+std::to_string(fixed_outfit));
            } else if(current>=0) {
                const int count=PED::GET_NUM_META_PED_OUTFITS(rider);
                if(count<=0) {if(now-began>=std::chrono::seconds(15))return Fail("outfit list unavailable");return Status::Waiting;}
                if(count>64 || current>=count)return Fail("preset outside model-local list");
                PED::_EQUIP_META_PED_OUTFIT_PRESET(rider,current,false);
                PED::_UPDATE_PED_VARIATION(rider,false,true,true,true,false);
            }
            applied=true;applied_at=now;attempted.push_back(current);status=Status::Waiting;
            Emit(current<0?"retaining original outfit; auditing shared path":"preset applied; awaiting variation rebuild");
            return status;
        }
        if(now-applied_at<std::chrono::milliseconds(300))return Status::Waiting;
        Assets r=ReadAssets(rider),h=ReadAssets(horse);
        if(!h.error.empty()) {
            horse_assets=h;stable_since={};status=Status::Waiting;
            if(now-applied_at>=std::chrono::seconds(2))return Fail("horse "+h.error);
            return status; // A broken saddle/horse is not a bad rider preset.
        }
        if(!r.error.empty()) {
            rider_assets=r;horse_assets=h;stable_since={};status=Status::Waiting;
            if(now-applied_at>=std::chrono::seconds(2))return Retry("rider "+r.error,now);
            return status;
        }
        if(!(r==rider_assets) || !(h==horse_assets) || stable_since==Clock::time_point{}) {
            rider_assets=std::move(r);horse_assets=std::move(h);stable_since=now;status=Status::Waiting;
        }
        if(PED::GET_MOUNT(rider)!=horse) {
            stable_since={};status=Status::Waiting;
            if(mount_started==Clock::time_point{})mount_started=now;
            if(now-mount_started>=std::chrono::seconds(3))return Fail("mount attachment timeout");
            if(last_mount_request==Clock::time_point{} || now-last_mount_request>=std::chrono::milliseconds(500)) {
                ENTITY::FREEZE_ENTITY_POSITION(rider,false);
                PED::SET_PED_ONTO_MOUNT(rider,horse,-1,true);last_mount_request=now;
            }
            return status;
        }
        if(now-stable_since<std::chrono::milliseconds(200)) {
            if(now-began>=std::chrono::seconds(15))return Fail("drawable set never stabilized");
            return Status::Waiting;
        }
        if(status!=Status::Ready)Emit("audit ready; rider_components="+std::to_string(rider_assets.count)+
            " horse_components="+std::to_string(horse_assets.count)+"; visual review still required");
        status=Status::Ready;return status;
    }
    bool Reveal(Clock::time_point now=Clock::now())
    {
        if(revealed)return true;
        // Re-audit in the reveal frame, not a stale boolean from a prior phase.
        if(Tick(now)!=Status::Ready)return false;
        for(Ped ped:{horse,rider}) {
            ENTITY::SET_ENTITY_COLLISION(ped,true,true);
            ENTITY::FREEZE_ENTITY_POSITION(ped,false);
            ENTITY::RESET_ENTITY_ALPHA(ped);
            ENTITY::SET_ENTITY_VISIBLE(ped,true);
        }
        revealed=true;Emit("revealed by shared appearance pipeline");return true;
    }
};
} // namespace horse_races::appearance
