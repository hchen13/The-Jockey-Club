#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace horse_races::bgm::audio::resources {
// Verified in the mapped build at RVA 2516734 / 2536130. This is the
// resource manager's inline table, not the native stream-slot manager.
inline constexpr std::uintptr_t table_rva=0x5342018;
inline constexpr std::size_t descriptor_stride=0xd0;
struct Identity {
    std::uintptr_t array=0,descriptor=0,data=0,index=0,entry=0,raw=0,registry=0;
    std::uint32_t data_size=0,index_count=0,name=0,offset=0,linked=0,record_size=0;
    std::uint32_t position=0,before_name=0,after_name=0;
    std::uintptr_t name_buckets=0;
    std::uint32_t range_begin=0,range_end=0;
    unsigned char bucketed=0;
    std::uint16_t descriptor_count=0,ordinal=0;
    // Allocator/registry snapshots are for a single lookup's stability only.
    // Other banks can grow/reallocate those tables without changing this link.
    bool operator==(const Identity& b) const {
        return registry==b.registry && data==b.data && raw==b.raw && name==b.name &&
            ordinal==b.ordinal && offset==b.offset && linked==b.linked && record_size==b.record_size;
    }
};
struct PackIdentity {
    std::uintptr_t buckets=0,head=0,node=0,next=0,registry=0;
    std::uint32_t bucket_count=0,name=0,index=0;
    // Collision neighbors and hash-table storage can change for another pack.
    // BindPack checks their stability within each lookup, not across playback.
    bool operator==(const PackIdentity& b) const {return registry==b.registry && name==b.name && index==b.index;}
};

