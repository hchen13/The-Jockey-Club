#pragma once
#include "race_bookmaker_native_standings.h"
#include <array>
#include <cmath>
#include <span>

namespace horse_races::bookmaking {
struct PortraitSlot {float x=0,y=0,diameter=0;};
struct PortraitDrawReport {
    std::array<bool,10> drawn{};
    std::size_t count=0;
};

// This is the explicit draw path that rendered in the user's game before
// 0.28.31. Successful native image bindings must not silently replace it.
// Slots are pixels; normalize once against the same viewport for both axes.
template<class Api>
PortraitDrawReport DrawStandingPortraitSprites(Api& api,bool visible,
    std::span<const NativeStandingRow> rows,std::span<const PortraitSlot> slots,
    int width,int height){
    PortraitDrawReport result;
    if(!visible || width<=0 || height<=0)return result;
    const std::size_t count=std::min({rows.size(),slots.size(),result.drawn.size()});
    if(!count)return result;
    api.SetDrawOrder(4);
    for(std::size_t i=0;i<count;++i){
        const auto& row=rows[i];const auto& slot=slots[i];
        if(!row.portrait || !*row.portrait || !row.portrait_dict || !*row.portrait_dict ||
            !std::isfinite(slot.x) || !std::isfinite(slot.y) ||
            !std::isfinite(slot.diameter) || slot.diameter<=0 ||
            !api.TextureLoaded(row.portrait_dict))continue;
        const float x=slot.x/static_cast<float>(width),y=slot.y/static_cast<float>(height);
        const float w=slot.diameter/static_cast<float>(width),h=slot.diameter/static_cast<float>(height);
        api.DrawSprite(row.portrait_dict,row.portrait,x,y,w,h,row.out?105:255);
        result.drawn[i]=true;++result.count;
        if(row.out && api.TextureLoaded("menu_textures"))
            api.DrawSprite("menu_textures","cross",x,y,w*.72F,h*.72F,255);
    }
    api.SetDrawOrder(0);
    return result;
}
} // namespace horse_races::bookmaking
