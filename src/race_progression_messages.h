#pragma once
#include "race_progression.h"
#include "race_progression_icons.h"

namespace horse_races::progression {
struct NoticeText {std::string title,body;int duration_ms=6000;std::optional<map_textures::Dictionary> icon;};
// Enrollment confirmation, distinct from earning the honorary qualification.
// Send through progression_toast::Show; do not duplicate it as a world subtitle.
inline NoticeText MembershipRegistered(bool honorary,bool chinese){
    return {
        honorary?(chinese?"赛马会荣誉会员":"HONORARY MEMBERSHIP"):
            (chinese?"赛马会会员":"CLUB MEMBERSHIP"),
        chinese?"入会手续已办妥。欢迎参加赛马会赌马赛。":
            "Your membership is registered. Horse betting is now open to you.",
        honorary?8000:6000};
}
inline std::string RaceName(Race race,bool chinese){
    struct Name {const char* en;const char* zh;};
    static constexpr std::array<Name,kKeys.size()> names{{
        {"Mary-Beth's Duel","玛丽贝思对决赛"},{"Pearson's Duel","皮尔逊对决赛"},
        {"Karen's Duel","凯伦对决赛"},{"Charles's Duel","查尔斯对决赛"},
        {"Bill's Duel","比尔对决赛"},{"Sadie's Duel","莎迪对决赛"},{"Javier's Duel","哈维尔对决赛"},
        {"Appaloosa Challenge","阿帕卢萨马限定赛"},{"Kentucky Saddler Challenge","肯塔基骑乘马限定赛"},
        {"Tennessee Walker Challenge","田纳西走马限定赛"},{"Standardbred Challenge","标准种马限定赛"},
        {"Paint Challenge","花马限定赛"},{"Arabian Challenge","阿拉伯马限定赛"},{"Mustang Challenge","美洲野马限定赛"},
        {"Arthur's Thoroughbred Challenge","亚瑟的纯血马限定赛"},{"Arthur's Duel","亚瑟对决赛"},
        {"John's Thoroughbred Challenge","约翰的纯血马限定赛"}
    }};
    if(Index(race)>=names.size())return {};
    const auto& name=names[Index(race)];
    return chinese?name.zh:name.en;
}
inline NoticeText Victory(std::optional<Race> race,bool chinese){
    return {chinese?"比赛获胜":"RACE WON",
        race?RaceName(*race,chinese):(chinese?"赛马":"Horse Race"),4000};
}
inline NoticeText Notice(std::optional<Race> race,bool chinese){
    if(!race)return {
        chinese?"赛马挑战全部完成":"ALL RACING CHALLENGES COMPLETE",
        chinese?"出售一匹有胜绩的赛马后，即可前往经纪人处免费入会。":
            "After selling a winning horse, visit the Club Agent for free enrollment.",8000};
    const auto name=RaceName(*race,chinese);
    if(name.empty())return {};
    return {chinese?"新赛事开放":"NEW RACE AVAILABLE",
        name+(chinese?"现已开放。":" is now open."),6000,RaceIconDictionary(*race)};
}
}
