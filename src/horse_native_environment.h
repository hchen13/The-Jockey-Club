#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace horse_motion::native_environment {
inline constexpr std::uintptr_t kComponentIndexRva=0x39ece40;
inline constexpr std::uintptr_t kDefaultKeyRva=0x3b797ac;
inline constexpr std::uint32_t kDefaultKey=0xe4df46d5;
inline constexpr std::uint32_t kSnowKey=0xefb6eff6;
inline constexpr std::uintptr_t kResourceVtableRva=0x3511538;
enum class Status {Unsupported,Unavailable,Identity,Resource,Directory,Capability,Stale,Ready};
struct Context {
    std::uintptr_t entity=0,components=0,motion=0,container=0,container_weak=0;
    std::uintptr_t current_resource=0,current_weak=0,normal_resource=0,normal_weak=0;
    std::uintptr_t directory=0,current_entry=0,normal_entry=0;
    std::uint32_t generation=0,component_base=0,current_key=0,default_key=0;
    std::uint16_t count=0;
    std::uint32_t interpolation_bits=0,native_bits=0;
    std::array<std::uint32_t,4> current_gallop{},normal_gallop{};
    bool operator==(const Context&) const=default;
};
struct Snapshot {
    bool valid=false;
    Status status=Status::Unavailable;
    float normal_reference_mps=0,current_native_mps=0,ratio=0;
    Context context{};
    bool Limited() const {return valid && ratio<1.F-.00001F;}
};
inline bool Pointer(std::uintptr_t p){return p>=0x10000 && p<0x0000800000000000ULL && !(p&7);}
template<class T> T At(const auto& bytes,std::size_t offset){
    T result{};std::memcpy(&result,bytes.data()+offset,sizeof result);return result;
}
template<class Memory,class T> bool Read(Memory& m,std::uintptr_t p,T& value){
    return p>=0x10000 && p<0x0000800000000000ULL-sizeof value && m.Read(p,&value,sizeof value);
}
inline float Float(std::uint32_t bits){float v;std::memcpy(&v,&bits,4);return v;}
// Exact 1AD0F40 table[7] producer: +6A4 plus the +6B0 -> +6AC
// interpolation. +6A8 is the OTHER gallop bound, not part of this value.
inline float Gallop(const std::array<std::uint32_t,4>& values,float blend){
    const float base=Float(values[0]),high=Float(values[2]),low=Float(values[3]);
    if(!std::isfinite(base) || !std::isfinite(high) || !std::isfinite(low) ||
        !std::isfinite(blend) || blend<0 || blend>1)return 0;
    return base+(low+(high-low)*blend);
}
struct Entry {std::uint32_t key=0;std::uintptr_t resource=0,address=0;};
// 450210 is a lower_bound over a bounded, sorted native array of 16-byte
// records. Read only the search path and exact match; no heap allocation.
template<class Memory> bool Lookup(Memory& m,const Context& c,std::uint32_t key,Entry& out){
    std::size_t first=0,count=c.count;
    while(count){
        const auto step=count/2,mid=first+step;
        std::array<std::uint8_t,16> raw{};
        if(!Read(m,c.directory+mid*16,raw))return false;
        const auto actual=At<std::uint32_t>(raw,0);
        if(actual<key){first=mid+1;count-=step+1;}else count=step;
    }
    if(first>=c.count)return false;
    std::array<std::uint8_t,16> raw{};
    out.address=c.directory+first*16;
    if(!Read(m,out.address,raw) || At<std::uint32_t>(raw,0)!=key)return false;
    out.key=key;out.resource=At<std::uintptr_t>(raw,8);return Pointer(out.resource);
}
template<class Memory> bool Resource(Memory& m,std::uintptr_t base,std::uintptr_t address,std::uint32_t key,
    std::uintptr_t& weak,std::array<std::uint32_t,4>& gallop){
    std::array<std::uint8_t,24> header{};std::uint8_t alive=0;
    return Pointer(address) && Read(m,address,header) && At<std::uintptr_t>(header,0)==base+kResourceVtableRva &&
        At<std::uint32_t>(header,0x10)==key && Pointer(weak=At<std::uintptr_t>(header,8)) &&
        Read(m,weak+4,alive) && alive && Read(m,address+0x6a4,gallop);
}
// This does not invoke the native resource selector or touch its reference
// counts. The caller verifies the exact engine code before using this reader.
template<class Memory> Snapshot CaptureOnce(Memory& m,std::uintptr_t base,std::uintptr_t entity){
    Snapshot s;auto& c=s.context;c.entity=entity;
    if(!Pointer(entity)){s.status=Status::Identity;return s;}
    std::array<std::uint8_t,16> component_header{},pair{},directory_header{};
    std::uintptr_t owner=0;std::uint8_t dirty=0;
    if(!Read(m,entity+0x9c,c.generation) || !Read(m,base+kComponentIndexRva,component_header))return s;
    c.component_base=At<std::uint32_t>(component_header,0);
    c.components=At<std::uintptr_t>(component_header,8);
    if(!Pointer(c.components) || (c.generation&0x1ffff)<c.component_base){s.status=Status::Identity;return s;}
    const auto index=(c.generation&0x1ffff)-c.component_base;
    if(index>0x1ffff || !Read(m,c.components+std::uintptr_t(index)*0x148+0xd0,c.motion))return s;
    c.motion&=~std::uintptr_t(1);
    if(!Pointer(c.motion) || !Read(m,c.motion+8,owner) || (owner&~std::uintptr_t(15))!=entity){s.status=Status::Identity;return s;}
    if(!Read(m,c.motion+0x290,dirty) || dirty){s.status=Status::Stale;return s;}
    if(!Read(m,c.motion+0x868,pair))return s;
    c.current_resource=At<std::uintptr_t>(pair,0);const auto selected_weak=At<std::uintptr_t>(pair,8);
    if(!Read(m,c.motion+0x6c0,owner) || owner!=entity || !Read(m,c.motion+0x6d0,pair)){s.status=Status::Identity;return s;}
    c.container=At<std::uintptr_t>(pair,0);c.container_weak=At<std::uintptr_t>(pair,8);
    std::uint8_t container_alive=0;
    if(!Pointer(c.container) || !Pointer(c.container_weak) || !Read(m,c.container_weak+4,container_alive) || !container_alive){s.status=Status::Resource;return s;}
    if(!Read(m,base+kDefaultKeyRva,c.default_key) || c.default_key!=kDefaultKey ||
        !Read(m,c.container+0x230,directory_header)){s.status=Status::Directory;return s;}
    c.directory=At<std::uintptr_t>(directory_header,0);c.count=At<std::uint16_t>(directory_header,8);
    if(!Pointer(c.directory) || !c.count || c.count>256){s.status=Status::Directory;return s;}
    if(!Pointer(c.current_resource) || !Read(m,c.current_resource+0x10,c.current_key)){s.status=Status::Resource;return s;}
    Entry normal,current;
    if(!Lookup(m,c,c.default_key,normal) || !Lookup(m,c,c.current_key,current) || current.resource!=c.current_resource){s.status=Status::Directory;return s;}
    c.normal_resource=normal.resource;c.normal_entry=normal.address;c.current_entry=current.address;
    if(!Resource(m,base,c.current_resource,c.current_key,c.current_weak,c.current_gallop) || c.current_weak!=selected_weak ||
        !Resource(m,base,c.normal_resource,c.default_key,c.normal_weak,c.normal_gallop)){s.status=Status::Resource;return s;}
    if(!Read(m,c.motion+0x908,c.interpolation_bits) || !Read(m,c.motion+0x254,c.native_bits))return s;
    const float blend=Float(c.interpolation_bits),derived=Gallop(c.current_gallop,blend);
    s.normal_reference_mps=Gallop(c.normal_gallop,blend);s.current_native_mps=Float(c.native_bits);
    if(!std::isfinite(s.current_native_mps) || s.current_native_mps<=0 || s.current_native_mps>25 ||
        !std::isfinite(s.normal_reference_mps) || s.normal_reference_mps<10 || s.normal_reference_mps>25 ||
        !std::isfinite(derived) || derived<=0){s.status=Status::Capability;return s;}
    // The native cache tolerates only 1e-6 interpolation changes. Allow float
    // rounding, not a previous resource's still-published speed table.
    if(std::abs(derived-s.current_native_mps)>0.0001F){s.status=Status::Stale;return s;}
    s.ratio=std::min(1.F,s.current_native_mps/s.normal_reference_mps);
    s.valid=true;s.status=Status::Ready;return s;
}
template<class Memory> bool StillCurrent(Memory& m,std::uintptr_t base,const Snapshot& expected){
    if(!expected.valid)return false;
    const auto now=CaptureOnce(m,base,expected.context.entity);
    return now.valid && now.context==expected.context;
}
template<class Memory> Snapshot Capture(Memory& m,std::uintptr_t base,std::uintptr_t entity){
    auto s=CaptureOnce(m,base,entity);
    if(s.valid && !StillCurrent(m,base,s)){s.valid=false;s.status=Status::Stale;}
    return s;
}
// A Snow entry in the same native directory is a capability reference, not
// evidence that this horse is standing in snow. Call only after independently
// identifying its terrain. No resource is selected and no game state changes.
struct SnowReference {
    bool valid=false;
    float native_mps=0,ratio=0;
    std::array<std::uint32_t,4> gallop{};
};
struct SnowReferenceRead {
    std::uintptr_t resource=0,entry=0,weak=0;
    std::array<std::uint32_t,4> gallop{};
    bool operator==(const SnowReferenceRead&) const=default;
};
template<class Memory> bool ReadSnowReference(Memory& m,std::uintptr_t base,const Context& context,SnowReferenceRead& out){
    Entry entry;
    if(!Lookup(m,context,kSnowKey,entry) ||
        !Resource(m,base,entry.resource,kSnowKey,out.weak,out.gallop))return false;
    out.resource=entry.resource;out.entry=entry.address;
    return true;
}
template<class Memory> SnowReference CaptureSnowReference(Memory& m,std::uintptr_t base,const Snapshot& expected){
    SnowReference result;
    if(!expected.valid || !std::isfinite(expected.normal_reference_mps) || expected.normal_reference_mps<=0)return result;
    SnowReferenceRead first{},second{};
    if(!ReadSnowReference(m,base,expected.context,first) || !StillCurrent(m,base,expected) ||
        !ReadSnowReference(m,base,expected.context,second) || first!=second ||
        !StillCurrent(m,base,expected))return result;
    const float speed=Gallop(first.gallop,Float(expected.context.interpolation_bits));
    if(!std::isfinite(speed) || speed<=0 || speed>25)return result;
    result.native_mps=speed;result.ratio=std::min(1.F,speed/expected.normal_reference_mps);result.gallop=first.gallop;
    result.valid=std::isfinite(result.ratio) && result.ratio>0;
    return result;
}
// Identity only: interpolation and current native speed legitimately change
// frame-to-frame. Including them would defeat the bound on directory reads.
inline bool SameSnowOwner(const Context& a,const Context& b){
    return a.entity==b.entity && a.generation==b.generation && a.components==b.components &&
        a.motion==b.motion && a.container==b.container && a.container_weak==b.container_weak &&
        a.directory==b.directory && a.count==b.count;
}
struct SnowReferenceCache {
    Context owner{};
    SnowReference reference{};
    std::uint64_t next_refresh=0;
    bool bound=false;
    void Reset(){*this={};}
    template<class Reader> float Ratio(const Snapshot& current,std::uint64_t now,Reader read){
        if(!current.valid){Reset();return 0;}
        if(!bound || !SameSnowOwner(owner,current.context) || now>=next_refresh){
            reference=read(current);owner=current.context;bound=true;next_refresh=now+250;
        }
        if(!reference.valid || !std::isfinite(current.normal_reference_mps) || current.normal_reference_mps<=0)return 0;
        // Only immutable copied floats are reused; use the current validated
        // frame's interpolation and default reference, not an old ratio.
        const float snow=Gallop(reference.gallop,Float(current.context.interpolation_bits));
        if(!std::isfinite(snow) || snow<=0 || snow>25)return 0;
        const float ratio=std::min(1.F,snow/current.normal_reference_mps);
        return std::isfinite(ratio) && ratio>0?ratio:0;
    }
};
} // namespace horse_motion::native_environment
