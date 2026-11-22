#pragma once
#include "native_horse_race_conditions.h"

namespace horse_races::native_encounter {
struct Admission {
    Truth allowed;
    std::optional<int> restricted_volume;
};
inline std::uint32_t PlaceMask(int place) {
    constexpr std::array<int,25> places{40,78,37,38,93,82,95,65,68,69,3,61,110,5,35,22,105,8,32,26,115,76,92,56,120};
    for(std::size_t i=0;i<places.size();++i)if(places[i]==place)return 1u<<i;
    return 0;
}
// medium_update.c func_256/654 et al. Copy only their read-only predicates:
// never the writes that alter public cooldowns or the scheduler queue.
// Future local placement, occupancy and competitive selection remain native.
template<class Api> Admission ReadAdmission(const Reader<Api>& read,int now) {
    Admission out;
    if(now<0 || !read.api.Supported())return out;
    const auto forced=read.Bool(1310750+16075);const auto queued=read.Int(1327479);
    const auto bypass_start=read.Bool(1326862+4);const auto started=read.Int(1310750+16111);
    if(!forced || !queued || !bypass_start || !started)return out;
    if(*forced)return out; // Special/forced native dispatch is not our ordinary candidate path.
    const auto block=[&](){out.allowed=false;return out;};
    if(*queued < -1 || *queued>=120)return out;
    if(*queued!=-1)return block(); // Native is already dispatching an event, not selecting a new candidate.
    if(*queued==-1 && !*bypass_start && (*started<=0 || static_cast<std::int64_t>(*started)+5000>now))return block();
    const auto busy=read.Int(1310750+16077),disabled=read.Int(1310750+16035);
    const auto place=read.Int(1894899+2),place_flags=read.Int(1894899),region=read.Int(1897952+41);
    const auto blocked_places=read.Int(1310750+16074);
    if(!busy || !disabled || !place || !place_flags || !region || !blocked_places)return out;
    if(*place < -1 || *place>150 || *region<0 || *region>16)return out;
    if(*busy || (*disabled&1) || (*place_flags&2) ||
        (static_cast<std::uint32_t>(*blocked_places)&PlaceMask(*place)))return block();
    // Dispatch has four slots (slot zero reserved); some events consume two.
    constexpr int slots=1310750+13321;
    if(!read.Header(slots,4))return out;
    int occupied=0;bool free_slot=false;
    for(int i=0;i<4;++i) {
        const auto event=read.Int(slots+1+i*9);
        if(!event || *event < -1 || *event>=120)return out;
        if(*event==-1){if(i>0)free_slot=true;continue;}
        const auto flags=read.Int(1310750+1+*event*111+1);
        if(!flags)return out;
        occupied+=1+((*flags&(1<<14))?1:0);
    }
    if(!free_slot || occupied>=((*place_flags&1) && *place!=-1?3:4))return block();
    const auto region_bits=read.Element(40+431,*region,1,0,18);
    if(!region_bits)return out;
    if((*region_bits&1) && !(*region_bits&8))return block();
    if(*place>=0) {
        const auto prohibited=read.Element(23118,*place,11,0,151);
        const auto type=read.Element(1888801,*place,35,20,151);
        if(!prohibited || !type)return out;
        if(*prohibited&0x2000000)return block();
        if(*type==8) {
            const int volume_slot=*place==11?0:*place==76?1:*place==15?3:*place==128?2:-1;
            if(volume_slot>=0) {
                out.restricted_volume=read.Element(1392194,volume_slot,10,5,4);
                if(!out.restricted_volume)return out;
            }
        }
    }
    const auto law18=read.Bool(1935630+18),law13=read.Bool(1935630+13);
    const auto law19=read.Int(1935630+19),law17=read.Int(1935630+17);
    const auto law20=read.Int(1935630+20),law21=read.Int(1935630+21),conflict=read.Int(1934266+67);
    if(!law18 || !law13 || !law19 || !law17 || !law20 || !law21 || !conflict)return out;
    if((*law18 && (*law19>0 || *law17>0)) || *law20>0 || (*law13 && *law21>0) ||
        (*conflict>0 && *conflict>static_cast<std::int64_t>(now)-30000))return block();
    const auto encounters=read.Int(1430242),extra=read.Int(1430242+2),density=read.Int(1310750+16037);
    if(!encounters || !extra || !density)return out;
    if(static_cast<std::int64_t>(*encounters)+*extra>1 && (*place_flags&1) && *place!=-1 && *density>=2)return block();
    const auto weapon=read.Int(1935630+44);const auto g16=read.Bool(16),g18=read.Bool(18);
    const auto scenario=read.Int(1935436),player_flags=read.Int(1935630);const auto scene=read.Bool(1430231+4);
    if(!weapon || !g16 || !g18 || !scenario || !player_flags || !scene)return out;
    // joaat("WEAPON_KIT_BINOCULARS"), verified by the native hash function in tests.
    if(static_cast<std::uint32_t>(*weapon)==0xF6687C5Au || *g16 || *g18 || *scenario==2 ||
        (*player_flags&(1<<14)) || *scene)return block();
    if(*place==5) {
        const auto size=read.Length(40+450,643),used=read.Int(40+1094);
        if(!size || !used || *used<0 || *used>*size)return out;
        for(int i=0;i<*used;++i) {
            const auto value=read.Int(40+450+1+i);
            if(!value)return out;
            if(*value==58)return block();
        }
    }
    out.allowed=true;
    return out;
}
} // namespace horse_races::native_encounter
