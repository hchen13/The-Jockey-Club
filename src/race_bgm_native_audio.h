#pragma once
#include "race_bgm_audio_signatures.h"
#include "race_bgm_resource_identity.h"
#include "data/race_bgm_vocal_resources.h"
#include <windows.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <array>

// Two narrow operations missing from the public natives: an owned frontend
// sound with a start cursor, and gain on verified owned camp singing instances.
// No code hooks, shared REL changes, audio files, workers or global mix changes.
namespace horse_races::bgm::audio {
constexpr std::uintptr_t kManager=0x5c81c20;
struct PoolChild {
    std::uintptr_t pool=0,start=0,child=0;
    std::uint32_t stride=0;
    std::uint16_t index=0xffff;
    unsigned char pool_index=0;
    bool operator==(const PoolChild&) const=default;
};
struct SimpleBinding {
    PoolChild slot;
    std::uintptr_t raw=0;
    resources::Identity identity;
    resources::PackIdentity pack;
    bool operator==(const SimpleBinding&) const=default;
};
struct StereoBinding {
    PoolChild slot;
    std::uintptr_t raw=0;
    resources::Identity identity;
    std::array<SimpleBinding,2> leaves;
    bool operator==(const StereoBinding&) const=default;
};
struct SoundOwner {
    int id=-1,context=-1;
    std::uint32_t thread=0,cue=0;
    std::uint64_t source=0;
    std::uintptr_t root=0,raw=0;
    resources::Identity identity;
    StereoBinding stereo;
    bool active=false;
    bool operator==(const SoundOwner&) const=default;
};
struct StreamGain {
    int id=-1,slot=-1,context=-1;
    std::uint32_t thread=0;
    std::uint64_t source=0;
    std::uintptr_t root=0,raw=0;
    resources::Identity identity;
    SimpleBinding child;
    std::string_view track_id;
    std::string_view failure;
    float before=0,after=0;
    bool applied=false;
};
struct StemSelection {
    int id=-1,slot=-1,context=-1;
    std::uint32_t thread=0,stride=0;
    std::uint64_t source=0;
    std::uintptr_t root=0,raw=0,pool=0,start=0;
    unsigned char pool_index=0;
    std::array<std::uint16_t,12> indices{};
    std::array<std::uintptr_t,12> children{},records{};
    resources::Identity identity;
    std::array<StereoBinding,12> stereo{};
    std::array<bool,12> changed{};
    bool applied=false;
    bool operator==(const StemSelection&) const=default;
};
struct StemCheck {std::string_view reason="none";int child=-1;};

// The reader is injectable so wrong/reused IDs, changed owners, malformed
// resources and compare-before-restore are exercised without a live process.
template<class Memory> struct Inspection {
    Memory memory;
    std::uintptr_t base=0;
    template<class T> bool Read(std::uintptr_t at,T& out) const{return memory.Read(at,out);}
    bool Hex(std::uintptr_t at,std::string_view hex) const {
        if(hex.size()%2)return false;
        auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for(std::size_t i=0;i<hex.size();i+=2){unsigned char value=0;
            if(!Read(at+i/2,value) || value!=(digit(hex[i])*16+digit(hex[i+1])))return false;}
        return true;
    }
    template<std::size_t N> bool Signatures(const Signature (&items)[N]) const {
        for(const auto& item:items)if(!Hex(base+item.rva,item.hex))return false;
        return base!=0;
    }
    resources::Inspection<Memory> Resources() const {return {memory,base};}
    static std::uint32_t SourceWord(std::string_view hex,std::size_t at) {
        const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        std::uint32_t value=0;
        for(std::size_t i=0;i<4;++i)value|=static_cast<std::uint32_t>(digit(hex[(at+i)*2])*16+digit(hex[(at+i)*2+1]))<<(i*8);
        return value;
    }
    bool Child(std::uintptr_t parent,std::size_t i,PoolChild& out) const {
        PoolChild v;
        if(i>=16 || !Read(parent+0xa0,v.pool_index) ||
            !Read(base+0x53588d8,v.pool) || v.pool<0x10000 ||
            !Read(base+0x53588b0,v.stride) || v.stride<0x158 || v.stride>0x10000 ||
            !Read(v.pool+static_cast<std::uintptr_t>(v.pool_index)*0x2a860+0x2a848,v.start) || v.start<0x10000 ||
            !Read(parent+0xb6+i*2,v.index) || v.index==0xffff ||
            !resources::Inspection<Memory>::Span(v.start,(static_cast<std::size_t>(v.index)+1)*v.stride))return false;
        v.child=v.start+static_cast<std::uintptr_t>(v.index)*v.stride;
        out=v;return true;
    }
    bool Simple(const PoolChild& slot,const RawResource& expected,SimpleBinding& out) const {
        SimpleBinding v;v.slot=slot;
        const auto sound=slot.child;
        unsigned char type=0,flags=0;std::uintptr_t table=0;
        if(!Read(sound,table) || table!=base+0x360eb88 || !Read(sound+0xf2,type) || (type&0x3f)!=12 ||
            !Read(sound+0xef,flags) || (flags&2) || !Read(sound+0x60,v.raw))return false;
        const auto size=expected.raw.size()/2;
        // The only permitted modified bytes are the explicit pack relocation.
        const std::size_t pack_at=size==63?10:size==66?13:0;
        std::uint32_t linked_pack=0;
        if(!pack_at || !Resources().Bind(expected.hash,v.raw,static_cast<std::uint32_t>(size),v.identity) ||
            !Hex(v.raw,expected.raw.substr(0,pack_at*2)) ||
            !Hex(v.raw+pack_at+4,expected.raw.substr((pack_at+4)*2)) ||
            !Read(v.raw+pack_at,linked_pack) || !Resources().BindPack(SourceWord(expected.raw,pack_at),linked_pack,v.pack))return false;
        if(!Resources().Same(v.identity) || !Resources().Same(v.pack))return false;
        out=v;return true;
    }
    bool Stereo(const PoolChild& slot,const StereoResource& expected,StereoBinding& out) const {
        StereoBinding v;v.slot=slot;
        unsigned char type=0,flags=0;std::uintptr_t table=0;int count=0;
        if(!Read(slot.child,table) || table!=base+0x360e760 || !Read(slot.child+0xf2,type) || (type&0x3f)!=13 ||
            !Read(slot.child+0xef,flags) || (flags&2) || !Read(slot.child+0x108,count) || count!=2 ||
            !Read(slot.child+0x60,v.raw) || expected.record.raw.size()!=54 ||
            !Resources().Bind(expected.record.hash,v.raw,27,v.identity) || !Hex(v.raw,expected.record.raw.substr(0,38)))return false;
        for(std::size_t i=0;i<2;++i) {
            PoolChild child;std::uint32_t linked=0;
            if(!Child(slot.child,i,child) || !Simple(child,expected.leaves[i],v.leaves[i]) ||
                !Read(v.raw+19+i*4,linked) || !Resources().LinkedEquals(v.leaves[i].identity,linked))return false;
        }
        if(v.leaves[0].slot.child==v.leaves[1].slot.child || !Resources().Same(v.identity))return false;
        out=v;return true;
    }
    bool RootRecord(const RawResource& expected,std::uintptr_t raw,std::size_t children,resources::Identity& identity) const {
        return expected.raw.size()>=children*8 &&
            Resources().Bind(expected.hash,raw,static_cast<std::uint32_t>(expected.raw.size()/2),identity) &&
            Hex(raw,expected.raw.substr(0,expected.raw.size()-children*8));
    }
    bool Sound(int id,SoundOwner& out,bool active,std::uint32_t cue) const {
        if(id<0 || id>=100)return false;
        SoundOwner v;v.id=id;v.active=active;
        const auto slot=base+kManager+0x5d0+id*0x80;
        unsigned char owned=0;
        if(!Read(slot+0x40,v.context) || v.context<0 || v.context>=32 ||
            !Read(slot+0x44,owned) || owned!=1 ||
            !Read(base+kManager+0x37d0+v.context*0x380,v.thread))return false;
        if(active) {
            std::uint32_t set=0;std::uintptr_t table=0;
            if(!Read(slot,set) || set || !Read(slot+4,v.cue) || v.cue!=cue ||
                !Read(slot+0xc,v.source) || !Read(slot+0x18,v.root) || v.root<0x10000 ||
                !Read(v.root,table) || table!=base+0x360f128 || !Read(v.root+0x60,v.raw) || v.raw<0x10000 ||
                cue!=credits_resource.root.hash || !RootRecord(credits_resource.root,v.raw,1,v.identity))return false;
            int count=0;unsigned char type=0,flags=0;PoolChild child;std::uint32_t linked=0;
            if(!Read(v.root+0xf2,type) || (type&0x3f)!=8 || !Read(v.root+0xef,flags) || (flags&2) ||
                !Read(v.root+0x154,count) || count!=1 || !Child(v.root,0,child) ||
                !Stereo(child,credits_resource.children[0],v.stereo) || !Read(v.raw+26,linked) ||
                !Resources().LinkedEquals(v.stereo.identity,linked) || !Resources().Same(v.identity))return false;
        }
        out=v;return true;
    }
    bool Owns(const SoundOwner& owner) const {
        SoundOwner current,again;
        return Sound(owner.id,current,owner.active,owner.cue) && current==owner &&
            Sound(owner.id,again,owner.active,owner.cue) && again==owner;
    }
    bool Stream(int id,StreamGain& out,bool ready_only,std::string_view track_id,std::string_view* why=nullptr) const {
        auto fail=[&](std::string_view reason){if(why)*why=reason;return false;};
        if(id<0)return fail("stream_tag_or_state");
        const VocalResource* resource=nullptr;
        for(const auto& entry:vocal_resources)if(entry.track_id==track_id){resource=&entry;break;}
        if(!resource)return fail("track_allowlist");
        int found=-1;
        for(int i=0;i<16;++i){int tag=-1;
            if(!Read(base+kManager+0xaa24+i*0x260,tag))return fail("stream_tag_or_state");
            if(tag==id){if(found>=0)return fail("stream_tag_or_state");found=i;}}
        if(found<0)return fail("stream_tag_or_state");
        StreamGain v;v.id=id;v.slot=found;v.track_id=track_id;
        const auto slot=base+kManager+found*0x260;
        int state=0,children=0;unsigned char type=0,flags=0;std::uintptr_t table=0;
        if(!Read(slot+0xaa14,state) || (state!=2 && (ready_only || state!=3)))return fail("stream_tag_or_state");
        if(!Read(slot+0xaa20,v.context) || v.context<0 || v.context>=32 ||
            !Read(base+kManager+0x37d0+v.context*0x380,v.thread) ||
            !Read(slot+0xaa18,v.source) || !Read(slot+0xa9f8,v.root) || v.root<0x10000)return fail("owner");
        if(!Read(v.root,table) || table!=base+0x360f128 ||
            !Read(v.root+0xf2,type) || (type&0x3f)!=8)return fail("parent_type");
        if(!Read(v.root+0xef,flags) || (flags&2))return fail("optimizer");
        if(!Read(v.root+0x154,children) || children!=1)return fail("childcount");
        if(!Read(v.root+0x60,v.raw) || v.raw<0x10000)return fail("raw_record");
        bool matches=false;
        for(const auto record:resource->roots)if(!record.empty() && RootRecord({resource->root_hash,record},v.raw,1,v.identity)){matches=true;break;}
        if(!matches)return fail("raw_record");
        PoolChild child;std::uint32_t linked=0;
        if(!Child(v.root,0,child) || !Simple(child,resource->leaf,v.child) ||
            !Read(v.raw+v.identity.record_size-4,linked) || !Resources().LinkedEquals(v.child.identity,linked) ||
            !Resources().Same(v.identity))return fail("child_resource");
        out=v;return true;
    }
    bool Same(const StreamGain& owner) const {
        StreamGain current;
        return Stream(owner.id,current,false,owner.track_id) && current.slot==owner.slot && current.context==owner.context &&
            current.thread==owner.thread &&
            current.source==owner.source && current.root==owner.root && current.raw==owner.raw &&
            current.identity==owner.identity && current.child==owner.child;
    }
    bool Gain(int id,float db,StreamGain& out,std::string_view track_id) const {
        auto fail=[&](std::string_view reason){out.failure=reason;return false;};
        StreamGain v;
        std::string_view why;
        if(!std::isfinite(db) || db<=0 || db>6)return fail("initial_gain");
        if(!Stream(id,v,true,track_id,&why))return fail(why);
        if(!Read(v.root+0x90,v.before) || v.before!=0)return fail("initial_gain");
        if(!Same(v))return fail("owner");
        v.after=v.before+db;
        if(!memory.WriteFloat(v.root+0x90,v.before,v.after))return fail("comparewrite");
        v.applied=true;out=v;
        if(!Verify(v))return fail("readback");
        return true;
    }
    bool Verify(const StreamGain& v) const {
        float value=0;
        return v.applied && Same(v) && Read(v.root+0x90,value) && value==v.after;
    }
    bool Restore(const StreamGain& v) const {
        return Verify(v) && Same(v) && memory.WriteFloat(v.root+0x90,v.after,v.before);
    }
    bool Stems(int id,StemSelection& out,bool ready_only,StemCheck* why=nullptr) const {
        auto fail=[&](std::string_view reason,int child=-1){if(why)*why={reason,child};return false;};
        if(id<0)return fail("stream_id");
        StemSelection v;v.id=id;
        for(int i=0;i<16;++i){int tag=-1;
            if(!Read(base+kManager+0xaa24+i*0x260,tag))return fail("stream_slot_read");
            if(tag==id){if(v.slot>=0)return fail("stream_tag_duplicate");v.slot=i;}}
        if(v.slot<0)return fail("stream_tag_missing");
        const auto slot=base+kManager+v.slot*0x260;
        int state=0,count=0;std::uintptr_t table=0;unsigned char type=0,flags=0,inherit=0;std::uint32_t secondary=0;
        if(!Read(slot+0xaa14,state) || (state!=2 && (ready_only || state!=3)))return fail("stream_state");
        if(!Read(slot+0xaa20,v.context) || v.context<0 || v.context>=32 ||
            !Read(base+kManager+0x37d0+v.context*0x380,v.thread) ||
            !Read(slot+0xaa18,v.source))return fail("stream_owner");
        if(!Read(slot+0xa9f8,v.root) || v.root<0x10000 ||
            !Read(v.root,table) || table!=base+0x360f128 || !Read(v.root+0xf2,type) || (type&0x3f)!=8)return fail("parent_type");
        if(!Read(v.root+0xef,flags) || (flags&2))return fail("parent_optimizer");
        if(!Read(v.root+0x154,count) || count!=12)return fail("parent_child_count");
        if(!Read(v.root+0x60,v.raw) || !RootRecord(venom_resource.root,v.raw,12,v.identity))return fail("parent_record");
        if(!Read(v.root+0xa0,v.pool_index) ||
            !Read(base+0x53588d8,v.pool) || v.pool<0x10000 ||
            !Read(base+0x53588b0,v.stride) || v.stride<0x158 || v.stride>0x10000 ||
            !Read(v.pool+v.pool_index*0x2a860+0x2a848,v.start) || v.start<0x10000)return fail("child_pool");
        for(std::size_t i=0;i<12;++i) {
            if(!Read(v.root+0xb6+i*2,v.indices[i]) || v.indices[i]==0xffff)return fail("child_index",static_cast<int>(i));
            v.children[i]=v.start+static_cast<std::uintptr_t>(v.indices[i])*v.stride;
            for(std::size_t j=0;j<i;++j)if(v.children[j]==v.children[i])return fail("child_duplicate",static_cast<int>(i));
            if(!Read(v.children[i],table) || table!=base+0x360e760 ||
                !Read(v.children[i]+0xf2,type) || (type&0x3f)!=13)return fail("child_type",static_cast<int>(i));
            if(!Read(v.children[i]+0xef,flags) || (flags&2))return fail("child_optimizer",static_cast<int>(i));
            if(
                !Read(v.children[i]+0xeb,inherit) || (inherit&1) ||
                !Read(v.children[i]+0x84,secondary) || (secondary&0xc0000)==0x40000)return fail("child_volume_flags",static_cast<int>(i));
            PoolChild child;std::uint32_t linked=0;
            if(!Child(v.root,i,child) || child.child!=v.children[i] ||
                !Stereo(child,venom_resource.children[i],v.stereo[i]) ||
                !Read(v.raw+26+i*4,linked) || !Resources().LinkedEquals(v.stereo[i].identity,linked))return fail("child_source",static_cast<int>(i));
            v.records[i]=v.stereo[i].raw;
        }
        if(!Resources().Same(v.identity))return fail("parent_record");
        out=v;return true;
    }
    bool Same(const StemSelection& v,bool ready_only=false) const {
        StemSelection now;
        if(!Stems(v.id,now,ready_only))return false;
        now.changed=v.changed;now.applied=v.applied;
        return now==v;
    }
    bool Verify(const StemSelection& v,StemCheck* why=nullptr) const {
        StemSelection current;
        if(!Stems(v.id,current,false,why))return false;
        if(!v.applied || !Same(v)){if(why)*why={"ownership_changed",-1};return false;}
        for(std::size_t i=0;i<12;++i){float db=0;
            if(!Read(v.children[i]+0x90,db) || db!=(i==10?0.0F:-100.0F)){if(why)*why={"gain_readback",static_cast<int>(i)};return false;}}
        return Same(v);
    }
    bool Restore(const StemSelection& v) const {
        // Never unmask a playing container. STOP_STREAM normally destroys the
        // instance, so no writes are needed or allowed after its slot clears.
        if(!Same(v,true))return false;
        bool restored=true;
        for(std::size_t i=0;i<12;++i)if(v.changed[i]) {
            if(!Same(v,true) || !memory.WriteFloat(v.children[i]+0x90,-100.0F,0.0F))restored=false;
        }
        return restored;
    }
    bool SelectStems(int id,StemSelection& out,StemCheck* why=nullptr) const {
        StemSelection v;
        if(!Stems(id,v,true,why))return false;
        // Validate the entire graph and authored gains before the first write.
        for(std::size_t i=0;i<12;++i){float db=0;if(!Read(v.children[i]+0x90,db) || db!=0){if(why)*why={"initial_gain",static_cast<int>(i)};return false;}}
        for(std::size_t i=0;i<12;++i)if(i!=10) {
            const bool owned=Same(v,true);
            if(!owned || !memory.WriteFloat(v.children[i]+0x90,0.0F,-100.0F)) {
                if(why)*why={owned?"gain_write":"ownership_changed",static_cast<int>(i)};
                Restore(v);out=v;return false;
            }
            v.changed[i]=true;
        }
        v.applied=true;out=v;
        if(Verify(v,why))return true;
        Restore(v);return false;
    }
};

struct WindowsMemory {
    template<class T> bool Read(std::uintptr_t at,T& out) const {
        __try {std::memcpy(&out,reinterpret_cast<const void*>(at),sizeof(T));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool WriteFloat(std::uintptr_t at,float expected,float value) const {
        MEMORY_BASIC_INFORMATION page{};
        if((at&3) || !VirtualQuery(reinterpret_cast<void*>(at),&page,sizeof(page)) || page.State!=MEM_COMMIT ||
            (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(page.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        __try {
            const auto previous=std::bit_cast<LONG>(expected),desired=std::bit_cast<LONG>(value);
            return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),desired,previous)==previous;
        }
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
struct WindowsBridge {
    static Inspection<WindowsMemory> View(){return {{},reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))};}
    static bool ResourcesSupported(){static const bool valid=View().Signatures(resource_signatures);return valid;}
    static bool OffsetSupported(){static const bool valid=View().Signatures(offset_signatures) && ResourcesSupported();return valid;}
    static bool GainSupported(){static const bool valid=View().Signatures(gain_signatures) && ResourcesSupported();return valid;}
    static bool StemsSupported(){static const bool valid=View().Signatures(stem_signatures) && GainSupported();return valid;}
    static bool Reserve(int id,SoundOwner& owner){return OffsetSupported() && View().Sound(id,owner,false,0);}
    static bool Capture(int id,std::uint32_t cue,SoundOwner& owner){return OffsetSupported() && View().Sound(id,owner,true,cue);}
    static bool Owns(const SoundOwner& owner){return OffsetSupported() && View().Owns(owner);}
    static bool PlayOffset(const SoundOwner& owner,const char* cue,int offset_ms) {
        if(!OffsetSupported() || !cue || offset_ms<0 || offset_ms>3600000 || !Owns(owner))return false;
        using Function=bool(__fastcall*)(void*,int,const char*,const char*,bool,int);
        const auto base=View().base;
        __try {return reinterpret_cast<Function>(base+0x31c3034)(reinterpret_cast<void*>(base+kManager),owner.id,cue,nullptr,false,offset_ms);}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Gain(int id,float db,StreamGain& owner,std::string_view track_id){
        if(!GainSupported()){owner.failure="signature";return false;}
        return View().Gain(id,db,owner,track_id);
    }
    static bool Verify(const StreamGain& owner){return GainSupported() && View().Verify(owner);}
    static bool Restore(const StreamGain& owner){return GainSupported() && View().Restore(owner);}
    static bool SelectStems(int id,StemSelection& owner,StemCheck* why=nullptr){
        if(!StemsSupported()){if(why)*why={"signatures",-1};return false;}
        return View().SelectStems(id,owner,why);
    }
    static bool Verify(const StemSelection& owner,StemCheck* why=nullptr){
        if(!StemsSupported()){if(why)*why={"signatures",-1};return false;}
        return View().Verify(owner,why);
    }
    static bool Restore(const StemSelection& owner){return StemsSupported() && View().Restore(owner);}
};
} // namespace horse_races::bgm::audio
