#pragma once
#include "race_bgm_resource_identity.h"
#include "race_bgm_native_music_profiles.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

// Original StartTrack request path (317302C -> 3170200 -> 316F4A4).
// Uses the engine's existing music WaveSlot, whose real arena has 3 MiB buffers.
// No audio replacement, code patch, shared REL edit, or invented buffer capacity.
namespace horse_races::bgm::native_music {
enum class Status { Pending, Playing, Fading, Finished, Replaced, Failed };
namespace resources=audio::resources;
inline constexpr std::uintptr_t music_registry=0x5b87450;
inline constexpr std::uintptr_t manager_pointer=0x5c7d2b8;
struct Request {
    std::uint32_t source=0,pad04=0;
    std::uintptr_t mood=0;
    float blend=0;
    std::int32_t fade_in=0,fade_out=0;
    float offset_scalar=0,delay=0;
    bool override_flags=true,flag_value=false;
    std::uint16_t pad26=0;
    std::uint32_t script=0,pad2c=0;
    std::uintptr_t range=0,identity=0;
};
static_assert(sizeof(Request)==0x40 && offsetof(Request,identity)==0x38 && offsetof(Request,script)==0x28);
// Native queue command2 applies the mood and its dB level together. StartTrack's
// new-source command1 does not carry Request.blend: source creation supplies0.
// Queue this directly after StartTrack in the same script turn, before yielding.
struct GainCommand {
    std::uint32_t type=2,pad04=0;
    std::uintptr_t mood=0;
    float gain_db=0;
    std::array<unsigned char,52> unused{};
};
static_assert(sizeof(GainCommand)==0x48 && offsetof(GainCommand,mood)==8 && offsetof(GainCommand,gain_db)==0x10);
struct ResourceCheck {
    std::string_view reason="none";
    std::uint32_t name=0,offset=0,expected=0,actual=0;
    int child=-1,leaf=-1;
    bool operator==(const ResourceCheck&) const=default;
};
struct RootState {
    std::uint32_t started_at=0;
    unsigned char flags=0,type=0;
    bool Playing() const {return (type&0xc0)==0x80;}
};
struct Token {
    TrackKind kind=TrackKind::Venom;
    Status status=Status::Failed;
    std::string_view reason="unprepared";
    std::uintptr_t manager=0,context=0,root=0,identity=0;
    std::uint32_t script=0,source=0,length_ms=0;
    std::uint32_t root_started_at=0;
    bool root_started=false;
    std::uintptr_t observed_root=0;
    int observed_state=0;
    RootState observed;
    int offset_ms=0;
    float scalar=0,gain_db=0;
    resources::Identity source_record,mood,mix;
    ResourceCheck resource_check;
};
// E60 is an opaque equality identity in the original manager. It is never
// dereferenced by this music class. A fresh nonce prevents Token address reuse
// from turning an uncancelled old request into ownership of a later programme.
inline std::atomic<std::uintptr_t> next_identity{0x544a430000000001ULL};
inline bool OffsetScalar(std::uint32_t length,int offset,float& scalar) {
    if(!length || offset<0 || static_cast<std::uint32_t>(offset)>=length)return false;
    scalar=static_cast<float>(offset)/static_cast<float>(length);
    // The native performs this exact float multiplication, then truncates.
    // Advance by one float ULP if needed to reproduce the requested integer ms.
    for(int i=0;i<4 && static_cast<int>(static_cast<float>(length)*scalar)<offset;++i)
        scalar=std::nextafter(scalar,1.0F);
    return std::isfinite(scalar) && scalar>=0 && scalar<1 &&
        static_cast<int>(static_cast<float>(length)*scalar)==offset;
}
struct Snapshot {
    std::uintptr_t root=0,mood=0,pending_mood=0,identity=0,context=0;
    std::uint32_t source=0,pending=0,deferred=0,script=0;
    int write=0,read=0,count=0,state=0;
    float delayed_stop=-1;
    bool operator==(const Snapshot&) const=default;
};
template<class Memory> struct Inspection {
    const Memory& memory;
    std::uintptr_t base=0;
    template<class T> bool Read(std::uintptr_t p,T& v) const {return memory.Read(p,v);}
    resources::Inspection<Memory> Registry(std::uintptr_t rva) const {return {memory,base,rva};}
    bool Find(std::uintptr_t registry,std::uint32_t name,std::uint32_t size,resources::Identity& out) const {
        return Registry(registry).Find(name,size,out);
    }
    bool SnapshotAt(std::uintptr_t manager,Snapshot& s) const {
        return manager>=0x10000 && Read(manager+0x6a0,s.root) && Read(manager+0xe08,s.source) &&
            Read(manager+0xe10,s.pending) && Read(manager+0xe14,s.script) && Read(manager+0xe18,s.deferred) &&
            Read(manager+0xe60,s.identity) && Read(manager+0xe68,s.mood) && Read(manager+0xe80,s.pending_mood) &&
            Read(manager+0xdc8,s.context) && Read(manager+0x690,s.write) && Read(manager+0x694,s.read) &&
            Read(manager+0x698,s.count) && Read(manager+0x1c08,s.state) && Read(manager+0x1b78,s.delayed_stop) &&
            s.write>=0 && s.write<16 && s.read>=0 && s.read<16 && s.count>=0 && s.count<=16 &&
            std::isfinite(s.delayed_stop);
    }
    bool Resources(const Token& t) const {
        return Registry(resources::table_rva).Same(t.source_record) &&
            Registry(music_registry).Same(t.mood) && Registry(music_registry).Same(t.mix);
    }
    bool Hex(std::uintptr_t at,std::string_view hex,std::size_t skip=0xffffffffU,std::size_t skipped=0) const {
        auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for(std::size_t i=0;i<hex.size()/2;++i) {
            if(i>=skip && i-skip<skipped)continue;
            unsigned char v=0;
            if(!Read(at+i,v) || v!=digit(hex[i*2])*16+digit(hex[i*2+1]))return false;
        }
        return true;
    }
    bool ResourceFailure(ResourceCheck* out,std::string_view reason,std::uint32_t name,int child=-1,int leaf=-1,
        std::uint32_t offset=0,std::uint32_t expected=0,std::uint32_t actual=0) const {
        if(out)*out={reason,name,offset,expected,actual,child,leaf};return false;
    }
    bool RecordBytes(std::uintptr_t at,std::string_view hex,ResourceCheck* out,std::string_view reason,
        std::uint32_t name,int child=-1,int leaf=-1,std::size_t skip=0xffffffffU,std::size_t skipped=0) const {
        auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
        for(std::size_t i=0;i<hex.size()/2;++i) {
            if(i>=skip && i-skip<skipped)continue;
            const auto expected=static_cast<std::uint32_t>(digit(hex[i*2])*16+digit(hex[i*2+1]));unsigned char actual=0;
            if(!Read(at+i,actual))return ResourceFailure(out,"resource_read",name,child,leaf,static_cast<std::uint32_t>(i),expected);
            if(actual!=expected)return ResourceFailure(out,reason,name,child,leaf,static_cast<std::uint32_t>(i),expected,actual);
        }
        return true;
    }
    bool MusicRecordBytes(std::uintptr_t at,std::string_view hex,ResourceCheck* out,std::string_view reason,
        std::uint32_t name,bool mood) const {
        // Dat325's low byte is the record type; the upper24 bits of its first
        // word are a name-table offset, consumed only by252B480 for labels.
        // Real loaded records can relocate/rebuild that name table. Bind still
        // proves namehash->exact raw identity; compare every behavior byte.
        // Do not apply this rule to dat54 sound headers (which contain flags).
        if(hex.size()<8 || !RecordBytes(at,hex.substr(0,2),out,reason,name))return false;
        if(!RecordBytes(at+4,hex.substr(8),out,reason,name,-1,-1,mood?148U:0xffffffffU,mood?4U:0U)) {
            if(out)out->offset+=4;return false;
        }
        return true;
    }
    bool Profiles(const Token& t,ResourceCheck* why=nullptr) const {
        const auto* profile=ProfileFor(t.kind);
        if(!profile || t.source!=profile->source)return ResourceFailure(why,"resource_profile",t.source);
        std::uint32_t linked=0;
        if(!Registry(resources::table_rva).Same(t.source_record))return ResourceFailure(why,"resource_source_identity",t.source);
        if(!Registry(music_registry).Same(t.mood))return ResourceFailure(why,"resource_mood_identity",t.mood.name);
        if(!Registry(music_registry).Same(t.mix))return ResourceFailure(why,"resource_mix_identity",t.mix.name);
        if(!SourceProfile(t,profile->root,profile->children,why))return false;
        if(!MusicRecordBytes(t.mood.raw,profile->mood_bytes,why,"resource_mood_bytes",t.mood.name,true) ||
            !MusicRecordBytes(t.mix.raw,profile->mix_bytes,why,"resource_mix_bytes",t.mix.name,false))return false;
        if(!Read(t.mood.raw+152,linked))return ResourceFailure(why,"resource_mood_link_read",t.mood.name);
        if(!Registry(music_registry).LinkedEquals(t.mix,linked))return ResourceFailure(why,"resource_mood_link",t.mix.name,-1,-1,152,t.mix.linked,linked);
        return true;
    }
    bool SourceProfile(const Token& t,const audio::RawResource& root,std::span<const audio::StereoResource> children,ResourceCheck* why=nullptr) const {
        const auto registry=Registry(resources::table_rva);
        if(!RecordBytes(t.source_record.raw,root.raw.substr(0,52),why,"resource_source_bytes",root.hash))return false;
        for(std::size_t i=0;i<children.size();++i) {
            const auto& stereo=children[i];resources::Identity child;std::uint32_t linked=0;
            if(!Find(resources::table_rva,stereo.record.hash,27,child))return ResourceFailure(why,"resource_stereo_lookup",stereo.record.hash,static_cast<int>(i));
            if(!RecordBytes(child.raw,stereo.record.raw.substr(0,38),why,"resource_stereo_bytes",stereo.record.hash,static_cast<int>(i)))return false;
            if(!Read(t.source_record.raw+26+i*4,linked))return ResourceFailure(why,"resource_source_link_read",stereo.record.hash,static_cast<int>(i));
            if(!registry.LinkedEquals(child,linked))return ResourceFailure(why,"resource_source_link",stereo.record.hash,static_cast<int>(i),-1,static_cast<std::uint32_t>(26+i*4),child.linked,linked);
            for(std::size_t j=0;j<2;++j) {
                const auto& expected_leaf=stereo.leaves[j];resources::Identity leaf;resources::PackIdentity pack;
                if(!Find(resources::table_rva,expected_leaf.hash,66,leaf))return ResourceFailure(why,"resource_leaf_lookup",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j));
                if(!RecordBytes(leaf.raw,expected_leaf.raw,why,"resource_leaf_bytes",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j),13,4))return false;
                if(!Read(child.raw+19+j*4,linked))return ResourceFailure(why,"resource_stereo_link_read",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j));
                if(!registry.LinkedEquals(leaf,linked))return ResourceFailure(why,"resource_stereo_link",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j),static_cast<std::uint32_t>(19+j*4),leaf.linked,linked);
                auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};std::uint32_t source_pack=0,actual_pack=0;
                for(unsigned k=0;k<4;++k)source_pack|=static_cast<std::uint32_t>(digit(expected_leaf.raw[(13+k)*2])*16+
                    digit(expected_leaf.raw[(13+k)*2+1]))<<(k*8);
                if(!Read(leaf.raw+13,actual_pack))return ResourceFailure(why,"resource_pack_read",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j));
                if(!registry.BindPack(source_pack,actual_pack,pack))return ResourceFailure(why,"resource_pack_identity",expected_leaf.hash,static_cast<int>(i),static_cast<int>(j),13,source_pack,actual_pack);
            }
        }
        return true;
    }
    bool QueueOwned(const Token& t,const Snapshot& s) const {
        for(int i=0;i<s.count;++i) {
            const auto at=t.manager+0x210+static_cast<std::uintptr_t>((s.read+1+i)%16)*0x48;
            std::uint32_t type=0,source=0;std::uintptr_t mood=0;
            if(!Read(at,type))return false;
            // StartTrack queues wait-for-old-source4 then start1; our exact
            // gain2 follows in that same FIFO. Other mood commands are foreign.
            if(type==4)continue;
            if(type==2) {
                GainCommand command;
                if(!Read(at,command) || command.pad04 || command.mood!=t.mood.raw ||
                    !std::isfinite(command.gain_db) || command.gain_db!=t.gain_db)return false;
                for(const auto value:command.unused)if(value)return false;
                continue;
            }
            if(type!=1 || !Read(at+8,source) || source!=t.source || !Read(at+0x10,mood) || mood!=t.mood.raw)return false;
        }
        return !s.deferred && s.delayed_stop<0;
    }
    bool Root(const Token& t,std::uintptr_t root,RootState& state) const {
        std::uintptr_t table=0,raw=0;
        return root>=0x10000 && Read(root,table) && table==base+0x360f128 &&
            Read(root+0x60,raw) && raw==t.source_record.raw && Read(root+0x8c,state.started_at) &&
            Read(root+0xee,state.flags) && Read(root+0xf2,state.type) && (state.type&0x3f)==8;
    }
    bool SameRoot(const Token& t,std::uintptr_t root,const RootState& state) const {
        return (!t.root || root==t.root) && (!t.root_started || state.started_at==t.root_started_at);
    }
    bool BankUnused(const Token& t) const {
        // Losing E60 ownership does not retire the old sound. A replacement
        // StartTrack can change the nonce while command4 still waits for the
        // old root to finish. Observe all native source references instead.
        if(!t.manager || !t.source || !t.source_record.raw)return false;
        auto unused=[&](Snapshot& snapshot,std::uintptr_t& root_raw)->bool {
            if(!SnapshotAt(t.manager,snapshot) || snapshot.source==t.source ||
                snapshot.pending==t.source || snapshot.deferred==t.source)return false;
            if(snapshot.root && !Read(snapshot.root+0x60,root_raw))return false;
            if(root_raw==t.source_record.raw)return false;
            for(int i=0;i<snapshot.count;++i) {
                const auto at=t.manager+0x210+static_cast<std::uintptr_t>((snapshot.read+1+i)%16)*0x48;
                std::uint32_t type=0,source=0;
                if(!Read(at,type) || type>5)return false;
                if(type==1 && (!Read(at+8,source) || source==t.source))return false;
            }
            return true;
        };
        std::uintptr_t manager=0,again_manager=0,raw=0,again_raw=0;
        if(!Read(base+manager_pointer,manager))return false;
        if(manager!=t.manager)
            return Read(base+manager_pointer,again_manager) && again_manager==manager;
        Snapshot before,after,last;
        return unused(before,raw) && unused(after,again_raw) && before==after && raw==again_raw &&
            SnapshotAt(t.manager,last) && last==after &&
            Read(base+manager_pointer,again_manager) && again_manager==manager;
    }
    Status Poll(Token& t) const {
        auto result=[&](Status status,std::string_view reason){t.status=status;t.reason=reason;return status;};
        if(t.status==Status::Finished || t.status==Status::Replaced || t.status==Status::Failed)return t.status;
        std::uintptr_t manager=0;Snapshot s;
        if(!Read(base+manager_pointer,manager) || manager!=t.manager || !SnapshotAt(manager,s))
            return result(Status::Replaced,"manager_changed");
        t.observed_root=s.root;t.observed_state=s.state;t.observed={};
        if(s.context!=t.context || !Resources(t))return result(Status::Replaced,"resource_changed");
        if(t.status==Status::Fading) {
            if(s.identity || s.source || s.mood || s.pending || s.deferred || s.count || s.delayed_stop>=0)
                return result(Status::Replaced,"fade_replaced");
            if(!s.root)return result(Status::Finished,"fade_finished");
            if(s.root!=t.root || !Root(t,s.root,t.observed) || !SameRoot(t,s.root,t.observed))
                return result(Status::Replaced,"fade_root_changed");
            return result(Status::Fading,"fading");
        }
        if(!s.identity && !s.root && !s.count && !s.pending && !s.source && !s.deferred)
            return result(t.root?Status::Finished:Status::Failed,t.root?"finished":"native_create_failed");
        if(s.identity!=t.identity || !QueueOwned(t,s))return result(Status::Replaced,"request_replaced");
        if(s.source==t.source && s.root) {
            if(s.mood!=t.mood.raw || s.script!=0xffffffffU)return result(Status::Replaced,"active_owner_changed");
            if(!Root(t,s.root,t.observed))return result(Status::Replaced,"root_changed");
            if(!SameRoot(t,s.root,t.observed))return result(Status::Replaced,"root_lifetime_changed");
            t.root=s.root;
            if(t.observed.flags&4)return result(Status::Failed,"native_error");
            if(s.state==3 && t.observed.Playing()) {
                float applied=0;
                if(!Read(t.manager+0x1bec,applied) || !std::isfinite(applied) || applied!=t.gain_db)
                    return result(Status::Failed,"gain_not_applied");
                // +9C is a lazily allocated parameter-map index (255FDE4),
                // not a source lifetime. +8C is written by native Start; bind
                // it only after the sound itself enters the active 80 state.
                t.root_started_at=t.observed.started_at;t.root_started=true;
                return result(Status::Playing,"playing");
            }
            return result(Status::Pending,"preparing_source");
        }
        if(s.pending==t.source && s.pending_mood==t.mood.raw && s.script==t.script)
            return result(Status::Pending,"queued");
        if(!s.root && !s.count && !s.pending && !s.source)
            return result(t.root?Status::Finished:Status::Failed,t.root?"finished":"native_create_failed");
        return result(Status::Replaced,"source_changed");
    }
};

