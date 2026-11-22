#pragma once

#include "game_natives.h"
#include <string>

namespace horse_races::i18n {
// GET_CURRENT_LANGUAGE, not Windows/launcher language. 9=zh-TW, 12=zh-CN.
inline int language = 0;
inline bool Chinese() { return language == 9 || language == 12; }
inline bool Refresh()
{
    const int current = LOCALIZATION::GET_CURRENT_LANGUAGE();
    const bool changed = language != current;
    language = current;
    return changed;
}
inline const char* Text(const char* english, const char* chinese)
{
    return Chinese() ? chinese : english;
}
inline std::string Label(const std::string& english, const std::string& chinese)
{
    return Chinese() && !chinese.empty() ? chinese : english;
}
inline const char* Literal(const std::string& text)
{
    return MISC::VAR_STRING(10, "LITERAL_STRING", text.c_str());
}
inline const char* HorseName(const std::string& model)
{
    if (model == "A_C_HORSE_NOKOTA_WHITEROAN") return Text("White Roan Nokota", "白沙色诺科塔马");
    if (model == "A_C_HORSE_THOROUGHBRED_BRINDLE") return Text("Brindle Thoroughbred", "斑纹纯血马");
    if (model == "A_C_HORSE_AMERICANSTANDARDBRED_BLACK") return Text("Black American Standardbred", "黑色美国标准种马");
    if (model == "A_C_HORSE_MORGAN_BAY") return Text("Bay Morgan", "骝色摩根马");
    if (model == "A_C_HORSE_MUSTANG_GRULLODUN") return Text("Grullo Dun Mustang", "灰褐色野马");
    if (model == "A_C_HORSE_SHIRE_DARKBAY") return Text("Dark Bay Shire", "深骝色夏尔马");
    return Text("Horse", "马匹");
}
} // namespace horse_races::i18n
