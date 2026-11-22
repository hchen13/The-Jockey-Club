#pragma once
#include "native_horse_race_admission.h"
#include <utility>

namespace horse_races::native_encounter {
// Diagnostic categories only. These values never grant admission or mutate native state.
enum class BlockReason : unsigned {
    None=0, RaceHistoryBlocked=1, RaceUnavailable, StoryProgressBlocked,
    RegionNotAllowed, RegionQuotaReached, OtherEncounterBlocking, EncounterCapacityFull,
    NativeDispatchBusy, NativeSystemRestricted, LocationRestricted, PlayerStateRestricted,
    HumanCargoRestricted, LawConflictRestricted
};
struct ReasonReport {
    std::uint32_t mask=0;
    bool unknown=false;
    static constexpr std::uint32_t Bit(BlockReason reason) {
        const auto value=static_cast<unsigned>(reason);
        return value>=1 && value<=13?1u<<(value-1):0;
    }
    void Add(BlockReason reason,Truth blocked) {
        if(!blocked)unknown=true;
        else if(*blocked)mask|=Bit(reason);
    }
    bool Has(BlockReason reason) const {return (mask&Bit(reason))!=0;}
};
inline Truth Invert(Truth allowed){return allowed?Truth{!*allowed}:std::nullopt;}

// All groups are evaluated even after an earlier group blocks or is unknown.
// Live entity predicates (injury/mission/train, human cargo, recent LAW timer and
// restricted volume membership) must be appended by the runtime caller.
template<class Api> ReasonReport ReadReasons(const Reader<Api>& r,int now) {
    ReasonReport out;
    if(!r.api.Supported()){out.unknown=true;return out;}
    const auto add=[&](BlockReason kind,Truth blocked){out.Add(kind,blocked);};
    const auto valid=[&](int index,int lo,int hi)->std::optional<int>{
        auto n=r.Int(index);return n && *n>=lo && *n<=hi?n:std::nullopt;
    };
    const auto flag=[&](int index,std::uint32_t bits)->Truth{
        const auto v=r.Int(index);return v?Truth{(static_cast<std::uint32_t>(*v)&bits)!=0}:std::nullopt;
    };
    if(!r.Header(1310750,120) || !r.Header(kEvents,120))out.unknown=true;
    add(BlockReason::RaceHistoryBlocked,Invert(r.History()));
    add(BlockReason::RaceUnavailable,Invert(r.Bool(kEventState+7)));
    add(BlockReason::RaceUnavailable,r.Bool(kEventState+6));
    add(BlockReason::RaceUnavailable,r.Bool(kConfig+48));
    const auto state=valid(kEventState+5,0,7);
    add(BlockReason::RaceUnavailable,state?Truth{*state==7}:std::nullopt);

    const auto mode=r.Int(1572887+12),main=r.Int(kConfig),flags=r.Int(kConfig+1);
    const auto time=r.Int(kConfig+38),weather=r.Int(kConfig+39);
    add(BlockReason::NativeSystemRestricted,mode?Truth{*mode!=-1}:std::nullopt);
    if(!main || !flags || !time || !weather || *main!=1073745953 || *flags!=1 || *time!=8192 || *weather!=-1)
        out.unknown=true; // Unrecognized configuration is not a named known failure.
    add(BlockReason::RaceUnavailable,flag(kConfig+1,1u<<19));
    for(const auto [offset,required]:{std::pair{4,true},std::pair{5,false}}) {
        const auto mission=r.Int(kConfig+offset);
        if(!mission){out.unknown=true;continue;}
        if(*mission!=-1) {
            const auto done=r.Mission(*mission);
            add(BlockReason::StoryProgressBlocked,required?Invert(done):done);
        }
    }
    const auto rcm_start=r.Int(kConfig+6),rcm_stop=r.Int(kConfig+7);
    if(!rcm_start || !rcm_stop || *rcm_start!=-1 || *rcm_stop!=-1)out.unknown=true;

    const auto region=valid(1897952+41,0,16),allowed=r.Int(kConfig+34);
    if(!region || !allowed)out.unknown=true;
    else {
        const bool region_allowed=(static_cast<std::uint32_t>(*allowed)&(1u<<*region))!=0;
        add(BlockReason::RegionNotAllowed,!region_allowed);
        if(region_allowed) {
            const bool supported=*region==0||*region==1||*region==3||*region==6||*region==9||*region==10||*region==11;
            if(!supported)out.unknown=true;
            else {
                if(*region==3 || *region==6 || *region==11) {
                    const auto before=r.Mission(*region==11?26:56),after=r.Mission(*region==11?28:60);
                    add(BlockReason::StoryProgressBlocked,before&&after?Truth{*before&&!*after}:std::nullopt);
                }
                if(!flags)out.unknown=true;
                else if(!(static_cast<std::uint32_t>(*flags)&(1u<<15)))
                    add(BlockReason::RegionQuotaReached,Invert(r.Quota(*region)));
            }
        }
        const auto bits=r.Element(40+431,*region,1,0,18);
        add(BlockReason::LocationRestricted,bits?Truth{(*bits&1)&&!(*bits&8)}:std::nullopt);
    }
    add(BlockReason::OtherEncounterBlocking,Invert(r.ActiveSlots()));
    const auto forced=r.Bool(1310750+16075);const auto queued=valid(1327479,-1,119);
    if(!forced || *forced)out.unknown=true; // Forced dispatch is outside the supported candidate path.
    add(BlockReason::NativeDispatchBusy,queued?Truth{*queued!=-1}:std::nullopt);
    const auto bypass=r.Bool(1326862+4);const auto started=r.Int(1310750+16111);
    if(!bypass || !started || now<0)out.unknown=true;
    else if(queued && *queued==-1 && !*bypass)
        add(BlockReason::NativeSystemRestricted,*started<=0 || static_cast<std::int64_t>(*started)+5000>now);
    const auto busy=r.Int(1310750+16077);
    add(BlockReason::NativeDispatchBusy,busy?Truth{*busy!=0}:std::nullopt);
    add(BlockReason::NativeSystemRestricted,flag(1310750+16035,1));

    const auto place=valid(1894899+2,-1,150),place_flags=r.Int(1894899),blocked_places=r.Int(1310750+16074);
    add(BlockReason::LocationRestricted,flag(1894899,2));
    if(!place || !place_flags || !blocked_places)out.unknown=true;
    else add(BlockReason::LocationRestricted,(static_cast<std::uint32_t>(*blocked_places)&PlaceMask(*place))!=0);
    if(flags && (static_cast<std::uint32_t>(*flags)&(1u<<14)))
        add(BlockReason::LocationRestricted,flag(1894899,1u<<9));
    if(place && *place>=0) {
        const auto prohibited=r.Element(23118,*place,11,0,151),type=r.Element(1888801,*place,35,20,151);
        add(BlockReason::LocationRestricted,prohibited?Truth{(*prohibited&0x2000000)!=0}:std::nullopt);
        if(!type)out.unknown=true;
        else if(*type==8) {
            const int slot=*place==11?0:*place==76?1:*place==15?3:*place==128?2:-1;
            if(slot>=0 && !r.Element(1392194,slot,10,5,4))out.unknown=true;
            // Having a volume is not itself a blocker: runtime checks membership.
        }
        if(*place==5) {
            const auto size=r.Length(40+450,643),used=r.Int(40+1094);
            if(!size || !used || *used<0 || *used>*size)out.unknown=true;
            else for(int i=0;i<*used;++i) {
                const auto entry=r.Int(40+450+1+i);
                add(BlockReason::LocationRestricted,entry?Truth{*entry==58}:std::nullopt);
            }
        }
    }
    constexpr int slots=1310750+13321;
    if(!r.Header(slots,4))out.unknown=true;
    else {
        int occupied=0;bool free_slot=false,complete=true;
        for(int i=0;i<4;++i) {
            const auto event=valid(slots+1+i*9,-1,119);
            if(!event){complete=false;continue;}
            if(*event==-1){if(i>0)free_slot=true;continue;}
            const auto f=r.Int(1310750+1+*event*111+1);
            if(!f){complete=false;continue;}
            occupied+=1+((*f&(1<<14))?1:0);
        }
        if(!complete || !place || !place_flags)out.unknown=true;
        else add(BlockReason::EncounterCapacityFull,!free_slot || occupied>=((*place_flags&1)&&*place!=-1?3:4));
    }
    const auto encounters=r.Int(1430242),extra=r.Int(1430242+2),density=r.Int(1310750+16037);
    add(BlockReason::EncounterCapacityFull,encounters&&extra&&density&&place&&place_flags?
        Truth{static_cast<std::int64_t>(*encounters)+*extra>1 && (*place_flags&1) && *place!=-1 && *density>=2}:std::nullopt);

    const auto law18=r.Bool(1935630+18),law13=r.Bool(1935630+13);
    const auto law19=r.Int(1935630+19),law17=r.Int(1935630+17),law20=r.Int(1935630+20),law21=r.Int(1935630+21);
    add(BlockReason::LawConflictRestricted,law18&&law19&&law17?Truth{*law18&&(*law19>0||*law17>0)}:std::nullopt);
    add(BlockReason::LawConflictRestricted,law20?Truth{*law20>0}:std::nullopt);
    add(BlockReason::LawConflictRestricted,law13&&law21?Truth{*law13&&*law21>0}:std::nullopt);
    const auto conflict=r.Int(1934266+67);
    add(BlockReason::LawConflictRestricted,conflict&&now>=0?Truth{*conflict>0 && *conflict>static_cast<std::int64_t>(now)-30000}:std::nullopt);
    const auto weapon=r.Int(1935630+44),scenario=r.Int(1935436);
    add(BlockReason::PlayerStateRestricted,weapon?Truth{static_cast<std::uint32_t>(*weapon)==0xF6687C5Au}:std::nullopt);
    add(BlockReason::PlayerStateRestricted,r.Bool(16));add(BlockReason::PlayerStateRestricted,r.Bool(18));
    add(BlockReason::PlayerStateRestricted,scenario?Truth{*scenario==2}:std::nullopt);
    add(BlockReason::PlayerStateRestricted,flag(1935630,1u<<14));
    add(BlockReason::PlayerStateRestricted,r.Bool(1430231+4));
    if(!r.Header(kUsed,29) || !r.Int(kUsed+1+8))out.unknown=true;
    return out;
}
} // namespace horse_races::native_encounter
