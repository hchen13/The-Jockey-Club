#pragma once
#include "race_bookmaker_portrait_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <windows.h>

namespace horse_races::bookmaking {

// LEGACY VISIBILITY RESTORE (0.28.32), NOT AN EXACT ALIGNMENT SOLUTION.
// The stock 2+8 meter flow puts the two principal rings just outside the
// rendered title, not at a fixed x offset. There is no exposed FlowBlock
// rectangle native in our bridge. This is the previously visible sprite path;
// its Windows font proxy is approximate and English first-row alignment is a
// known defect. Do not add venue/length corrections or claim exact geometry.
// Replacing it requires actual widget bounds or a single shared composition,
// with in-game evidence before removing this visible path again.
inline float StandingTitleAdvance1440(std::string_view title){
    if(title.empty())return 0.0F;
    const int bytes=static_cast<int>(title.size());
    const int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,title.data(),bytes,nullptr,0);
    if(chars<=0)return 0.0F;
    std::wstring wide(static_cast<std::size_t>(chars),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,title.data(),bytes,wide.data(),chars)!=chars)
        return 0.0F;
    const bool han=std::any_of(wide.begin(),wide.end(),[](wchar_t ch){
        return (ch>=0x3400 && ch<=0x9FFF) || (ch>=0xF900 && ch<=0xFAFF);
    });
    // Screenshot calibration at 3440x1440: six/seven CJK glyphs advance at
    // ~31 px each; the condensed Latin title has Impact-like advances at 38 px.
    const HFONT font=CreateFontW(han?-31:-38,0,0,0,han?FW_BOLD:FW_NORMAL,
        FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,DEFAULT_PITCH,han?L"SimSun":L"Impact");
    if(!font)return 0.0F;
    HDC dc=GetDC(nullptr);
    if(!dc){DeleteObject(font);return 0.0F;}
    const HGDIOBJ old=SelectObject(dc,font);
    SIZE extent{};
    const bool measured=GetTextExtentPoint32W(dc,wide.data(),chars,&extent)!=FALSE;
    if(old)SelectObject(dc,old);
    ReleaseDC(nullptr,dc);
    DeleteObject(font);
    if(!measured)return 0.0F;
    // The MP title font gives each W a wider advance than the Windows Impact
    // proxy. Its stock ring centers were measured from the live English HUD:
    // RHODES HANDICAP needs -6 px from the proxy, while STRAWBERRY WFA needs
    // a further +8 px for each W. This is a glyph metric correction, not a
    // title/venue switch, so translated and future titles still scale.
    const float latin_correction=han?0.0F:
        -6.0F+8.0F*static_cast<float>(std::count(wide.begin(),wide.end(),L'W'));
    return std::max(0.0F,static_cast<float>(extent.cx)+latin_correction);
}

// title_advance_1440 is the measured advance at the reference HUD height.
inline std::array<PortraitSlot,10> PortraitSlots(std::size_t count,int width,int height,
    float title_advance_1440){
    std::array<PortraitSlot,10> slots{};
    if(width<=0 || height<=0)return slots;
    count=std::min<std::size_t>(count,slots.size());
    const float scale=static_cast<float>(height)/1440.0F;
    const float middle=static_cast<float>(width)*0.5F;
    // Ring centers follow the native title width; the stock MP layout itself
    // is centered 1 px to the right of the window midpoint at 3440x1440.
    const float principal_offset=59.5F+std::max(0.0F,title_advance_1440)*0.5F;
    constexpr float secondary_pitch=97.0F;
    constexpr float principal_art_x_offset=1.0F;
    constexpr float secondary_art_x_offset=1.0F;
    // Script sprites are the only portrait path confirmed to render in-game.
    // They cannot be drawn beneath the flow's progress ring, so fit the photo
    // within its black center instead of covering the original ring.
    constexpr float portrait_diameter=62.0F;
    if(count>0)slots[0]={middle+(-principal_offset+principal_art_x_offset)*scale,
        81.0F*scale,portrait_diameter*scale};
    if(count>1)slots[1]={middle+(principal_offset+principal_art_x_offset)*scale,
        81.0F*scale,portrait_diameter*scale};
    const std::size_t below=count>2?count-2:0;
    for(std::size_t i=0;i<below;++i){
        const float offset=(static_cast<float>(i)-static_cast<float>(below-1)*0.5F)*secondary_pitch;
        slots[i+2]={middle+(offset+secondary_art_x_offset)*scale,
            179.0F*scale,portrait_diameter*scale};
    }
    return slots;
}

} // namespace horse_races::bookmaking
