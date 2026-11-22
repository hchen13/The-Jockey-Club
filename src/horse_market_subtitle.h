#pragma once
#include <string>
#include <string_view>

namespace horse_races::horse_market::subtitle {
inline std::string Text(std::string_view name){
    // Preserve the native PM_SUB_HEADER_TEXT font/style/layout. UIText is
    // rich text, so arbitrary player names must not become markup/commands.
    std::string result;
    for(char c:name){
        switch(c){
        case '&':result+="&amp;";break;
        case '<':result+="&lt;";break;
        case '>':result+="&gt;";break;
        // Player names are literal text, never HUD formatting commands.
        case '~':result+="&#126;";break;
        case '\r':case '\n':result+=' ';break;
        default:result+=c;
        }
    }
    return result;
}
}
