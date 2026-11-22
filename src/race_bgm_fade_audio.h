#pragma once
#include "race_bgm_native_audio.h"

namespace horse_races::bgm::audio::fade {
struct Owner {
    int id=-1,slot=-1,context=-1;
    bool sound=false,allow_preload=false;
    std::uint32_t thread=0,cue=0;
    std::uint64_t source=0;
    std::uintptr_t root=0,raw=0,table=0;
    unsigned char type=0;
    bool operator==(const Owner&) const=default;
};
struct Volume {
    Owner owner;
    float initial=0,current=0;
};

// This only adjusts the root of an owned voice. Fade capture requires active
// playback; volume settings can explicitly include a ready stream so its
// attenuation is applied before PLAY_STREAM_FRONTEND. It never edits shared
// REL data, a global mix, or any individual Venom stem.
template<class Memory> struct Inspection {
    Memory memory;
    std::uintptr_t base=0;
    template<class T> bool Read(std::uintptr_t at,T& value) const {return memory.Read(at,value);}
    bool CaptureOwner(int id,bool sound,Owner& out,bool allow_preload=false) const {
        if(!base || id<0 || (sound && id>=100))return false;
        Owner v;v.id=id;v.sound=sound;v.allow_preload=allow_preload;
        if(sound) {
            const auto slot=base+kManager+0x5d0+id*0x80;
            unsigned char owned=0;
            if(!Read(slot+0x40,v.context) || !Read(slot+0x44,owned) || owned!=1 ||
                !Read(slot+4,v.cue) || !Read(slot+0xc,v.source) || !Read(slot+0x18,v.root))return false;
        } else {
            for(int i=0;i<16;++i){int tag=-1;
                if(!Read(base+kManager+0xaa24+i*0x260,tag))return false;
                if(tag==id){if(v.slot>=0)return false;v.slot=i;}}
            if(v.slot<0)return false;
            const auto slot=base+kManager+v.slot*0x260;
            int state=0;
            if(!Read(slot+0xaa14,state) || (state!=3 && !(allow_preload && state==2)) || !Read(slot+0xaa20,v.context) ||
                !Read(slot+0xaa18,v.source) || !Read(slot+0xa9f8,v.root))return false;
        }
        if(v.context<0 || v.context>=32 || !v.source || v.root<0x10000 ||
            !Read(base+kManager+0x37d0+v.context*0x380,v.thread) ||
            !Read(v.root,v.table) || !Read(v.root+0x60,v.raw) || v.raw<0x10000 ||
            !Read(v.root+0xf2,v.type))return false;
        v.type&=0x3f;
        if(!((v.type==6 && v.table==base+0x360f1c0) ||
             (v.type==8 && v.table==base+0x360f128)))return false;
        unsigned char optimizer=0,inherit=0;std::uint32_t secondary=0;
        if(!Read(v.root+0xef,optimizer) || (optimizer&2) ||
            !Read(v.root+0xeb,inherit) || (inherit&1) ||
            !Read(v.root+0x84,secondary) || (secondary&0xc0000)==0x40000)return false;
        out=v;return true;
    }
    bool Same(const Owner& v) const {
        Owner first,second;
        return CaptureOwner(v.id,v.sound,first,v.allow_preload) && first==v &&
            CaptureOwner(v.id,v.sound,second,v.allow_preload) && second==v;
    }
    bool Capture(int id,bool sound,Volume& out,bool allow_preload=false) const {
        Volume v;
        if(!CaptureOwner(id,sound,v.owner,allow_preload) || !Read(v.owner.root+0x90,v.initial) ||
            !std::isfinite(v.initial) || v.initial < -100 || v.initial > 24 || !Same(v.owner))return false;
        v.current=v.initial;out=v;return true;
    }
    bool Set(Volume& v,float db) const {
        if(!std::isfinite(db) || db < -100 || db > v.current || !Same(v.owner))return false;
        if(db!=v.current && !memory.WriteFloat(v.owner.root+0x90,v.current,db))return false;
        v.current=db;
        float actual=0;
        return Same(v.owner) && Read(v.owner.root+0x90,actual) && actual==db;
    }
};
struct WindowsBridge {
    static Inspection<WindowsMemory> View(){return {{},reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))};}
    static bool Capture(int id,bool sound,Volume& out,bool allow_preload=false){
        return audio::WindowsBridge::GainSupported() &&
            (!sound || audio::WindowsBridge::OffsetSupported()) && View().Capture(id,sound,out,allow_preload);
    }
    static bool Set(Volume& out,float db){
        return audio::WindowsBridge::GainSupported() &&
            (!out.owner.sound || audio::WindowsBridge::OffsetSupported()) && View().Set(out,db);
    }
};
} // namespace horse_races::bgm::audio::fade
