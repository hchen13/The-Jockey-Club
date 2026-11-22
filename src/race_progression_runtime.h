#pragma once
#include "race_progression.h"
#include "game_natives.h"
#include "race_localization.h"
#include <functional>
#include <cstdint>

namespace horse_races::progression::runtime {
inline State state; // Refreshable view; the archive owns the authoritative state.
inline bool initialized=false,unsaved=false;
inline std::string status;
inline std::function<void(const std::string&)> log;
inline Era current_era=Era::None;
struct ArchiveView {std::uint64_t generation{};std::string encoded;};
using ArchiveReader=std::function<std::optional<ArchiveView>()>;
using ArchiveWriter=std::function<bool(std::uint64_t,const std::string&,const std::string&)>;
using ArchiveIssueReader=std::function<std::string()>;
inline ArchiveReader archive_read;
inline ArchiveWriter archive_write;
inline ArchiveIssueReader archive_issue;
inline std::optional<ArchiveView> archive_view;
namespace detail {
// Separate from the public compatibility view: direct writes to `state` must
// never change the parsed archive used by a cache hit.
inline std::optional<State> parsed_archive;
inline std::string shown_archive_issue;
}
inline void SetArchiveProvider(ArchiveReader read,ArchiveWriter write,ArchiveIssueReader issue={}){
    archive_read=std::move(read);archive_write=std::move(write);archive_issue=std::move(issue);archive_view.reset();
    detail::parsed_archive.reset();detail::shown_archive_issue.clear();state={};unsaved=false;status.clear();
}
inline std::string CurrentArchiveIssue(){
    try{return archive_issue?archive_issue():std::string{};}catch(...){return {};}
}
inline std::string StatusWithArchiveIssue(std::string fallback){
    const auto issue=CurrentArchiveIssue();return issue.empty()?std::move(fallback):issue;
}
inline bool Refresh(){
    const auto previous_generation=archive_view?archive_view->generation:0;
    const auto clear=[](){archive_view.reset();detail::parsed_archive.reset();state={};unsaved=false;};
    if(!initialized||!archive_read){clear();return false;}
    const auto issue=CurrentArchiveIssue();
    if(!issue.empty())status=issue;
    else if(!detail::shown_archive_issue.empty()&&status==detail::shown_archive_issue)status.clear();
    detail::shown_archive_issue=issue;
    const auto unavailable=[&](){clear();status=issue.empty()?i18n::Text("Current save archive is not ready.","当前存档档案尚未就绪。"):issue;return false;};
    try{
        const auto view=archive_read();
        if(!view||!view->generation)return unavailable();
        if(archive_view&&detail::parsed_archive&&archive_view->generation==view->generation&&
           archive_view->encoded==view->encoded){
            state=*detail::parsed_archive;unsaved=false;return true;
        }
        const auto parsed=State::Decode(view->encoded);
        if(!parsed)return unavailable();
        if(previous_generation!=view->generation&&issue.empty())status.clear();
        archive_view=view;detail::parsed_archive=*parsed;state=*parsed;unsaved=false;return true;
    }catch(...){return unavailable();}
}
// Never retry an old cached state into a later loaded world. The archive writer
// must atomically compare generation AND prior bytes before accepting replacement.
inline bool Commit(const State& replacement){
    if(!archive_view||!archive_write)return false;
    const auto expected=*archive_view;const auto bytes=replacement.Encode();
    if(!Refresh()||archive_view->generation!=expected.generation||archive_view->encoded!=expected.encoded)return false;
    bool accepted=false;
    try{accepted=archive_write(expected.generation,expected.encoded,bytes);}catch(...){}
    const bool ready=Refresh();
    return accepted&&ready&&archive_view->generation==expected.generation&&archive_view->encoded==bytes;
}
inline std::function<bool(Era)> membership_provider;
inline std::function<void(std::optional<Race>)> notice_handler;
inline void SetNoticeHandler(std::function<void(std::optional<Race>)> handler){notice_handler=std::move(handler);}
inline void SetMembershipProvider(std::function<bool(Era)> provider){membership_provider=std::move(provider);}
inline bool EffectiveBetting(Era era){
    return (era==Era::Arthur||era==Era::John)&&membership_provider&&membership_provider(era);
}
inline Era ObserveEra(){
    const auto player=PLAYER::PLAYER_PED_ID();
    return player&&ENTITY::DOES_ENTITY_EXIST(player)?world::ProtagonistOf(ENTITY::GET_ENTITY_MODEL(player)):Era::None;
}
inline Era RealEra(){return initialized?current_era:ObserveEra();}
inline bool HonoraryMembershipEligible(Era era){return initialized&&Refresh()&&era==RealEra()&&state.HonoraryMembershipEligible(era);}
inline void Initialize(const std::string&,std::function<void(const std::string&)> logger){
    // The old global progress.ini is deliberately neither read nor written.
    initialized=true;state={};unsaved=false;archive_view.reset();log=std::move(logger);status.clear();current_era=ObserveEra();Refresh();
}
inline bool Open(std::string_view key){
    if(!initialized)return true; // Isolated native diagnostic fixtures only.
    if(!Refresh())return false;
    const auto race=Find(key);return race&&state.Open(RealEra(),*race);
}
inline bool Betting(){return !initialized||(Refresh()&&EffectiveBetting(RealEra()));}
inline bool Camp(){return !initialized||(Refresh()&&state.Camp(RealEra()));}
inline void ClearHonoraryQualification(){
    if(!initialized||!Refresh())return;
    auto replacement=state;
    if(!replacement.ClearHonoraryQualification(RealEra()))return;
    if(Commit(replacement)){if(log)log("honorary membership qualification cleared in current archive");}
    else status=StatusWithArchiveIssue(i18n::Text("Current save archive did not accept the change.","当前存档档案未接受此更改。"));
}
inline void Win(std::string_view key,bool practice,bool substituted_host){
    if(!initialized||!Refresh())return;
    const auto race=Find(key);const auto era=RealEra();
    if(!race||era==Era::None)return;
    const auto before=state.For(era);auto replacement=state;
    if(!replacement.Win(era,*race,practice,substituted_host))return;
    const bool saved=Commit(replacement);
    status=StatusWithArchiveIssue(i18n::Text(saved?"Victory recorded.":"Current save archive did not accept the victory.",saved?"胜绩已记入赛程。":"当前存档档案未接受此胜绩。"));
    if(saved&&notice_handler){
        const auto opened=state.For(era).unlocked&~before.unlocked;
        for(std::size_t i=0;i<kKeys.size();++i)if(opened.test(i))notice_handler(static_cast<Race>(i));
        if(!before.honorary&&state.HonoraryMembershipEligible(era))notice_handler(std::nullopt);
    }
    if(log)log("progression victory station="+std::string(key)+" accepted="+std::to_string(saved));
}
inline ActionResult Perform(Action action,bool busy){
    if(!initialized||action==Action::None)return ActionResult::Ignored;
    const bool ready=Refresh();const auto era=RealEra();
    const auto report=[&](ActionResult result){
        if(log)log("race progression action="+std::to_string(static_cast<int>(action))+" result="+std::to_string(static_cast<int>(result))+" archive_ready="+std::to_string(archive_view.has_value()));
        return result;
    };
    if(!ready||busy||era==Era::None){
        status=StatusWithArchiveIssue(!ready?i18n::Text("Current save archive is not ready.","当前存档档案尚未就绪。"):
            i18n::Text("Finish the current event before changing progress.","请先结束当前活动，再调整赛马进度。"));
        return report(ActionResult::Blocked);
    }
    auto replacement=state;
    if(action==Action::Reset)replacement={};
    else if(action==Action::UnlockNext)replacement.UnlockNext(era);
    else if(action==Action::LockPrevious)replacement.LockPrevious(era);
    else if(action==Action::UnlockAll)replacement.UnlockAll(era);
    if(replacement==state){
        status=StatusWithArchiveIssue(action==Action::Reset||action==Action::LockPrevious?i18n::Text("Already at first progression step.","已是初始进度。"):
            i18n::Text("All available race progress is already open.","全部赛马进度已开放。"));
        return report(ActionResult::Unchanged);
    }
    if(!Commit(replacement)){
        status=StatusWithArchiveIssue(i18n::Text("Current save archive did not accept the change.","当前存档档案未接受此更改。"));
        return report(ActionResult::Blocked);
    }
    if(action==Action::Reset)status=i18n::Text("Both campaigns reset. Only Mary-Beth's duel is open.","两位主角赛程已重置，仅开放玛丽贝思对决赛。");
    else if(action==Action::UnlockAll)status=i18n::Text("All races completed. Sell a winning racehorse to qualify for free enrollment.","全部赛事已完成；成功出售有胜绩的马后，可免费入会。");
    else if(action==Action::UnlockNext)status=i18n::Text("Race progress advanced by one step.","已推进一条赛马进度。");
    else status=i18n::Text("Race progress moved back by one step.","已回退一条赛马进度。");
    status=StatusWithArchiveIssue(std::move(status));
    return report(ActionResult::Applied);
}
inline void Tick(double){current_era=ObserveEra();if(initialized)Refresh();}
inline std::string CountSummary(bool victories){
    const auto era=RealEra();if(!initialized||era==Era::None||!Refresh())return "--";
    std::size_t total=0,count=0;const auto& entries=victories?state.For(era).won:state.For(era).unlocked;
    for(std::size_t i=0;i<kKeys.size();++i)if(Eligible(era,static_cast<Race>(i))){++total;if(entries.test(i))++count;}
    return std::string(victories?i18n::Text("Won ","获胜 "):i18n::Text("Open ","开放 "))+std::to_string(count)+"/"+std::to_string(total);
}
inline std::string Summary(){return CountSummary(false);}
inline std::string WinSummary(){return CountSummary(true);}
} // namespace horse_races::progression::runtime
