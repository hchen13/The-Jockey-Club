#pragma once
#include "horse_native_environment.h"

// Diagnostics only. A consistent read is NOT proof that the native cache was
// recomputed this frame, nor a complete effective speed cap. No controller may
// consume these observations as an authority for applying speed changes.
namespace horse_motion::native_constraint_probe {
enum class Status { Unsupported,Unavailable,Identity,InvalidFloat,Stale,Ready };
enum class FieldStatus { Unavailable,Missing,InvalidTable,ProbeLimit,WrongType,InvalidFloat,Ready };
// A stable read of the query arguments, not proof the engine ran the query
// recently, that the position tracks the horse, or what the word signifies.
enum class QueryInputStatus { Unavailable,InvalidFloat,Stale,Ready,Unsupported };
struct QueryInput {
    QueryInputStatus status=QueryInputStatus::Unavailable;
    std::array<std::uint8_t,16> raw_position{};
    std::uint16_t raw_word_470=0;
    bool operator==(const QueryInput&) const=default;
};
struct Field {
    FieldStatus status=FieldStatus::Unavailable;
    std::uint32_t bits=0,flags=0,count=0;
    std::uintptr_t data=0,entry=0;
    bool Present() const {return status==FieldStatus::Ready;}
    float Number() const {return native_environment::Float(bits);}
    bool Boolean() const {return (bits&255)!=0;}
    bool operator==(const Field&) const=default;
};
struct Context {
    std::uintptr_t entity=0,components=0,motion=0,physics=0,blackboard=0,blackboard_owner=0;
    std::uint32_t generation=0,component_base=0;
    std::uint8_t cache_update_gate_858=0;
    std::array<std::uint8_t,20> request{};
    std::array<std::uint32_t,2> cached_speed_bits{};
    std::array<std::uint8_t,0x38> reductions{};
    std::array<std::uint8_t,0x30> surface{};
    bool operator==(const Context&) const=default;
};
struct Snapshot {
    bool valid=false;
    Status status=Status::Unavailable;
    // Intentionally always false: there is no native timestamp in this cache.
    static constexpr bool freshness_verified=false;
    static constexpr bool speed_authority=false;
    Context context{};
    float request_value=0;
    std::uint32_t request_gait=0,request_type=0;
    float gait_bound_2bc=0,gait_bound_2c0=0;
    float integrated_speed_mps=0,generated_speed_mps=0;
    bool cache_update_gate_858=false; // Producer prerequisite, NOT a timestamp.
    std::array<float,14> reductions_mps{}; // native offsets 0..34, step 4
    bool surface_valid=false;
    std::array<float,3> surface_position{},surface_normal{};
    float surface_depth_raw=0;
    std::uint32_t surface_material_index=0;
    QueryInput surface_query_input;
    Field surface_depth,signed_slope,alternate_slope;
    Field alternate_slope_selected,slope_bypass;
    float SlopeReduction() const {return reductions_mps[1];}
    float WaterReduction() const {return reductions_mps[2];}
    float SurfaceReduction() const {return reductions_mps[3];}
    float TotalReduction() const {return reductions_mps[13];}
};
inline std::uint32_t HashIndex(std::uint32_t key){
    auto h=(key^(key>>16))*0x85ebca6bU;
    h=(h^(h>>13))*0xc2b2ae35U;return h^(h>>16);
}
// Exact 2AD03C/3ABBEC hashing and wraparound. Limit reads to 64 probe cells;
// this deliberate diagnostic bound returns Unknown, never a false zero value.
template<class Memory> Field Lookup(Memory& m,std::uintptr_t table,std::uint32_t key,bool boolean=false){
    using native_environment::At;using native_environment::Read;
    Field f;std::array<std::uint8_t,12> header{};
    if(!Read(m,table,header))return f;
    f.data=At<std::uintptr_t>(header,0);f.count=At<std::uint32_t>(header,8);
    if(!f.count){f.status=FieldStatus::Missing;return f;}
    if(!native_environment::Pointer(f.data) || f.count>8192 || (f.count&(f.count-1))){f.status=FieldStatus::InvalidTable;return f;}
    const auto stride=boolean?8U:12U;
    auto index=HashIndex(key)&(f.count-1);
    for(std::uint32_t probe=0;probe<std::min(f.count,64U);++probe){
        std::array<std::uint8_t,12> entry{};
        const auto address=f.data+std::uintptr_t(index)*stride;
        if(address<0x10000 || address>0x0000800000000000ULL-stride || !m.Read(address,entry.data(),stride))return f;
        const auto actual=At<std::uint32_t>(entry,0);
        if(!actual){f.status=FieldStatus::Missing;return f;}
        if(actual==key){
            f.entry=address;f.bits=At<std::uint32_t>(entry,4);
            if(boolean){
                f.flags=At<std::uint16_t>(entry,6);
                f.bits&=255;
                f.status=f.flags?FieldStatus::Ready:FieldStatus::Missing;
            }else{
                f.flags=At<std::uint32_t>(entry,8);
                if(!(f.flags>>16))f.status=FieldStatus::Missing;
                else if((f.flags&255)!=4)f.status=FieldStatus::WrongType;
                else f.status=std::isfinite(f.Number())?FieldStatus::Ready:FieldStatus::InvalidFloat;
            }
            return f;
        }
        index=(index+1)&(f.count-1);
    }
    f.status=FieldStatus::ProbeLimit;return f;
}
template<class Memory> Snapshot CaptureOnce(Memory& m,std::uintptr_t base,std::uintptr_t entity,bool query_supported=true){
    using native_environment::At;using native_environment::Read;using native_environment::Pointer;
    Snapshot s;auto& c=s.context;c.entity=entity;
    if(!Pointer(entity)){s.status=Status::Identity;return s;}
    std::array<std::uint8_t,16> header{};std::uintptr_t owner=0,vtable=0;
    if(!Read(m,entity+0x9c,c.generation) || !Read(m,base+native_environment::kComponentIndexRva,header))return s;
    c.component_base=At<std::uint32_t>(header,0);c.components=At<std::uintptr_t>(header,8);
    if(!Pointer(c.components) || (c.generation&0x1ffff)<c.component_base){s.status=Status::Identity;return s;}
    const auto index=(c.generation&0x1ffff)-c.component_base;
    if(index>0x1ffff || !Read(m,c.components+std::uintptr_t(index)*0x148+0xd0,c.motion) ||
        !Read(m,c.components+std::uintptr_t(index)*0x148+0xe0,c.physics))return s;
    c.motion&=~std::uintptr_t(1);c.physics&=~std::uintptr_t(1);
    if(!Pointer(c.motion) || !Pointer(c.physics) || !Read(m,c.motion+8,owner) ||
        (owner&~std::uintptr_t(15))!=entity || !Read(m,c.physics+8,owner) ||
        (owner&~std::uintptr_t(15))!=entity || !Read(m,c.physics,vtable) || vtable!=base+0x33f67e0){s.status=Status::Identity;return s;}
    if(!Read(m,c.motion+0x2b0,c.request) || !Read(m,c.motion+0x1138,c.reductions) ||
        !Read(m,c.physics+0x690,c.surface) || !Read(m,c.motion+0x860,c.blackboard) ||
        !Read(m,c.motion+0x890,c.cached_speed_bits[0]) || !Read(m,c.motion+0x8ac,c.cached_speed_bits[1]) ||
        !Read(m,c.motion+0x858,c.cache_update_gate_858))return s;
    s.request_value=At<float>(c.request,0);s.request_gait=At<std::uint32_t>(c.request,4);s.request_type=At<std::uint32_t>(c.request,8);
    s.gait_bound_2bc=At<float>(c.request,12);s.gait_bound_2c0=At<float>(c.request,16);
    s.integrated_speed_mps=native_environment::Float(c.cached_speed_bits[0]);s.generated_speed_mps=native_environment::Float(c.cached_speed_bits[1]);
    s.cache_update_gate_858=c.cache_update_gate_858!=0;
    for(std::size_t i=0;i<s.reductions_mps.size();++i)s.reductions_mps[i]=At<float>(c.reductions,i*4);
    for(std::size_t i=0;i<3;++i){s.surface_position[i]=At<float>(c.surface,i*4);s.surface_normal[i]=At<float>(c.surface,0x10+i*4);}
    s.surface_depth_raw=At<float>(c.surface,0x1c);s.surface_valid=c.surface[0x24]!=0;s.surface_material_index=At<std::uint32_t>(c.surface,0x28);
    const auto finite=[](const auto& a){return std::all_of(a.begin(),a.end(),[](float v){return std::isfinite(v);});};
    if(!std::isfinite(s.request_value) || !std::isfinite(s.gait_bound_2bc) || !std::isfinite(s.gait_bound_2c0) ||
        !std::isfinite(s.integrated_speed_mps) || !std::isfinite(s.generated_speed_mps) || !finite(s.reductions_mps) ||
        !finite(s.surface_position) || !finite(s.surface_normal) || !std::isfinite(s.surface_depth_raw)){s.status=Status::InvalidFloat;return s;}
    // 1491.50: 9CBE23 passes physics+440 (16 bytes) and the uint16 at
    // physics+470 to 142AF98. These are only arguments at the producer's
    // callsite: neither a query timestamp nor a verified material/terrain ID.
    // Their optional read must not reject an otherwise useful observation.
    if(!query_supported)s.surface_query_input.status=QueryInputStatus::Unsupported;
    else if(Read(m,c.physics+0x440,s.surface_query_input.raw_position) &&
        Read(m,c.physics+0x470,s.surface_query_input.raw_word_470)) {
        bool position_finite=true;
        for(std::size_t i=0;i<3;++i)
            position_finite &= std::isfinite(At<float>(s.surface_query_input.raw_position,i*4));
        s.surface_query_input.status=position_finite?QueryInputStatus::Ready:QueryInputStatus::InvalidFloat;
    }
    // 1A8C52C stores its entity argument at BB+0. A recycled/foreign BB must
    // not supply optional fields even when the entity generation is unchanged.
    // Other raw component observations remain useful without a usable BB.
    if(Pointer(c.blackboard) && Read(m,c.blackboard,c.blackboard_owner) && c.blackboard_owner==entity){
        s.surface_depth=Lookup(m,c.blackboard+0x28,0x8020353e);
        s.signed_slope=Lookup(m,c.blackboard+0x28,0xc822bf3b);
        s.alternate_slope=Lookup(m,c.blackboard+0x28,0x60654beb);
        s.alternate_slope_selected=Lookup(m,c.blackboard+0x18,0xeb66a57f,true);
        s.slope_bypass=Lookup(m,c.blackboard+0x18,0x7d80d01c,true);
    }
    s.valid=true;s.status=Status::Ready;return s;
}
template<class Memory> Snapshot Capture(Memory& m,std::uintptr_t base,std::uintptr_t entity,bool query_supported=true){
    auto s=native_constraint_probe::CaptureOnce(m,base,entity,query_supported);if(!s.valid)return s;
    const auto again=native_constraint_probe::CaptureOnce(m,base,entity,query_supported);
    if(!again.valid || s.context!=again.context || s.surface_depth!=again.surface_depth ||
        s.signed_slope!=again.signed_slope || s.alternate_slope!=again.alternate_slope ||
        s.alternate_slope_selected!=again.alternate_slope_selected || s.slope_bypass!=again.slope_bypass){s.valid=false;s.status=Status::Stale;}
    // Auxiliary input churn is not a reason to discard the previously
    // supported fields, but it must never be presented as a stable reading.
    if(s.valid){
        auto& first=s.surface_query_input;const auto& second=again.surface_query_input;
        if(first.status==QueryInputStatus::Unsupported || second.status==QueryInputStatus::Unsupported)
            first.status=QueryInputStatus::Unsupported;
        else if(first.status==QueryInputStatus::Unavailable || second.status==QueryInputStatus::Unavailable)
            first.status=QueryInputStatus::Unavailable;
        else if(first.status==QueryInputStatus::InvalidFloat || second.status==QueryInputStatus::InvalidFloat)
            first.status=QueryInputStatus::InvalidFloat;
        else if(first!=second)first.status=QueryInputStatus::Stale;
    }
    return s;
}
} // namespace horse_motion::native_constraint_probe
