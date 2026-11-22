#include "horse_market_sale.h"
#include "horse_market_sale_layout.h"
#include "horse_market_tack.h"
#include "world/identity.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <main.h>
#include "game_natives.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
namespace horse_races::horse_market_sale {
namespace {
using Cell=std::uint64_t;
using Saved=std::array<Cell,436>;
using Active=std::array<Cell,45>;
using Guid=std::array<Cell,4>;
template<class... A> std::uint32_t Call(Cell hash,A... a){nativeInit(hash);(nativePush64(static_cast<Cell>(a)),...);auto*r=nativeCall();if(!r)throw std::runtime_error("native returned no result");return static_cast<std::uint32_t>(*r);}
Cell Ptr(const void*p){return reinterpret_cast<Cell>(p);}
bool Read(int index,void* out,std::size_t n){SIZE_T done{};auto*p=getGlobalPtr(index);return p&&ReadProcessMemory(GetCurrentProcess(),p,out,n,&done)&&done==n;}
bool Write(int index,const void* in,std::size_t n){SIZE_T done{};auto*p=getGlobalPtr(index);return p&&WriteProcessMemory(GetCurrentProcess(),p,in,n,&done)&&done==n;}
bool Same(const individual_identity::Identity&a,const individual_identity::Identity&b){return a.guid==b.guid&&a.model==b.model&&a.slot==b.slot;}
bool InventoryAvailable(int inventory,Guid* observed=nullptr){
    Guid zero{},root{};
    const bool valid=Call(0x886DFD3E185C8A89,inventory,Ptr(zero.data()),MISC::GET_HASH_KEY("CHARACTER"),
         static_cast<std::uint32_t>(-1591664384),Ptr(root.data()))&&Call(0xB881CA836CC4B6D4,Ptr(root.data()));
    if(valid&&observed)*observed=root;
    return valid;
}
void RestoreModelSuppression(std::uint32_t model){
    // Vanilla func_1410 / func_2125 / func_2702-2703.
    bool pc_exclusive=false;
    for(const char* name:{"A_C_HORSE_ARABIAN_REDCHESTNUT_PC","A_C_HORSE_KENTUCKYSADDLE_BUTTERMILKBUCKSKIN_PC",
        "A_C_HORSE_MORGAN_LIVERCHESTNUT_PC","A_C_HORSE_APPALOOSA_FEWSPOTTED_PC","A_C_HORSE_ANDALUSIAN_PERLINO",
        "A_C_HORSE_TENNESSEEWALKER_GOLDPALOMINO_PC","A_C_HORSE_ARABIAN_WARPEDBRINDLE_PC"})
        if(MISC::GET_HASH_KEY(name)==model)pc_exclusive=true;
    if(PED::_IS_PED_MODEL_SUPPRESSED(model)&&(!pc_exclusive||UNLOCK::UNLOCK_IS_UNLOCKED(MISC::GET_HASH_KEY("SP_GAME_CONTENT_PC_TIMED_EXCLUSIVE"))))
        PED::SET_PED_MODEL_IS_SUPPRESSED(model,false);
}
}
Check Inspect(int horse,const individual_identity::Identity& approved){
    try {
        std::string reason;auto current=individual_identity::Capture(horse,reason);
        if(!current||!Same(*current,approved))return {false,false,"identity changed: "+reason};
        if(approved.slot<0||approved.slot>4)return {false,false,"owned horse slot is unsupported"};
        const auto player=PLAYER::PLAYER_PED_ID();const auto rider=PED::_GET_RIDER_OF_MOUNT(horse,true);
        if(rider&&rider!=player)return {false,false,"another rider is using this horse"};
        if(ENTITY::IS_ENTITY_DEAD(horse)||PED::IS_PED_INJURED(horse)||PED::IS_PED_IN_COMBAT(horse,0))
            return {false,false,"horse must be healthy and out of combat"};
        for(int i=4;i<=6;++i){std::array<Cell,4> carry{};
            if(Call(0x608BC6A6AACD5036,Ptr(carry.data()),horse,i,0)&&ENTITY::DOES_ENTITY_EXIST(static_cast<int>(carry[3])))
                return {false,false,"remove carried cargo before sale"};
        }
        const bool mounted=PED::GET_MOUNT(player)==horse||rider==player;
        horse_market_tack::Plan tack;
        if(!horse_market_tack::Prepare(horse,approved.slot,tack,reason,true))return {false,mounted,reason};
        return {true,mounted,{}};
    }catch(const std::exception&e){return {false,false,e.what()};}
    catch(...){return {false,false,"horse sale inspection exception"};}
}
namespace {
Result RemoveImpl(int horse,const individual_identity::Identity& approved,bool retain_entity){
    bool entered=false,retained_entity=false;
    try {
        auto eligibility=Inspect(horse,approved);
        if(!eligibility.eligible||(!retain_entity&&eligibility.needs_dismount))return {State::NotStarted,eligibility.needs_dismount?"dismount before sale":eligibility.reason};
        std::string reason;auto current=individual_identity::Capture(horse,reason);
        if(!current||!Same(*current,approved))return {State::NotStarted,"identity changed: "+reason};
        const auto player=PLAYER::PLAYER_PED_ID();
        const auto pinned=world::Identify(horse);
        const auto rider=PED::_GET_RIDER_OF_MOUNT(horse,true);
        if(!pinned.Known())return {State::NotStarted,"horse entity identity unavailable"};
        if((!retain_entity&&(PED::GET_MOUNT(player)==horse||rider!=0))||
           (retain_entity&&rider!=0&&rider!=player))
            return {State::NotStarted,"dismount before sale"};
        if(TASK::_IS_PED_LEADING_HORSE(player))
            return {State::NotStarted,"finish releasing the lead before sale"};
        const int saved_index=1137+436*approved.slot, active_index=1900384+45*approved.slot;
        Saved saved{};Active active{};
        if(!Read(saved_index,saved.data(),sizeof saved)||!Read(active_index,active.data(),sizeof active)||
           static_cast<std::uint32_t>(saved[15])!=3||static_cast<std::uint32_t>(saved[298])!=57||
           static_cast<std::uint32_t>(saved[356])!=3||static_cast<std::uint32_t>(saved[374])!=23||
           static_cast<std::uint32_t>(saved[398])!=2||static_cast<std::uint32_t>(saved[407])!=3)
            return {State::NotStarted,"stable record layout changed"};
        horse_market_tack::Plan tack;
        if(!horse_market_tack::Prepare(horse,approved.slot,tack,reason,retain_entity))return {State::NotStarted,reason};
        // Carrying objects must never be silently destroyed with the mount.
        for(int i=4;i<=6;++i){std::array<Cell,4> carry{};
            // func_2124 / func_2701: padded four-cell carry structure.
            if(Call(0x608BC6A6AACD5036,Ptr(carry.data()),horse,i,0)&&
               ENTITY::DOES_ENTITY_EXIST(static_cast<int>(carry[3])))
                return {State::NotStarted,"remove carried cargo before sale"};
        }
        const auto final=individual_identity::Capture(horse,reason);
        Saved check{};Active active_check{};
        if(!final||!Same(*final,approved)||!Read(saved_index,check.data(),sizeof check)||check!=saved||
           !Read(active_index,active_check.data(),sizeof active_check)||active_check!=active)
            return {State::NotStarted,"owned horse changed during confirmation"};
        Guid guid{};for(int i=0;i<4;++i)guid[i]=approved.guid.words[i];
        const int inventory=Call(0x7C7E4AB748EA3B07)?5:1;
        std::array<Cell,29> item{};item[9]=static_cast<std::uint32_t>(-1591664384);
        if(!Call(0x025A1B1FB03FBF61,inventory,Ptr(guid.data()),Ptr(item.data()),29,1)||
           static_cast<std::uint32_t>(item[11])!=1)
            return {State::NotStarted,"horse inventory item is not exactly one owned item"};
        for(int i=0;i<4;++i)if(static_cast<std::uint32_t>(item[i])!=approved.guid.words[i])
            return {State::NotStarted,"horse inventory query returned a different GUID"};
        const int final_rider=PED::_GET_RIDER_OF_MOUNT(horse,true);
        const int final_mount=PED::GET_MOUNT(player);
        if((!retain_entity&&(PED::IS_PED_ON_MOUNT(player)||final_rider!=0))||
           (retain_entity&&((final_rider!=0&&final_rider!=player)||(final_mount!=0&&final_mount!=horse)))||
           TASK::_IS_PED_LEADING_HORSE(player)||!world::Still(pinned))
            return {State::NotStarted,"mount or lead relationship changed before sale"};
        Guid root{};
        if(!InventoryAvailable(inventory,&root))return {State::NotStarted,"horse inventory root unavailable before removal"};
        constexpr std::array<std::uint32_t,5> slots{{1782082944U,static_cast<std::uint32_t>(-832337898),1271463052U,1983140194U,677262775U}};
        const auto slot_hash=slots[approved.slot];
        const auto count=[&](){return Call(0x033EE4B89F3AC545,inventory,Ptr(root.data()),slot_hash);};
        const auto exact_child=[&](){
            std::array<Cell,29> child{};child[9]=static_cast<std::uint32_t>(-1591664384);
            if(!Call(0xBE012571B25F5ACA,inventory,Ptr(root.data()),slot_hash,0,Ptr(child.data())))return false;
            for(int i=0;i<4;++i)if(static_cast<std::uint32_t>(child[i])!=approved.guid.words[i])return false;
            return true;
        };
        if(count()!=1||!exact_child())return {State::NotStarted,"horse inventory slot changed before removal"};
        entered=true;
        // Inventory func_1676: exact GUID, quantity one, remove reason SELL.
        // 1051348's 793bb8 guard rejects inventory type2 (async transaction path).
        // Accepted requests synchronously change inventory under its lock through
        // 7990d0 -> 799148 -> 7993bc -> 7995ec. Detach only after absence is
        // proven: an external infinite-items hook can acknowledge without removing.
        const bool removed=Call(0x3E4E811480B3AE79,inventory,Ptr(guid.data()),1,1992602754)!=0;
        std::array<Cell,29> remaining{};remaining[9]=static_cast<std::uint32_t>(-1591664384);
        Guid after_root{};
        if((Call(0x7C7E4AB748EA3B07)?5:1)!=inventory||!InventoryAvailable(inventory,&after_root)||after_root!=root)
            return {State::Uncertain,"inventory root/mode unavailable after removal; native_result="+std::to_string(removed)};
        const bool resolves=Call(0x025A1B1FB03FBF61,inventory,Ptr(guid.data()),Ptr(remaining.data()),29,1)!=0;
        const auto children=count();
        if(resolves||children!=0||!removed){
            Saved unchanged_saved{};Active unchanged_active{};
            const bool unchanged=resolves&&remaining==item&&children==1&&exact_child()&&
                Read(saved_index,unchanged_saved.data(),sizeof unchanged_saved)&&unchanged_saved==saved&&
                Read(active_index,unchanged_active.data(),sizeof unchanged_active)&&unchanged_active==active&&
                world::Still(pinned)&&PED::_GET_RIDER_OF_MOUNT(horse,true)==final_rider&&
                PED::GET_MOUNT(player)==final_mount&&!TASK::_IS_PED_LEADING_HORSE(player)&&
                horse_market_tack::Unchanged(tack,reason,retain_entity);
            const auto detail="native_result="+std::to_string(removed)+" guid_resolves="+std::to_string(resolves)+
                " quantity="+std::to_string(static_cast<std::uint32_t>(remaining[11]))+" slot_children="+std::to_string(children);
            if(unchanged)return {State::Unchanged,"inventory removal left exact ownership, saddle and world unchanged: "+detail};
            return {State::Uncertain,"ownership removal not confirmed: "+detail+"; "+reason};
        }
        if(retain_entity){ENTITY::SET_ENTITY_AS_MISSION_ENTITY(horse,true,true);retained_entity=true;}
        if(!(retain_entity?horse_market_tack::DetachBindingPreservingVisuals(tack,reason):horse_market_tack::Relocate(tack,reason)))return {State::Uncertain,"saddle relocation not confirmed: "+reason,retained_entity};
        if(!horse_market_tack::Preserved(tack,reason))return {State::Uncertain,"saddle inventory not preserved: "+reason,retained_entity};
        auto reset=detail::ResetSaved(saved);
        // Hair defaults in func_1396/1397 are inventory child mutations. The
        // complete horse GUID was removed above, so there is no child to mutate.
        for(int i:{26,27}){int blip=static_cast<int>(active[i]);if(MAP::DOES_BLIP_EXIST(blip))MAP::REMOVE_BLIP(&blip);}
        Active reset_active=detail::ResetActive(active);
        RestoreModelSuppression(approved.model);
        if(!retain_entity){
            if(ENTITY::DOES_ENTITY_EXIST(horse)){
                ENTITY::SET_ENTITY_AS_MISSION_ENTITY(horse,true,true);int handle=horse;PED::DELETE_PED(&handle);
            }
            if(ENTITY::DOES_ENTITY_EXIST(horse))return {State::Uncertain,"horse entity removal not confirmed"};
        }else if(!world::Still(pinned))return {State::Uncertain,"transferred horse entity changed",retained_entity};
        if(!Write(saved_index,reset.data(),sizeof reset)||!Write(active_index,reset_active.data(),sizeof reset_active))
            return {State::Uncertain,"stable record reset incomplete",retained_entity};
        Saved verified{};Active verified_active{};
        remaining={};remaining[9]=static_cast<std::uint32_t>(-1591664384);
        if(!Read(saved_index,verified.data(),sizeof verified)||verified!=reset||
           !Read(active_index,verified_active.data(),sizeof verified_active)||verified_active!=reset_active||
           (Call(0x7C7E4AB748EA3B07)?5:1)!=inventory||
           !InventoryAvailable(inventory,&after_root)||after_root!=root||
           Call(0x025A1B1FB03FBF61,inventory,Ptr(guid.data()),Ptr(remaining.data()),29,1)||count()!=0)
            return {State::Uncertain,"post-sale ownership verification failed",retained_entity};
        if(!horse_market_tack::Preserved(tack,reason))return {State::Uncertain,"post-sale saddle verification failed: "+reason,retained_entity};
        if(retain_entity&&!world::Still(pinned))return {State::Uncertain,"transferred horse entity no longer survives",retained_entity};
        return {State::RemovedConfirmed,retain_entity?"ownership removed; identical mission-owned entity retained with visuals; payout not performed":
            "owned GUID and stable record removed; payout not performed",retained_entity};
    }catch(const std::exception&e){return {entered?State::Uncertain:State::NotStarted,e.what(),retained_entity};}
    catch(...){return {entered?State::Uncertain:State::NotStarted,"horse sale exception",retained_entity};}
}
}
Result Remove(int horse,const individual_identity::Identity& approved){return RemoveImpl(horse,approved,false);}
Result TransferOwnership(int horse,const individual_identity::Identity& approved){return RemoveImpl(horse,approved,true);}

}