template<class Memory> struct Inspection {
    const Memory& memory;
    std::uintptr_t base=0;
    std::uintptr_t registry_rva=table_rva; // Other verified REL registries share this layout.
    template<class T> bool Read(std::uintptr_t at,T& value) const {return memory.Read(at,value);}
    static bool Span(std::uintptr_t at,std::size_t size) {
        return at>=0x10000 && size && size<=(std::numeric_limits<std::uintptr_t>::max)()-at;
    }
    bool NameRange(Identity& v) const {
        v.range_begin=0;v.range_end=v.index_count;
        if(v.bucketed) {
            std::uint32_t count=0;
            if(!Read(v.descriptor+0x58,v.name_buckets) || !Span(v.name_buckets,256*8) ||
                !Read(v.name_buckets+static_cast<std::uintptr_t>(v.name&255)*8,v.range_begin) ||
                !Read(v.name_buckets+static_cast<std::uintptr_t>(v.name&255)*8+4,count) ||
                v.range_begin>v.index_count || count>v.index_count-v.range_begin)return false;
            v.range_end=v.range_begin+count;
        }
        return true;
    }
    // Exact native last-descriptor-first lookup. Bind repeats the resource
    // metadata, entry and selected bucket checks before returning an identity.
    bool Find(std::uint32_t name,std::uint32_t size,Identity& out) const {
        if(!base || !name || !size || !Span(base,registry_rva+0x96))return false;
        std::uintptr_t array=0;std::uint16_t count=0;unsigned char bucketed=0;
        if(!Read(base+registry_rva,array) || !Read(base+registry_rva+8,count) ||
            !Read(base+registry_rva+0x95,bucketed) || !count || count>256 ||
            !Span(array,static_cast<std::size_t>(count)*descriptor_stride))return false;
        for(unsigned ordinal=count;ordinal--;) {
            Identity v;v.name=name;v.bucketed=bucketed;v.descriptor=array+ordinal*descriptor_stride;
            unsigned char enabled=0;
            if(!Read(v.descriptor+0xcd,enabled) || !Read(v.descriptor+0x1c,v.index_count))return false;
            if(!enabled || !v.index_count)continue;
            if(v.index_count>0x200000 || !Read(v.descriptor+0x20,v.index) ||
                !Span(v.index,static_cast<std::size_t>(v.index_count)*8) || !NameRange(v))return false;
            auto lo=v.range_begin,hi=v.range_end;
            while(lo<hi) {
                const auto mid=lo+(hi-lo)/2;std::uint32_t candidate=0;
                if(!Read(v.index+static_cast<std::uintptr_t>(mid)*8+4,candidate))return false;
                if(candidate<name)lo=mid+1;else hi=mid;
            }
            if(lo==v.range_end)continue;
            std::uint32_t candidate=0,offset=0;
            const auto entry=v.index+static_cast<std::uintptr_t>(lo)*8;
            if(!Read(entry+4,candidate))return false;
            if(candidate!=name)continue;
            if(!Read(entry,offset) || !Read(v.descriptor+0x10,v.data) || !Read(v.descriptor+0x18,v.data_size) ||
                !Span(v.data,v.data_size) || offset>=v.data_size || size>v.data_size-offset)return false;
            return Bind(name,v.data+offset,size,out);
        }
        return false;
    }
    // Bind an authored name hash to an existing runtime raw pointer. Never
    // identify a record from a byte prefix with ignored reference fields.
    bool Bind(std::uint32_t name,std::uintptr_t raw,std::uint32_t size,Identity& out) const {
        if(!base || !name || !Span(raw,size) || !Span(base,registry_rva+0x96))return false;
        Identity v;v.registry=base+registry_rva;v.name=name;v.raw=raw;v.record_size=size;
        if(!Read(base+registry_rva,v.array) || !Read(base+registry_rva+8,v.descriptor_count) ||
            !Read(base+registry_rva+0x95,v.bucketed) ||
            !v.descriptor_count || v.descriptor_count>256 ||
            !Span(v.array,static_cast<std::size_t>(v.descriptor_count)*descriptor_stride))return false;
        unsigned found=0;
        for(std::uint16_t i=0;i<v.descriptor_count;++i) {
            const auto descriptor=v.array+static_cast<std::uintptr_t>(i)*descriptor_stride;
            std::uintptr_t data=0;std::uint32_t bytes=0;unsigned char enabled=0;
            if(!Read(descriptor+0xcd,enabled) || !Read(descriptor+0x10,data) ||
                !Read(descriptor+0x18,bytes))return false;
            if(!enabled || !Span(data,bytes) || raw<data || raw-data>=bytes)continue;
            // Handles carry exactly 24 offset bits. Check the full record,
            // rather than merely its first byte, against the data allocation.
            if(++found!=1 || bytes>0x1000000 || size>bytes-(raw-data))return false;
            v.ordinal=i;v.descriptor=descriptor;v.data=data;v.data_size=bytes;
            v.offset=static_cast<std::uint32_t>(raw-data);
            if(!Read(descriptor+0x1c,v.index_count) || !v.index_count || v.index_count>0x200000 ||
                !Read(descriptor+0x20,v.index) || !Span(v.index,static_cast<std::size_t>(v.index_count)*8))return false;
        }
        if(found!=1)return false;
        // The native bucket accelerator defines index ordering, not merely
        // an optimization that can be skipped for a whole-index binary search.
        if(!NameRange(v))return false;
        // Native entries are {dataOffset,nameHash}, sorted within this range.
        std::uint32_t lo=v.range_begin,hi=v.range_end;
        while(lo<hi) {
            const auto mid=lo+(hi-lo)/2;
            std::uint32_t candidate=0;
            if(!Read(v.index+static_cast<std::uintptr_t>(mid)*8+4,candidate))return false;
            if(candidate<name)lo=mid+1;else hi=mid;
        }
        if(lo==v.range_end)return false;
        v.position=lo;
        v.entry=v.index+static_cast<std::uintptr_t>(lo)*8;
        std::uint32_t candidate=0,offset=0;
        if(!Read(v.entry+4,candidate) || candidate!=name || !Read(v.entry,offset) || offset!=v.offset ||
            (lo>v.range_begin && (!Read(v.entry-4,v.before_name) || v.before_name>=name)) ||
            (lo+1<v.range_end && (!Read(v.entry+12,v.after_name) || v.after_name<=name)))return false;
        v.linked=(static_cast<std::uint32_t>(v.ordinal)<<24)|v.offset;
        if(v.linked==0 || v.linked==0xffffffff)return false;
        // Re-read the selected metadata and index after lookup; the caller
        // must additionally recheck its native stream generation/owner token.
        if(!Stable(v))return false;
        out=v;return true;
    }
    bool Stable(const Identity& v) const {
        std::uintptr_t array=0,data=0,index=0,buckets=0;
        std::uint16_t count=0;std::uint32_t bytes=0,entries=0,offset=0,name=0,before=0,after=0,begin=0,range_count=0;unsigned char enabled=0,bucketed=0;
        return v.registry==base+registry_rva && Read(base+registry_rva,array) && array==v.array &&
            Read(base+registry_rva+8,count) && count==v.descriptor_count &&
            Read(base+registry_rva+0x95,bucketed) && bucketed==v.bucketed &&
            Read(v.descriptor+0xcd,enabled) && enabled &&
            Read(v.descriptor+0x10,data) && data==v.data &&
            Read(v.descriptor+0x18,bytes) && bytes==v.data_size &&
            Read(v.descriptor+0x1c,entries) && entries==v.index_count &&
            Read(v.descriptor+0x20,index) && index==v.index &&
            (!v.bucketed || (Read(v.descriptor+0x58,buckets) && buckets==v.name_buckets &&
                Read(buckets+static_cast<std::uintptr_t>(v.name&255)*8,begin) && begin==v.range_begin &&
                Read(buckets+static_cast<std::uintptr_t>(v.name&255)*8+4,range_count) && range_count==v.range_end-v.range_begin)) &&
            Read(v.entry,offset) && offset==v.offset && Read(v.entry+4,name) && name==v.name &&
            (v.position==v.range_begin || (Read(v.entry-4,before) && before==v.before_name)) &&
            (v.position+1>=v.range_end || (Read(v.entry+12,after) && after==v.after_name));
    }
    bool Same(const Identity& v) const {
        Identity current;
        return Bind(v.name,v.raw,v.record_size,current) && current==v;
    }
    bool LinkedEquals(const Identity& target,std::uint32_t actual) const {
        return actual==target.linked && Same(target);
    }
    // Pack relocation uses the same manager's hash->global pack-index table,
    // not the descriptor/offset handle format of REL resource references.
    // For this table the source four bytes are read as little-endian u32.
    bool BindPack(std::uint32_t name,std::uint32_t index,PackIdentity& out) const {
        PackIdentity first,again;
        if(!LookupPack(name,index,first) || !LookupPack(name,index,again) ||
            first.buckets!=again.buckets || first.bucket_count!=again.bucket_count || first.head!=again.head ||
            first.node!=again.node || first.next!=again.next || !(first==again))return false;
        out=again;return true;
    }
    bool LookupPack(std::uint32_t name,std::uint32_t index,PackIdentity& out) const {
        if(!base || !name || index==0xffffffff || !Span(base,registry_rva+0x3c))return false;
        PackIdentity v;v.registry=base+registry_rva;v.name=name;v.index=index;
        if(!Read(base+registry_rva+0x30,v.buckets) ||
            !Read(base+registry_rva+0x38,v.bucket_count) || !v.bucket_count || v.bucket_count>0x100000 ||
            !Span(v.buckets,static_cast<std::size_t>(v.bucket_count)*8))return false;
        if(!Read(v.buckets+static_cast<std::uintptr_t>(name%v.bucket_count)*8,v.head))return false;
        auto at=v.head;unsigned found=0;
        for(unsigned limit=0;limit<1024 && at;++limit) {
            std::uint32_t hash=0,value=0;std::uintptr_t next=0;
            if(!Span(at,16) || !Read(at,hash) || !Read(at+4,value) || !Read(at+8,next))return false;
            if(hash==name) {
                if(++found!=1 || value!=index)return false;
                v.node=at;v.next=next;
            }
            at=next;
        }
        // Scan the entire bounded chain, including after a matching node;
        // duplicate target hashes and cycles must never produce an identity.
        if(at || found!=1 || !Stable(v))return false;
        out=v;return true;
    }
    bool Stable(const PackIdentity& v) const {
        std::uintptr_t buckets=0,head=0,next=0;std::uint32_t count=0,name=0,index=0;
        return v.registry==base+registry_rva && Read(base+registry_rva+0x30,buckets) && buckets==v.buckets &&
            Read(base+registry_rva+0x38,count) && count==v.bucket_count &&
            Read(v.buckets+static_cast<std::uintptr_t>(v.name%v.bucket_count)*8,head) && head==v.head &&
            Read(v.node,name) && name==v.name && Read(v.node+4,index) && index==v.index &&
            Read(v.node+8,next) && next==v.next;
    }
    bool Same(const PackIdentity& v) const {
        PackIdentity current;
        return BindPack(v.name,v.index,current) && current==v;
    }
};
} // namespace horse_races::bgm::audio::resources
