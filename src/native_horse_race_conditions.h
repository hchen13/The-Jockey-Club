#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <cstdint>
#include <limits>
#include <optional>

// Read-only 1491.50 script cells. Array headers occupy one 8-byte cell.
// This is a candidate filter, not an attempt to run Rockstar's local scheduler.
namespace horse_races::native_encounter {
using Truth=std::optional<bool>;
inline constexpr int kEvent=27,kConfig=1310750+1+kEvent*111;
inline constexpr int kHistory=17504+1003,kUsed=17504+12,kEvents=17504+42;
inline constexpr int kEventState=kEvents+1+kEvent*8;
inline constexpr int kDirectory=1326862+11,kStarts=1326862+512,kEnds=1326862+558;
inline constexpr int kCounts=17504+2205;
inline constexpr bool Yes(Truth value){return value && *value;}
inline Truth Daytime(int hour){return hour>=0 && hour<24?Truth{hour>=6 && hour<20}:std::nullopt;}
inline Truth Attributes(int handling,int speed) {
    if(handling<0 || handling>9 || speed<0 || speed>9)return std::nullopt;
    return handling>=2 || speed>=2;
}
inline std::optional<int> CooldownSteps(int raw) {
    if(raw < -89)return std::nullopt;
    return raw<=0?0:static_cast<int>((static_cast<std::int64_t>(raw)+89)/90);
}
inline std::optional<int> SharedSeconds(int deadline,int now) {
    // Timer wrap/rebase is not a trustworthy countdown. Do not guess zero.
    if(deadline<0 || now<0)return std::nullopt;
    return static_cast<int>((std::max<std::int64_t>(0,static_cast<std::int64_t>(deadline)-now)+999)/1000);
}
struct Snapshot {
    Truth supported,history_ok,common_ok,bonding;
    std::optional<int> cooldown_steps,shared_deadline,player_region;
    std::optional<std::uint32_t> used;
    std::array<Truth,17> regions{};
    // Globals used by func_2449; evaluated against native entity state each frame.
    std::optional<int> carried,stowed,lasso_active;
    bool ignore_human_cargo=false;
};

template<class Api> struct Reader {
    const Api& api;
    mutable std::map<int,std::optional<int>> cells;
    std::optional<int> Int(int index) const {
        if(const auto it=cells.find(index);it!=cells.end())return it->second;
        std::uint64_t cell=0;
        if(index<0 || !api.ReadGlobal(index,cell))return cells[index]=std::nullopt;
        return cells[index]=static_cast<std::int32_t>(static_cast<std::uint32_t>(cell));
    }
    bool Header(int index,int expected) const {return Int(index)==expected;}
    std::optional<int> Length(int header,int max) const {
        auto size=Int(header);
        return size && *size>0 && *size<=max?size:std::nullopt;
    }
    std::optional<int> Element(int header,int index,int stride,int offset,int max) const {
        const auto count=Length(header,max);
        if(!count || index<0 || index>=*count || stride<=0 || offset<0 || offset>=stride)return std::nullopt;
        return Int(header+1+index*stride+offset);
    }
    Truth Bool(int index) const {
        const auto value=Int(index);
        if(!value || (*value!=0 && *value!=1))return std::nullopt;
        return *value==1;
    }
    Truth Mission(int mission) const {
        const auto id=Element(1835011,mission,74,1,256);
        if(!id || *id<=0 || *id>=771)return std::nullopt;
        const auto state=Element(12106,*id,7,1,771);
        if(!state || *state<0 || *state>9)return std::nullopt;
        return *state==5 || *state==6;
    }
    Truth History() const {
        if(!Header(kHistory,180))return std::nullopt;
        std::array<bool,3> found{};
        for(int row=0;row<180;++row) {
            const int base=kHistory+1+row*6;
            const auto key=Int(base);
            if(!key)return std::nullopt;
            if(*key!=27 && *key!=147 && *key!=267)continue;
            const int npc=(*key-27)/120;
            if(found[npc])return std::nullopt; // Conflicting directory, never guess.
            found[npc]=true;
            const auto outcomes=Int(base+1),attempts=Int(base+2);
            if(!outcomes || !attempts || *outcomes<0 || *attempts<0)return std::nullopt;
            if(*outcomes>=3 || *attempts==3)return false;
        }
        return true; // Native lookup returns zero for a key not recorded yet.
    }
    std::optional<int> Completions(int region) const {
        const auto start=Element(kStarts,region,1,0,45),end=Element(kEnds,region,1,0,45);
        const auto size=Length(kDirectory,500),counts=Length(kCounts,500);
        if(!start || !end || !size || !counts || *start<0 || *start>=*size ||
            *end < *start-1 || *end>=*size)return std::nullopt;
        for(int i=*start;i<=*end;++i) {
            const auto event=Int(kDirectory+1+i);
            if(!event || *event<0 || *event>=120)return std::nullopt;
            if(*event!=kEvent)continue;
            if(i>=*counts)return std::nullopt;
            const auto result=Int(kCounts+1+i*2+1);
            return result && *result>=0?result:std::nullopt;
        }
        return 0;
    }
    Truth Quota(int region) const {
        int cap_index=-1;
        std::array<int,4> members{-1,-1,-1,-1};
        switch(region) {
        case 0:case 11:members={0,2,11,-1};cap_index=0;break;
        case 1:case 6:members={1,7,6,12};cap_index=1;break;
        case 3:case 10:members={3,10,-1,-1};cap_index=3;break;
        case 9:members={4,9,-1,-1};cap_index=4;break;
        default:return std::nullopt;
        }
        const auto cap=Element(kConfig+64,cap_index,1,0,45);
        if(!cap || *cap<0)return std::nullopt;
        std::int64_t total=0;
        for(int member:members)if(member>=0) {
            const auto count=Completions(member);
            if(!count)return std::nullopt;
            total+=*count;
        }
        return total<*cap;
    }
    Truth Region(int region,std::uint32_t allowed,bool skip_quota) const {
        if(region<0 || region>=17)return std::nullopt;
        if(!(allowed&(1u<<region)))return false;
        // Only regions configured by the verified native horse-race script.
        if(region!=0 && region!=1 && region!=3 && region!=6 && region!=9 && region!=10 && region!=11)
            return std::nullopt;
        if(region==3 || region==6 || region==11) {
            const auto before=Mission(region==11?26:56),after=Mission(region==11?28:60);
            if(!before || !after)return std::nullopt;
            if(*before && !*after)return false;
        }
        return skip_quota?Truth{true}:Quota(region);
    }
    Truth ActiveSlots() const {
        constexpr int header=1310750+13321;
        if(!Header(header,4))return std::nullopt;
        int count32=0;
        for(int i=0;i<4;++i) {
            const auto event=Int(header+1+i*9);
            if(!event || *event < -1 || *event>=120)return std::nullopt;
            if(*event<0)continue;
            const auto flags=Int(1310750+1+*event*111);
            if(!flags)return std::nullopt;
            const auto bits=static_cast<std::uint32_t>(*flags);
            if(bits&(1u<<31))return false;
            if(bits&32u)++count32;
        }
        return count32<2;
    }
    Truth Common(Snapshot& out) const {
        const auto mode=Int(1572887+12),main=Int(kConfig),flags=Int(kConfig+1);
        const auto time_mask=Int(kConfig+38),weather=Int(kConfig+39);
        if(!mode || !main || !flags || !time_mask || !weather)return std::nullopt;
        if(*mode!=-1)return false;
        // Unknown configuration is not evidence of permission. Do not silently
        // ignore another mod's new weather, mission or event-category rules.
        if(*main!=1073745953 || *flags!=1 || *time_mask!=8192 || *weather!=-1)return std::nullopt;
        const auto bits=static_cast<std::uint32_t>(*flags);
        if(bits&(1u<<19))return false;
        const auto enabled=Bool(kEventState+7),ended=Bool(kEventState+6),running=Bool(kConfig+48);
        const auto state=Int(kEventState+5);
        if(!enabled || !ended || !running || !state || *state<0 || *state>7)return std::nullopt;
        if(!*enabled || *ended || *running || *state==7)return false;
        const auto prereq=Int(kConfig+4),stop=Int(kConfig+5),rcm_start=Int(kConfig+6),rcm_stop=Int(kConfig+7);
        if(!prereq || !stop || !rcm_start || !rcm_stop || *rcm_start!=-1 || *rcm_stop!=-1)return std::nullopt;
        if(*prereq!=-1) {const auto done=Mission(*prereq);if(!done || !*done)return done;}
        if(*stop!=-1) {const auto done=Mission(*stop);if(!done)return std::nullopt;if(*done)return false;}
        const auto slots=ActiveSlots();if(!slots || !*slots)return slots;
        if(bits&(1u<<14)) {
            const auto blocked=Int(1894899);if(!blocked)return std::nullopt;
            if(static_cast<std::uint32_t>(*blocked)&(1u<<9))return false;
        }
        out.ignore_human_cargo=(bits&(1u<<20))!=0 || *state==1;
        if(!out.ignore_human_cargo) {
            out.carried=Int(1935630+32);out.stowed=Int(1935630+33);out.lasso_active=Int(1935630+30);
            if(!out.carried || !out.stowed || !out.lasso_active ||
                (*out.lasso_active!=0 && *out.lasso_active!=1))return std::nullopt;
        }
        const auto allowed=Int(kConfig+34);
        if(!allowed)return std::nullopt;
        for(int region=0;region<17;++region)
            out.regions[region]=Region(region,static_cast<std::uint32_t>(*allowed),(bits&(1u<<15))!=0);
        return true;
    }
    Snapshot Capture() const {
        Snapshot out;
        if(!api.Supported())return out;
        out.supported=true;
        if(Header(40+1095+1,7)) {
            const auto slot=Int(40+1095);
            if(slot && *slot>=0 && *slot<7) {
                const auto rank=Int(1509+*slot*436);
                if(rank && *rank>=0 && *rank<=4)out.bonding=*rank>=3;
            }
        }
        if(Header(kEvents,120)) {
            const auto raw=Int(kEventState+4);
            if(raw)out.cooldown_steps=CooldownSteps(*raw);
        }
        const auto deadline=Int(1327479+4);
        if(deadline && *deadline>=0)out.shared_deadline=deadline;
        out.player_region=Int(1897952+41);
        if(Header(kUsed,29)) {
            const auto bits=Int(kUsed+1+8);
            if(bits)out.used=static_cast<std::uint32_t>(*bits);
        }
        out.history_ok=History();
        if(Header(1310750,120) && Header(kEvents,120))out.common_ok=Common(out);
        return out;
    }
};
inline bool Candidate(const Snapshot& state,int variant,int region,bool player_ok) {
    if(!player_ok || !Yes(state.supported) || !Yes(state.history_ok) || !Yes(state.common_ok) ||
        !state.used || variant<0 || variant>=11 || region<0 || region>=17 || !state.player_region ||
        *state.player_region<0 || *state.player_region>=17)return false;
    return Yes(state.regions[*state.player_region]) && Yes(state.regions[region]) &&
        ((*state.used&(1u<<((259+variant)%31)))==0);
}
} // namespace horse_races::native_encounter
