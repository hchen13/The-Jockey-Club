#pragma once
#include "race_localization.h"
#include "bg_literal_text.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace horse_races::countdown_hud {
// A small, stateless overlay using the same SP font renderer as the settings.
// No opaque panel, streamed texture, MP state machine or shared HUD ownership.
inline void Text(const std::string& value,float x,float y,float scale,int alpha,bool gold=false) {
    const bool latin=std::all_of(value.begin(),value.end(),[](unsigned char c){return c<128;});
    const std::string formatted=std::string("~s~<FONT FACE='")+(latin?"$Font5":"$body")+"'>"+value+"</FONT>";
    UIDEBUG::_BG_SET_TEXT_SCALE(0,scale);
    UIDEBUG::_BG_SET_TEXT_COLOR(12,10,8,alpha);
    bg_text::Display(formatted,x+.001F,y+.0015F);
    UIDEBUG::_BG_SET_TEXT_SCALE(0,scale);
    UIDEBUG::_BG_SET_TEXT_COLOR(gold?238:245,gold?194:239,gold?112:225,alpha);
    bg_text::Display(formatted,x,y);
}
inline void Draw(int digit,double elapsed,double go_left=0) {
    if(HUD::IS_PAUSE_MENU_ACTIVE() || HUD::IS_HUD_HIDDEN())return;
    int width=0,height=0;GRAPHICS::GET_SCREEN_RESOLUTION(&width,&height);
    const float aspect=width>0 && height>0?static_cast<float>(width)/height:16.0F/9;
    const float horizontal=(16.0F/9)/std::clamp(aspect,1.0F,4.0F);
    const bool go=digit==0;
    const int alpha=go?static_cast<int>(255*std::clamp(go_left/.25,0.0,1.0)):255;
    const float progress=go?3.0F:static_cast<float>(std::clamp(elapsed,0.0,3.0));
    const std::string value=go?"GO":std::to_string(std::clamp(digit,1,3));
    Text(i18n::Text("GET READY","准备起跑"),.5F-(i18n::Chinese()?.031F:.047F)*horizontal,.249F,.27F,go?0:220);
    Text(value,.5F-(go?.036F:.0135F)*horizontal,.285F,1.25F,alpha,go);
    // Three fine ticks empty in elapsed order; no flashing/pulsating numeral.
    for(int i=0;i<3;++i) {
        const float x=.5F+(i-1)*.032F*horizontal,w=.025F*horizontal;
        GRAPHICS::DRAW_RECT(x,.407F,w,.004F,12,10,8,alpha*3/4,false,true);
        const float remaining=go?1.0F:std::clamp(1.0F-(progress-i),0.0F,1.0F);
        if(remaining>0)GRAPHICS::DRAW_RECT(x-w*(1-remaining)/2,.407F,w*remaining,.002F,
            238,194,112,alpha,false,true);
    }
    GRAPHICS::DRAW_RECT(.5F-.066F*horizontal,.350F,.028F*horizontal,.0012F,245,239,225,alpha*2/3,false,true);
    GRAPHICS::DRAW_RECT(.5F+.066F*horizontal,.350F,.028F*horizontal,.0012F,245,239,225,alpha*2/3,false,true);
}
}
