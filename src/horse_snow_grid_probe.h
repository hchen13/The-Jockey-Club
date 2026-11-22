#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Read-only, script-thread diagnostic shadow of a build-guarded native grid.
// The engine's byte meaning and update freshness are NOT established. Never
// use these values as a terrain decision or a controller input.
namespace horse_motion::snow_grid_probe {
enum class Status : unsigned {Unsupported=0,Unavailable=1,Invalid=2,OutOfBounds=3,Changed=4,Ready=5};
inline constexpr std::uintptr_t kPointerRva=0x4c3c9c8;
struct Header {
    Status status=Status::Unavailable;
    std::uintptr_t object=0;
    std::array<float,4> center{};
    float width=0;
    std::uint8_t flag=0;
};
struct Sample {
    Status status=Status::Unavailable;
    std::array<float,4> center{};
    float width=0;
    std::uint8_t flag=0,raw=0;
    int x=-1,y=-1;
};
inline bool Pointer(std::uintptr_t p){return p>=0x10000 && p<=0x00007ffffffff000ULL;}
template<class Memory,class T> bool Read(Memory& m,std::uintptr_t p,T& value){
    return p>=0x10000 && p<=0x00007ffffffff000ULL && sizeof(value)<=128 && m.Read(p,&value,sizeof(value));
}
template<class Memory> Header ReadHeader(Memory& m,std::uintptr_t base){
    Header h;
    if(!Pointer(base) || base>0x00007ffffffff000ULL-kPointerRva ||
        !snow_grid_probe::Read(m,base+kPointerRva,h.object) || !Pointer(h.object) ||
        !snow_grid_probe::Read(m,h.object+0x360,h.center) || !snow_grid_probe::Read(m,h.object+0x384,h.width) ||
        !snow_grid_probe::Read(m,h.object+0x42a,h.flag))return h;
    h.status=Status::Invalid;
    if(!h.flag || !std::isfinite(h.width) || h.width<=0.01F || h.width>100000.F)return h;
    for(std::size_t i=0;i<2;++i)
        if(!std::isfinite(h.center[i]) || std::abs(h.center[i])>100000.F)return h;
    h.status=Status::Ready;return h;
}
inline bool Same(const Header& a,const Header& b){
    return a.status==Status::Ready && b.status==Status::Ready && a.object==b.object &&
        std::memcmp(a.center.data(),b.center.data(),2*sizeof(float))==0 &&
        std::memcmp(&a.width,&b.width,sizeof(a.width))==0 && a.flag==b.flag;
}
template<class Memory> Header CaptureHeader(Memory& m,std::uintptr_t base){
    auto h=ReadHeader(m,base);if(h.status!=Status::Ready)return h;
    const auto second=ReadHeader(m,base);if(!Same(h,second))h.status=Status::Changed;
    return h;
}
template<class Memory> Sample Capture(Memory& m,std::uintptr_t base,const Header& shared,float world_x,float world_y){
    Sample s;s.status=shared.status;s.center=shared.center;s.width=shared.width;s.flag=shared.flag;
    if(shared.status!=Status::Ready)return s;
    if(!std::isfinite(world_x) || !std::isfinite(world_y) ||
        std::abs(world_x)>100000.F || std::abs(world_y)>100000.F){s.status=Status::Invalid;return s;}
    const float scale=32.F/shared.width,half=shared.width*.5F;
    const float fx=((world_x-shared.center[0])+half)*scale;
    const float fy=((world_y-shared.center[1])+half)*scale;
    // Native truncates toward zero; reject negative fractional cells instead
    // of treating just-outside coordinates as evidence inside the grid.
    if(!std::isfinite(fx)||!std::isfinite(fy)||fx<0||fy<0||fx>=32||fy>=32){s.status=Status::OutOfBounds;return s;}
    s.x=static_cast<int>(fx);s.y=static_cast<int>(fy);
    const auto first=ReadHeader(m,base);if(!Same(shared,first)){s.status=Status::Changed;return s;}
    const auto address=shared.object+0x470+static_cast<std::uintptr_t>(s.y*32+s.x);
    std::uint8_t a=0,b=0;if(!snow_grid_probe::Read(m,address,a)||!snow_grid_probe::Read(m,address,b)){s.status=Status::Unavailable;return s;}
    const auto last=ReadHeader(m,base);
    if(a!=b||!Same(shared,last)){s.status=Status::Changed;return s;}
    s.raw=a;s.status=Status::Ready;return s;
}
} // namespace horse_motion::snow_grid_probe