template<class Memory,class Calls> struct Bridge {
    Memory memory;
    Calls calls;
    std::uintptr_t base=0;
    Inspection<Memory> View() const {return {memory,base};}
    bool Prepare(TrackKind kind,Token& t) const {
        if(t.identity && (t.status==Status::Pending || t.status==Status::Playing || t.status==Status::Fading)) {
            t.reason="token_in_use";return false;
        }
        t={};t.kind=kind;
        auto fail=[&](std::string_view why){t.reason=why;return false;};
        if(!calls.Supported())return fail("signatures");
        const auto view=View();const auto* profile=ProfileFor(kind);
        if(!profile)return fail("profile");
        t.source=profile->source;
        if(!view.Find(resources::table_rva,t.source,static_cast<std::uint32_t>(profile->root.raw.size()/2),t.source_record)) {
            t.resource_check={"resource_source_lookup",t.source};return fail(t.resource_check.reason);
        }
        const std::uint32_t mood_name=profile->mood,mix_name=profile->mix;
        if(!view.Find(music_registry,mood_name,184,t.mood)) {
            t.resource_check={"resource_mood_lookup",mood_name};return fail(t.resource_check.reason);
        }
        if(!view.Find(music_registry,mix_name,28,t.mix)) {
            t.resource_check={"resource_mix_lookup",mix_name};return fail(t.resource_check.reason);
        }
        if(!view.Profiles(t,&t.resource_check))return fail(t.resource_check.reason);
        std::uintptr_t header=0;std::uint32_t total=0,reserve=0;unsigned char flags=0;
        if(!view.Read(base+manager_pointer,t.manager) || !view.Read(t.manager+0xdc8,t.context) ||
            !view.Read(t.context+8,header) || !view.Read(header+0x14,total) || !view.Read(header+0x1c,reserve) ||
            total<reserve || (total-reserve)/2<0x300000 || !view.Read(t.context+0xb5,flags) || !(flags&4))return fail("music_context");
        std::uint32_t authored_length=0;
        if(!calls.Length(t.source,t.length_ms) || !t.length_ms || t.length_ms>3600000 ||
            !view.Read(t.source_record.raw+20,authored_length) || t.length_ms!=authored_length)return fail("source_length");
        t.status=Status::Pending;t.reason="prepared";return true;
    }
    bool Start(Token& t,std::uint32_t script,int offset_ms,float gain_db=0,bool replace_active=true) const {
        auto fail=[&](std::string_view why){t.status=Status::Failed;t.reason=why;return false;};
        const auto available=[&](const Snapshot& snapshot) {
            if(replace_active)return true;
            const bool pending=snapshot.count || snapshot.pending || snapshot.deferred || snapshot.delayed_stop>=0;
            if(!pending && !snapshot.source && !snapshot.root)return true;
            t.status=Status::Replaced;
            t.reason=pending?"programme_takeover_pending":"programme_takeover_active";
            return false;
        };
        if(t.reason!="prepared" || t.identity || !calls.Supported())return fail("not_prepared");
        if(!std::isfinite(gain_db) || gain_db < -100 || gain_db > 6)return fail("request_gain");
        const auto view=View();Snapshot before;
        std::uintptr_t current=0;unsigned char overrides=0;
        if(!view.Read(base+manager_pointer,current) || current!=t.manager || !view.SnapshotAt(t.manager,before) ||
            before.context!=t.context || !view.Profiles(t))return fail("resource_changed");
        t.observed_root=before.root;t.observed_state=before.state;t.observed={};
        if(before.source==t.source && before.root)view.Root(t,before.root,t.observed);
        if(!available(before))return false;
        // A competing queued/deferred request is not ours to cancel. Active
        // music can be replaced using the exact normal StartTrack semantics.
        if(before.count || before.pending || before.deferred || before.delayed_stop>=0)return fail("queue_busy");
        if(before.source==t.source)return fail("same_source_active");
        if(!view.Read(t.manager+0x1398,overrides) || overrides)return fail("music_fade_override");
        if(!OffsetScalar(t.length_ms,offset_ms,t.scalar))return fail("offset_precision");
        t.offset_ms=offset_ms;t.script=script;t.gain_db=gain_db;
        Request request;request.source=t.source;request.mood=t.mood.raw;request.script=script;
        // Request.blend is consumed only by the same-source fast path, which
        // this adapter intentionally never takes. The new-source FIFO needs2.
        request.blend=0;
        request.offset_scalar=t.scalar;
        // Original SetMood action's 7500/10000ms transitions are not used:
        // this is a StartTrack request with fade_in=fade_out=delay=0.
        if(!replace_active) {
            // Resource/offset checks can take place after the invitation's
            // last poll. Check again immediately before submitting a later
            // piece; a completed predecessor grants no right to a new score.
            Snapshot latest;
            if(!view.Read(base+manager_pointer,current) || current!=t.manager ||
                !view.SnapshotAt(t.manager,latest) || latest.context!=t.context)return fail("resource_changed");
            if(!available(latest))return false;
        }
        t.identity=next_identity.fetch_add(1,std::memory_order_relaxed);
        if(!t.identity)return fail("identity_exhausted");
        request.identity=t.identity;
        if(!calls.Start(t.manager,request))return fail("native_call");
        Snapshot queued,again,posted;
        if(!view.Read(base+manager_pointer,current) || current!=t.manager ||
            !view.SnapshotAt(t.manager,queued) || queued.identity!=t.identity || queued.context!=t.context ||
            queued.pending!=t.source || queued.pending_mood!=t.mood.raw || queued.script!=t.script ||
            queued.count<=0 || queued.count>=16 || !view.QueueOwned(t,queued) ||
            !view.SnapshotAt(t.manager,again) || queued!=again)return fail("gain_queue_owner");
        GainCommand command;command.mood=t.mood.raw;command.gain_db=gain_db;
        if(!calls.QueueGain(t.manager,command))return fail("gain_enqueue");
        // The native helper copies the stack command into its ring. Verify the
        // append without yielding; never treat a lost/replaced queue as ours.
        again=queued;again.count++;again.write=(again.write+1)%16;
        if(!view.Read(base+manager_pointer,current) || current!=t.manager ||
            !view.SnapshotAt(t.manager,posted) || posted!=again || !view.QueueOwned(t,posted))return fail("gain_queue_readback");
        t.status=Status::Pending;t.reason="queued";
        view.Poll(t);
        // The void native accepted the request even if its asynchronous source
        // subsequently failed. Keep the caller responsible for owned cleanup.
        return true;
    }
    Status Poll(Token& t) const {
        if(!calls.Supported()){t.status=Status::Failed;t.reason="signatures";return t.status;}
        if(!t.identity && t.reason=="prepared")return Status::Pending;
        return View().Poll(t);
    }
    bool BankUnused(const Token& t) const {return calls.Supported() && View().BankUnused(t);}
    bool Stop(Token& t,int fade_ms) const {
        if(fade_ms<0 || fade_ms>8000 || !calls.Supported())return false;
        if(t.status==Status::Finished)return true;
        if(!t.identity && t.reason=="prepared"){t.status=Status::Finished;t.reason="cancelled_prepared";return true;}
        const auto view=View();const auto state=view.Poll(t);
        if(state==Status::Finished)return true;
        if(state==Status::Fading){t.reason="fade_already_scheduled";return false;}
        if(state!=Status::Pending && state!=Status::Playing && state!=Status::Failed)return false;
        Snapshot before,again;std::uintptr_t current_manager=0;
        auto replaced=[&](std::string_view why){t.status=Status::Replaced;t.reason=why;return false;};
        if(!view.Read(base+manager_pointer,current_manager)){t.reason="stop_unreadable";return false;}
        if(current_manager!=t.manager)return replaced("manager_changed");
        if(!view.SnapshotAt(t.manager,before)){t.reason="stop_unreadable";return false;}
        if(before.context!=t.context || !view.Resources(t))return replaced("resource_changed");
        if(before.identity!=t.identity || !view.QueueOwned(t,before))return replaced("request_replaced");
        if(!view.SnapshotAt(t.manager,again) || before!=again){t.reason="stop_raced";return false;}
        if(state==Status::Failed) {
            if(before.source==t.source && before.mood==t.mood.raw && before.script==0xffffffffU && before.root) {
                RootState root;
                if(!view.Root(t,before.root,root) || !view.SameRoot(t,before.root,root))return false;
                t.root=before.root;
            } else if(!before.root && !before.source && !before.pending && !before.count) {
                // Creation failed after the native accepted our identity.
            } else if(before.pending!=t.source || before.pending_mood!=t.mood.raw || before.script!=t.script)return false;
        }
        if(!calls.Stop(t.manager,fade_ms)){t.reason="native_stop";return false;}
        t.status=t.root?Status::Fading:Status::Finished;t.reason=t.root?"fading":"cancelled_queued";
        return true;
    }
};
} // namespace horse_races::bgm::native_music

