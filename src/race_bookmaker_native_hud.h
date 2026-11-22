#pragma once
#include "game_natives.h"
#include "race_map_textures.h"
#include "race_bookmaker_native_standings.h"
#include "race_bookmaker_portrait_renderer.h"
#include <limits>
#include <string_view>

namespace horse_races::bookmaking {
struct NativeStandingsApi {
    using Handle=Any;
    bool Valid(Handle h) const{
        return h && h<=std::numeric_limits<Hash>::max() &&
            DATABINDING::DATABINDING_IS_ENTRY_VALID(static_cast<Hash>(h));
    }
    Handle FindRoot(const char* name) const{
        return DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH(name);
    }
    Handle AddRoot(const char* name) const{
        return DATABINDING::_DATABINDING_ADD_DATA_CONTAINER_FROM_PATH("",name);
    }
    Handle AddContainer(Handle parent,const char* name) const{
        return DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(parent),name);
    }
    Handle AddBool(Handle parent,const char* name,bool value) const{
        return DATABINDING::_DATABINDING_ADD_DATA_BOOL(parent,name,value);
    }
    Handle AddInt(Handle parent,const char* name,int value) const{
        return DATABINDING::_DATABINDING_ADD_DATA_INT(parent,name,value);
    }
    Handle AddFloat(Handle parent,const char* name,float value) const{
        return DATABINDING::_DATABINDING_ADD_DATA_FLOAT(parent,name,value);
    }
    Handle AddString(Handle parent,const char* name,const char* value) const{
        return DATABINDING::_DATABINDING_ADD_DATA_STRING(parent,name,value);
    }
    Handle AddHash(Handle parent,const char* name,const char* value) const{
        return DATABINDING::_DATABINDING_ADD_DATA_HASH(parent,name,
            value && *value?MISC::GET_HASH_KEY(value):0);
    }
    int ReadInt(Handle h) const{return DATABINDING::DATABINDING_READ_INT(h);}
    void WriteBool(Handle h,bool value) const{
        DATABINDING::_DATABINDING_WRITE_DATA_BOOL(h,value);
    }
    void WriteString(Handle h,const char* value) const{
        DATABINDING::_DATABINDING_WRITE_DATA_STRING(h,value);
    }
    void WriteHash(Handle h,const char* value) const{
        DATABINDING::_DATABINDING_WRITE_DATA_HASH_STRING(h,
            value && *value?MISC::GET_HASH_KEY(value):0);
    }
    void WriteFloat(Handle h,float value) const{
        DATABINDING::_DATABINDING_WRITE_DATA_FLOAT(h,value);
    }
    void RequestTexture(const char* dict) const{
        if(std::string_view(dict)=="blips")
            map_textures::Acquire(map_textures::Dictionary::Story,map_textures::Owner::BettingStandings);
        else TXD::REQUEST_STREAMED_TEXTURE_DICT(dict,false);
    }
    bool TextureLoaded(const char* dict) const{
        return TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(dict);
    }
    void ReleaseTexture(const char* dict) const{
        if(std::string_view(dict)=="blips")
            map_textures::Release(map_textures::Dictionary::Story,map_textures::Owner::BettingStandings);
        else TXD::SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED(dict);
    }
    void RemoveRoot(Handle h) const{
        DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(h));
    }
    Handle RequestFlow(int hash) const{return UISTATEMACHINE::_UIFLOWBLOCK_REQUEST(hash);}
    bool FlowLoaded(Handle h) const{return UISTATEMACHINE::_UIFLOWBLOCK_IS_LOADED(h)!=0;}
    void EnterFlow(Handle h,Hash style) const{UISTATEMACHINE::_UIFLOWBLOCK_ENTER(h,style);}
    void ReleaseFlow(Handle& h) const{
        // Decompiled 1436 Story scripts pass &handle; the SDK's Any parameter
        // obscures the pointer ABI. Native flow handles are 32-bit script ints.
        Hash flow_handle=static_cast<Hash>(h);
        UISTATEMACHINE::_UIFLOWBLOCK_RELEASE(reinterpret_cast<Any>(&flow_handle));
        h=flow_handle;
    }
    Hash StyleHash(const char* name) const{return MISC::GET_HASH_KEY(name);}
    bool MachineExists(int hash) const{return UISTATEMACHINE::UI_STATE_MACHINE_EXISTS(hash)!=0;}
    void CreateMachine(int hash,Handle flow) const{
        UISTATEMACHINE::UI_STATE_MACHINE_CREATE(hash,flow);
    }
    bool MachineCanExit(int hash) const{
        return UISTATEMACHINE::UI_STATE_MACHINE_CAN_REQUEST_TRANSITION(hash)!=0;
    }
    void RequestExit(int hash) const{UISTATEMACHINE::UI_STATE_MACHINE_REQUEST_EXIT(hash,0);}
    bool MachineExited(int hash) const{return UISTATEMACHINE::_UI_STATE_MACHINE_IS_EXITED(hash);}
    void DestroyMachine(int hash) const{UISTATEMACHINE::UI_STATE_MACHINE_DESTROY(hash);}
    void EnableHudContext() const{
        HUD::_ENABLE_HUD_CONTEXT(MISC::GET_HASH_KEY("HUD_CTX_MP_INSTANCED_TOP_RIGHT_HUD"));
    }
    void DisableHudContext() const{
        HUD::_DISABLE_HUD_CONTEXT(MISC::GET_HASH_KEY("HUD_CTX_MP_INSTANCED_TOP_RIGHT_HUD"));
    }
};
using NativeStandingsHud=NativeStandingsHudT<NativeStandingsApi>;
struct NativePortraitSpriteApi {
    bool TextureLoaded(const char* dict) const{
        return TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(dict);
    }
    void SetDrawOrder(int order) const{GRAPHICS::SET_SCRIPT_GFX_DRAW_ORDER(order);}
    void DrawSprite(const char* dict,const char* texture,float x,float y,float w,float h,int shade) const{
        GRAPHICS::DRAW_SPRITE(dict,texture,x,y,w,h,0.0F,shade,shade,shade,255,false);
    }
};
} // namespace horse_races::bookmaking
