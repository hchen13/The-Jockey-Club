#pragma once
#include "race_bgm_native_music.h"
#include <sstream>
#include <string>

// Read-only failure evidence. No native calls, writes, decoding or recovery.
namespace horse_races::bgm::native_music::diagnostics {
inline constexpr std::uintptr_t context_pool_pointer=0x5348de0,context_stride=0x360c914;
// Event requests have no ownership token. Observe the manager without claiming
// its main/auxiliary sounds belong to the requested cue or are audible.
template<class Memory> std::string DescribeEvent(const Memory& memory,std::uintptr_t base,bool supported) {
    if(!supported || !base)return "event_native_reason=signatures";
    struct Sound {
        std::uintptr_t pointer=0,resource=0;std::uint32_t started=0;
        unsigned char flags=0,type=0;bool readable=false;
        bool operator==(const Sound&)const=default;
    };
    struct Observation {
        Snapshot manager;Sound main;std::array<Sound,2> aux;
        std::array<int,2> clocks{};std::array<unsigned char,2> retired{};
        bool operator==(const Observation&)const=default;
    };
    const Inspection<Memory> view{memory,base};std::uintptr_t manager=0,again=0;
    if(!memory.Read(base+manager_pointer,manager) || manager<0x10000)
        return "event_native_reason=manager_unavailable";
    auto sound=[&](Sound& s) {
        if(!s.pointer)return;
        if(s.pointer<0x10000)return;
        s.readable=memory.Read(s.pointer+0x60,s.resource) && memory.Read(s.pointer+0x8c,s.started) &&
            memory.Read(s.pointer+0xee,s.flags) && memory.Read(s.pointer+0xf2,s.type);
    };
    auto capture=[&](Observation& o) {
        if(!view.SnapshotAt(manager,o.manager))return false;
        o.main.pointer=o.manager.root;sound(o.main);
        for(int i=0;i<2;++i) {
            const auto entry=manager+0x6a8+i*0x390;
            if(!memory.Read(entry+8,o.aux[i].pointer) || !memory.Read(entry+0x378,o.clocks[i]) ||
               !memory.Read(entry+0x385,o.retired[i]))return false;
            sound(o.aux[i]);
        }
        return true;
    };
    Observation before,after;
    if(!capture(before))return "event_native_reason=fields_unreadable";
    if(!capture(after) || before!=after || !memory.Read(base+manager_pointer,again) || again!=manager)
        return "event_native_reason=snapshot_changed";
    std::ostringstream out;out<<"event_native_reason=observed; source=0x"<<std::hex<<before.manager.source
        <<"; pending=0x"<<before.manager.pending<<"; deferred=0x"<<before.manager.deferred
        <<"; mood=0x"<<before.manager.mood<<std::dec<<"; state="<<before.manager.state
        <<"; queued="<<before.manager.count<<"; delayed_stop="<<before.manager.delayed_stop;
    auto describe=[&](const std::string& name,const Sound& s) {
        out<<"; "<<name<<"=0x"<<std::hex<<s.pointer<<"; "<<name<<"_readable="<<s.readable;
        if(s.readable)out<<"; "<<name<<"_resource=0x"<<s.resource<<"; "<<name<<"_type=0x"<<unsigned(s.type)
            <<"; "<<name<<"_flags=0x"<<unsigned(s.flags)<<std::dec<<"; "<<name<<"_started="<<s.started;
        out<<std::dec;
    };
    describe("main",before.main);
    for(int i=0;i<2;++i){const auto name="aux"+std::to_string(i);describe(name,before.aux[i]);
        out<<"; "<<name<<"_clock="<<before.clocks[i]<<"; "<<name<<"_retired="<<unsigned(before.retired[i]);}
    out<<"; volume_available=0; observation_only=1";return out.str();
}
template<class Memory> std::string Describe(const Memory& memory,std::uintptr_t base,const Token& token) {
    const Inspection<Memory> view{memory,base};
    Snapshot before,after;RootState root;
    std::uintptr_t manager=0,pool=0,actual=0,header=0,reader=0;
    std::uint32_t stride=0,container=0,a4=0,a8=0,ac=0,total=0,reserve=0,capacity=0;
    std::int16_t index=-1;unsigned char flags=0;
    auto unavailable=[](const char* reason){return std::string("music_failure_diagnostic; reason=")+reason;};
    if(!token.identity || !token.root || !memory.Read(base+manager_pointer,manager) || manager!=token.manager ||
       !view.SnapshotAt(manager,before) || before.root!=token.root || before.identity!=token.identity ||
       before.source!=token.source || before.context!=token.context || before.mood!=token.mood.raw ||
       !view.Resources(token) || !view.Root(token,before.root,root) || !view.SameRoot(token,before.root,root))
        return unavailable("owner_unavailable");
    // 256681A..2566867 resolves the signed root context index using this pool.
    if(!memory.Read(token.root+0x9e,index) || index<0 || !memory.Read(base+context_pool_pointer,pool) ||
       pool<0x10000 || !memory.Read(base+context_stride,stride) || !stride || stride>0x100000 ||
       static_cast<std::uintptr_t>(index)>((std::numeric_limits<std::uintptr_t>::max)()-pool)/stride)
        return unavailable("context_unavailable");
    actual=pool+static_cast<std::uintptr_t>(index)*stride;
    if(!memory.Read(token.root+0x164,container) || !memory.Read(actual+0xa4,a4) ||
       !memory.Read(actual+0xa8,a8) || !memory.Read(actual+0xac,ac) || !memory.Read(actual+0xb5,flags))
        return unavailable("bank_fields_unreadable");
    const bool header_readable=memory.Read(actual+8,header) && header>=0x10000 &&
        memory.Read(header+0x14,total) && memory.Read(header+0x1c,reserve);
    const bool reader_readable=memory.Read(actual+0xc8,reader) && reader>=0x10000 &&
        memory.Read(reader+0x378,capacity);
    // Keep optional fields independent: native failure clears format/seek
    // pointers, whereas the preceding preparing observation may retain them.
    std::ostringstream fields;
    auto field=[&]<class T>(const char* name,std::uintptr_t at,T initial) {
        T value=initial;const bool readable=at>=0x10000 && memory.Read(at,value);
        fields<<"; "<<name<<"_readable="<<readable;
        if(readable)fields<<"; "<<name<<"=0x"<<std::hex<<static_cast<std::uint64_t>(value)<<std::dec;
    };
    field("root_offset_ms",token.root+0x6c,std::uint32_t{});
    field("root_stream_option",token.root+0x16d,static_cast<unsigned char>(0));
    field("context_offset_ms",actual+0xe8,std::uint32_t{});
    field("context_block_index",actual+0xec,std::uint32_t{});
    field("context_read_blocks",actual+0xf4,static_cast<unsigned char>(0));
    field("context_buffer_index",actual+0xf6,static_cast<unsigned char>(0));
    field("context_prefetch",actual+0xf7,static_cast<unsigned char>(0));
    field("context_read_flags",actual+0xf8,static_cast<unsigned char>(0));
    std::uintptr_t format=0,seek=0;
    const bool format_pointer=memory.Read(actual+0xb8,format),seek_pointer=memory.Read(actual+0xc0,seek);
    fields<<"; format_pointer_readable="<<format_pointer<<"; format=0x"<<std::hex<<format
          <<"; seek_pointer_readable="<<seek_pointer<<"; seek=0x"<<seek<<std::dec;
    if(format_pointer && format>=0x10000) {
        field("format_blocks",format,std::uint32_t{});
        field("format_block_bytes",format+4,std::uint32_t{});
        field("format_channels",format+8,std::uint32_t{});
    }
    if(header_readable) {
        field("header_buffer",header+8,std::uintptr_t{});
        field("archive_handle",header+0x24,std::uint32_t{});
    }
    if(reader_readable) {
        field("reader_buffer0",reader+0x360,std::uintptr_t{});
        field("reader_buffer1",reader+0x368,std::uintptr_t{});
        field("reader_data_offset",reader+0x374,std::uint32_t{});
        field("reader_request_bytes",reader+0x37c,std::uint32_t{});
    }
    std::int16_t again_index=-1;std::uintptr_t again_manager=0;
    std::uintptr_t again_pool=0;std::uint32_t again_stride=0,again_a4=0,again_a8=0,again_ac=0;
    unsigned char again_flags=0;
    RootState again_root;
    if(!view.SnapshotAt(manager,after) || before!=after || !memory.Read(token.root+0x9e,again_index) ||
       again_index!=index || !view.Root(token,token.root,again_root) || !view.SameRoot(token,token.root,again_root) ||
       again_root.started_at!=root.started_at || again_root.flags!=root.flags || again_root.type!=root.type ||
       !memory.Read(base+context_pool_pointer,again_pool) || again_pool!=pool ||
       !memory.Read(base+context_stride,again_stride) || again_stride!=stride ||
       !memory.Read(actual+0xa4,again_a4) || again_a4!=a4 || !memory.Read(actual+0xa8,again_a8) || again_a8!=a8 ||
       !memory.Read(actual+0xac,again_ac) || again_ac!=ac || !memory.Read(actual+0xb5,again_flags) || again_flags!=flags ||
       !memory.Read(base+manager_pointer,again_manager) || again_manager!=manager)
        return unavailable("snapshot_changed");
    std::ostringstream out;
    out<<"music_failure_diagnostic; reason=observed; context_index="<<index<<"; context_matches="<<(actual==token.context)
       <<"; actual_context=0x"<<std::hex<<actual<<"; validated_context=0x"<<token.context
       <<"; container=0x"<<container<<"; requested_bank=0x"<<a4<<"; loading_bank=0x"<<a8
       <<"; failed_bank=0x"<<ac<<"; context_flags=0x"<<static_cast<unsigned>(flags)<<std::dec
       <<"; failed_bank_matches="<<(ac==container)<<"; header_readable="<<header_readable;
    if(header_readable)out<<"; header_total="<<total<<"; header_reserve="<<reserve;
    out<<"; reader_readable="<<reader_readable;
    if(reader_readable)out<<"; reader_capacity="<<capacity;
    out<<fields.str();
    return out.str();
}
}
#ifdef _WIN32
struct NativeBgmDiagnostics {
    static std::string DescribeEvent() {
        namespace music=horse_races::bgm::native_music;
        return music::diagnostics::DescribeEvent(horse_races::bgm::audio::WindowsMemory{},
            music::WindowsCalls::Base(),music::WindowsCalls{}.Supported());
    }
    static std::string Describe(const horse_races::bgm::native_music::Token& token) {
        return horse_races::bgm::native_music::diagnostics::Describe(
            horse_races::bgm::audio::WindowsMemory{},horse_races::bgm::native_music::WindowsCalls::Base(),token);
    }
};
#endif