#ifdef _WIN32
#include "race_bgm_native_audio.h"
namespace horse_races::bgm::native_music {
struct WindowsCalls {
    static std::uintptr_t Base(){return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));}
    bool Supported() const {
        static const bool supported=[] {
            const audio::WindowsMemory memory;const Inspection<audio::WindowsMemory> view{memory,Base()};
            if(!audio::WindowsBridge::ResourcesSupported())return false;
            for(const auto& signature:music_signatures)if(!view.Hex(Base()+signature.rva,signature.hex))return false;
            return true;
        }();
        return supported;
    }
    bool Length(std::uint32_t source,std::uint32_t& length) const {
        using Function=const std::uint32_t*(__fastcall*)(void*,std::uint32_t);
        __try {
            const auto data=reinterpret_cast<Function>(Base()+0x2515e24)(reinterpret_cast<void*>(Base()+0x5342010),source);
            return data && audio::WindowsMemory{}.Read(reinterpret_cast<std::uintptr_t>(data),length);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Start(std::uintptr_t manager,const Request& request) const {
        using Function=void(__fastcall*)(void*,const Request*);
        __try {reinterpret_cast<Function>(Base()+0x3170200)(reinterpret_cast<void*>(manager),&request);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool QueueGain(std::uintptr_t manager,const GainCommand& command) const {
        using Function=bool(__fastcall*)(void*,const GainCommand*);
        __try {return reinterpret_cast<Function>(Base()+0x31714e4)(reinterpret_cast<void*>(manager+0x210),&command);}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Stop(std::uintptr_t manager,int fade_ms) const {
        using Function=void(__fastcall*)(void*,int,float,void*,bool);
        __try {reinterpret_cast<Function>(Base()+0x3172618)(reinterpret_cast<void*>(manager),fade_ms,-1.0F,nullptr,false);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
struct WindowsBridge {
    static auto View(){return Bridge<audio::WindowsMemory,WindowsCalls>{{},{},WindowsCalls::Base()};}
    static bool Prepare(TrackKind kind,Token& token){return View().Prepare(kind,token);}
    static bool Start(Token& token,std::uint32_t script,int offset_ms,float gain_db=0,bool replace_active=true){return View().Start(token,script,offset_ms,gain_db,replace_active);}
    static Status Poll(Token& token){return View().Poll(token);}
    static bool BankUnused(const Token& token){return View().BankUnused(token);}
    static bool Stop(Token& token,int fade_ms){return View().Stop(token,fade_ms);}
};
}
#endif
