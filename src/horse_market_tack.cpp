#include "horse_market_tack.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <main.h>
#include "game_natives.h"
#include "world/identity.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace horse_races::horse_market_tack {
namespace {
using Guid=std::array<Cell,4>;
// shop_horse_shop_sp.c func788/789,1374/1379,1384/1386.
// Same script-global bases as version-gated individual_identity::Capture.
constexpr int kCommon=40+1095+3054;
constexpr int kSaddleObject=1900383+393;
constexpr std::uint32_t kItem=static_cast<std::uint32_t>(-1591664384);
template<class...A> std::uint32_t Call(Cell hash,A...a){
    nativeInit(hash);(nativePush64(static_cast<Cell>(a)),...);auto*r=nativeCall();
    if(!r)throw std::runtime_error("tack native returned no result");return static_cast<std::uint32_t>(*r);
}
Cell Ptr(const void*p){return reinterpret_cast<Cell>(p);}
bool Read(int index,void*out,std::size_t n){SIZE_T done{};auto*p=getGlobalPtr(index);
    return p&&ReadProcessMemory(GetCurrentProcess(),p,out,n,&done)&&done==n;}
bool WriteWord(int index,std::uint32_t value){
    Cell cell{};if(!Read(index,&cell,sizeof cell))return false;
    cell=(cell&0xffffffff00000000ULL)|value;SIZE_T done{};auto*p=getGlobalPtr(index);
    return p&&WriteProcessMemory(GetCurrentProcess(),p,&cell,sizeof cell,&done)&&done==sizeof cell;
}
constexpr std::array<const char*,6> kSlots{{"SLOTID_HORSE_BLANKET","SLOTID_HORSE_SADDLE",
    "SLOTID_HORSE_HORN","SLOTID_HORSE_STIRRUP","SLOTID_HORSE_BEDROLL","SLOTID_HORSE_SADDLEBAG"}};
bool SameGuid(const Cell* a,const Cell* b){
    for(int i=0;i<4;++i)if(static_cast<std::uint32_t>(a[i])!=static_cast<std::uint32_t>(b[i]))return false;
    return true;
}
bool ReadItem(int inventory,const Guid& guid,const Guid& parent,std::uint32_t slot,Item& item,std::string& reason){
    // Vanilla func_2233: generic base14 works for container/accessories.
    // 025A size29 is accepted only for the horse runtime item class.
    item={};item[9]=kItem;
    if(!Call(0x9700E8EFC4AB9089,inventory,Ptr(guid.data()),Ptr(item.data()),1)){reason="generic inventory query returned false";return false;}
    if(!SameGuid(item.data(),guid.data())){reason="generic inventory GUID mismatch";return false;}
    if(!SameGuid(item.data()+5,parent.data())){reason="generic inventory parent GUID mismatch";return false;}
    if(static_cast<std::uint32_t>(item[9])!=slot){reason="generic inventory slot mismatch expected="+std::to_string(slot)+" actual="+std::to_string(static_cast<std::uint32_t>(item[9]));return false;}
    return true;
}
bool InventorySnapshot(Plan& plan,std::string& reason){
    plan.inventory=Call(0x7C7E4AB748EA3B07)?5:1;
    Guid zero{};
    if(!Call(0x886DFD3E185C8A89,plan.inventory,Ptr(zero.data()),MISC::GET_HASH_KEY("CHARACTER"),kItem,Ptr(plan.root.data()))||
        !Call(0xB881CA836CC4B6D4,Ptr(plan.root.data()))){reason="player tack inventory unavailable";return false;}
    // func1433/848/360: saddle container is a sibling of owned horses under
    // CHARACTER, item856287005 in slot-218846335. It is not a horse GUID child.
    const auto count=Call(0x033EE4B89F3AC545,plan.inventory,Ptr(plan.root.data()),static_cast<std::uint32_t>(-218846335));
    if(count>1){reason="ambiguous saddle container";return false;}
    // Vanilla func_1433 obtains the specific container GUID directly.
    plan.has_container=Call(0x886DFD3E185C8A89,plan.inventory,Ptr(plan.root.data()),856287005,
        static_cast<std::uint32_t>(-218846335),Ptr(plan.container.data()))&&
        Call(0xB881CA836CC4B6D4,Ptr(plan.container.data()));
    if(!plan.has_container){
        if(plan.saddled||count!=0){reason="saddle container GUID unavailable";return false;}
        return true;
    }
    if(!ReadItem(plan.inventory,plan.container,plan.root,static_cast<std::uint32_t>(-218846335),plan.container_item,reason)){
        reason="saddle container: "+reason;return false;
    }
    if(static_cast<std::uint32_t>(plan.container_item[4])!=856287005U){
        reason="saddle container item hash mismatch expected=856287005 actual="+std::to_string(static_cast<std::uint32_t>(plan.container_item[4]));return false;
    }
    plan.equipment.clear();
    for(const auto* name:kSlots){
        Equipment entry;entry.slot=MISC::GET_HASH_KEY(name);
        const auto children=Call(0x033EE4B89F3AC545,plan.inventory,Ptr(plan.container.data()),entry.slot);
        if(children>128){reason="tack slot inventory count invalid";return false;}
        for(std::uint32_t i=0;i<children;++i){
            Item child{},detail{};child[9]=kItem;Guid guid{};
            if(!Call(0xBE012571B25F5ACA,plan.inventory,Ptr(plan.container.data()),entry.slot,i,Ptr(child.data()))||
                !Call(0xB881CA836CC4B6D4,Ptr(child.data()))){
                reason="tack item identity unavailable";return false;
            }
            std::copy_n(child.begin(),4,guid.begin());
            if(!ReadItem(plan.inventory,guid,plan.container,entry.slot,detail,reason)){
                reason="tack item: "+reason;return false;
            }
            entry.items.push_back(detail);
        }
        entry.has_equipped=Call(0x22E590F108289A9D,plan.inventory,Ptr(plan.container.data()),entry.slot,Ptr(entry.equipped.data()))!=0;
        if(entry.has_equipped&&!Call(0xB881CA836CC4B6D4,Ptr(entry.equipped.data()))){reason="equipped tack GUID invalid";return false;}
        plan.equipment.push_back(std::move(entry));
    }
    return true;
}
bool SameInventory(const Plan&a,const Plan&b){return a.inventory==b.inventory&&a.root==b.root&&
    a.has_container==b.has_container&&a.container==b.container&&a.container_item==b.container_item&&a.equipment==b.equipment;}
bool SameHorse(const Plan& p){return world::Still({p.horse,p.model,p.address});}
bool SameSaddleObject(const Plan& p){
    Cell current{};
    if(!Read(kSaddleObject,&current,sizeof current)||current!=p.saddle_object)return false;
    const auto object=world::Identify(static_cast<int>(static_cast<std::uint32_t>(current)));
    return object.model==p.saddle_object_model&&object.address==p.saddle_object_address;
}
bool NoCargo(int horse,int slot,std::array<Cell,74>&out,std::string&reason){
    // 298: count57 +57 scalar pelts. 356: count3 + three5-cell animal cargo records.
    if(!Read(1137+436*slot+298,out.data(),sizeof out)||
        static_cast<std::uint32_t>(out[0])!=57||static_cast<std::uint32_t>(out[58])!=3){reason="horse cargo layout unavailable";return false;}
    for(int i=1;i<58;++i)if(static_cast<std::uint32_t>(out[i])!=0){reason="remove stored pelts before selling this horse";return false;}
    for(int i=59;i<74;++i)if(static_cast<std::uint32_t>(out[i])!=0){reason="remove stored animal cargo before selling this horse";return false;}
    for(int slot_id=4;slot_id<=6;++slot_id){Guid carry{};
        if(Call(0x608BC6A6AACD5036,Ptr(carry.data()),horse,slot_id,0)&&ENTITY::DOES_ENTITY_EXIST(static_cast<int>(carry[3]))){
            reason="remove carried cargo before selling this horse";return false;
        }
    }
    return true;
}
}
bool Prepare(int horse,int slot,Plan& result,std::string&reason,bool allow_player_rider){
    try{
        Plan p;p.horse=horse;p.slot=slot;
        const auto id=world::Identify(horse);p.model=id.model;p.address=id.address;
        if(slot<0||slot>4||!id.Known()){reason="invalid horse relocation target";return false;}
        const int player=PLAYER::PLAYER_PED_ID();
        const int rider=PED::_GET_RIDER_OF_MOUNT(horse,true);
        if((rider!=0&&(!allow_player_rider||rider!=player))||
            (!allow_player_rider&&PED::GET_MOUNT(player)==horse)){
            reason="finish dismounting before sale";return false;
        }
        p.saddled=PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID())==horse;
        if(!Read(kSaddleObject,&p.saddle_object,sizeof p.saddle_object)||!Read(kCommon,p.common.data(),sizeof p.common)||
            static_cast<std::uint32_t>(p.common[3])!=57||static_cast<std::uint32_t>(p.common[61])!=3){reason="common saddle storage layout unavailable";return false;}
        const auto loose=world::Identify(static_cast<int>(static_cast<std::uint32_t>(p.saddle_object)));
        if(loose.entity){
            // A removed saddle is independent of an unsaddled sale target.
            // Vanilla func799 clears the saddle-horse binding before exposing
            // its carryable saddle object. Preserve that object and inventory.
            if(p.saddled){reason="saddle assignment conflicts with the loose saddle";return false;}
            if(!loose.Known()){reason="loose saddle identity unavailable";return false;}
            p.saddle_object_model=loose.model;p.saddle_object_address=loose.address;
        }
        if(p.saddled&&(static_cast<int>(static_cast<std::uint32_t>(p.common[0]))!=slot||static_cast<std::uint32_t>(p.common[1])>3)){
            reason="saddle assignment changed";return false;
        }
        if(!NoCargo(horse,slot,p.cargo,reason)||!InventorySnapshot(p,reason))return false;
        result=std::move(p);return true;
    }catch(const std::exception&e){reason=e.what();return false;}
}
bool Unchanged(const Plan&p,std::string&reason,bool allow_player_rider){
    Plan current;
    if(!SameHorse(p)||!Prepare(p.horse,p.slot,current,reason,allow_player_rider)||
       !SameInventory(p,current)||p.saddled!=current.saddled||p.common!=current.common||p.cargo!=current.cargo||
       p.saddle_object!=current.saddle_object||p.saddle_object_model!=current.saddle_object_model||p.saddle_object_address!=current.saddle_object_address){
        reason="tack changed during ownership request: "+reason;return false;
    }
    return true;
}
namespace {
bool RelocateImpl(const Plan&p,std::string&reason,bool retain_visuals){
    try{
        Plan current;
        if(!SameHorse(p)||!Prepare(p.horse,p.slot,current,reason,retain_visuals)||!SameInventory(p,current)||
            p.saddled!=current.saddled||p.common!=current.common||p.cargo!=current.cargo||
            p.saddle_object!=current.saddle_object||p.saddle_object_model!=current.saddle_object_model||p.saddle_object_address!=current.saddle_object_address){reason="tack changed before relocation: "+reason;return false;}
        if(!p.saddled)return true;
        // func797's zero six-component outfit, preserving the independent inventory.
        // No mane/tail changes are needed on a horse that will be sold/deleted.
        if(!retain_visuals){
            for(const char*category:{"HORSE_BLANKETS","HORSE_SADDLES","SADDLE_HORNS","SADDLE_STIRRUPS","HORSE_BEDROLLS","HORSE_SADDLEBAGS"})
                PED::REMOVE_SHOP_ITEM_FROM_PED_BY_CATEGORY(p.horse,MISC::GET_HASH_KEY(category),0,false);
            PED::_UPDATE_PED_VARIATION(p.horse,false,true,true,true,true);
        }
        // func772→1348→788(-1)/789(3). The independent saddle inventory stays owned.
        PLAYER::_SET_PED_AS_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID(),0);
        if(!WriteWord(kCommon,static_cast<std::uint32_t>(-1))||!WriteWord(kCommon+1,3)){
            reason="saddle storage state write incomplete";return false;
        }
        return Preserved(p,reason);
    }catch(const std::exception&e){reason=e.what();return false;}
}
}
bool Relocate(const Plan&p,std::string&reason){return RelocateImpl(p,reason,false);}
bool DetachBindingPreservingVisuals(const Plan&p,std::string&reason){return RelocateImpl(p,reason,true);}
bool Preserved(const Plan&p,std::string&reason){
    try{
        if(!SameSaddleObject(p)){reason="loose saddle changed during ownership transfer";return false;}
        Plan current;current.saddled=p.saddled;
        if(!InventorySnapshot(current,reason)||!SameInventory(p,current)){reason="saddle inventory preservation not confirmed: "+reason;return false;}
        std::array<Cell,80> common{};auto expected=p.common;
        if(p.saddled){expected[0]=(expected[0]&0xffffffff00000000ULL)|0xffffffffU;expected[1]=(expected[1]&0xffffffff00000000ULL)|3U;}
        if(!Read(kCommon,common.data(),sizeof common)||common!=expected){reason="common saddle storage changed";return false;}
        if(p.saddled&&PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID())==p.horse){reason="saddle horse binding still references sold horse";return false;}
        return true;
    }catch(const std::exception&e){reason=e.what();return false;}
}
}
