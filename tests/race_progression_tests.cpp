#include "../src/race_progression_runtime.h"
#include "../src/race_progression_messages.h"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>

__declspec(dllimport) void setTestNativeHandler(void (*)(UINT64,const UINT64*,UINT64*));
using namespace horse_races;
namespace {
void Check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct FakeArchive {
    progression::State value;
    bool ready=true,accept=true;
    std::string issue;
    std::uint64_t generation=1;
    unsigned writes=0;
    std::function<void()> before_write;
    void Install(){progression::runtime::SetArchiveProvider(
        [this]()->std::optional<progression::runtime::ArchiveView>{
            if(!ready)return {};return progression::runtime::ArchiveView{generation,value.Encode()};
        },[this](std::uint64_t expected,const std::string& prior,const std::string& replacement){
            ++writes;if(before_write)before_write();
            if(!ready||!accept||generation!=expected||value.Encode()!=prior)return false;
            const auto parsed=progression::State::Decode(replacement);if(!parsed)return false;value=*parsed;return true;
        },[this](){return issue;});}
};
Hash model=world::kJohnModel;
void Native(UINT64 hash,const UINT64*,UINT64* out){
    if(hash==0x096275889B8E0EE0)out[0]=1; // PLAYER_PED_ID
    if(hash==0xD42BD6EB2E0F1677)out[0]=1; // DOES_ENTITY_EXIST
    if(hash==0xDA76A9F39210D365)out[0]=model;
}
void RollbackProgress(){
    using namespace progression;
    for(auto era:{Era::Arthur,Era::John}){
        State state;const auto fresh=state;
        Check(!state.LockPrevious(era)&&state==fresh,"rollback preserves fresh Mary floor");
        Check(!state.LockPrevious(Era::None)&&state==fresh,"unknown era rollback does nothing");
        const auto other=state.For(era==Era::John?Era::Arthur:Era::John);
        for(std::size_t i=1;i<Order(era).size();++i)state.UnlockNext(era);
        while(state.LockPrevious(era)){}
        Check(state==fresh,"repeated rollback inverts administrative chain to floor");
        Check(state.For(era==Era::John?Era::Arthur:Era::John)==other,"rollback never changes other protagonist");
        const auto second=Order(era)[1];
        Check(state.Win(era,Race::MaryDuel)&&state.Open(era,second),"real first win unlocks second stage");
        Check(state.LockPrevious(era)&&!state.Open(era,second)&&!state.For(era).won.test(Index(Race::MaryDuel)),"rollback clears predecessor win for replay");
        Check(state.Win(era,Race::MaryDuel)&&state.Open(era,second),"replaying predecessor restores removed stage");
        state.UnlockAll(era);Check(state.LockPrevious(era),"unlock-all can roll back");
        Check(!state.HonoraryMembershipEligible(era)&&state.Open(era,Order(era).back()),"first rollback removes only standalone honorary qualification");
        if(era==Era::Arthur){
            Check(state.LockPrevious(era)&&!state.Open(era,Race::Paint)&&!state.Open(era,Race::Arabian)&&!state.Open(era,Race::Mustang),"Arthur next rollback removes final stage and branch");
        }
        state.Reset(era);Check(!state.LockPrevious(era)&&state.For(era)==fresh.For(era),"reset restores nonremovable floor");
    }
    State john;
    for(auto race:kJohnOrder)Check(john.Win(Era::John,race),"complete actual John chain");
    Check(john.HonoraryMembershipEligible(Era::John)&&john.LockPrevious(Era::John)&&!john.HonoraryMembershipEligible(Era::John),"rollback clears real finale qualification");
    Check(john.Win(Era::John,Race::ArthurDuel)&&john.HonoraryMembershipEligible(Era::John),"replayed Arthur duel regrants honorary qualification");
    for(auto owner:{Race::SadieDuel,Race::Appaloosa,Race::Paint}){
        State arthur;
        for(auto race:kArthurOrder){Check(arthur.Win(Era::Arthur,race),"complete Arthur prefix");if(race==owner)break;}
        const auto check_side=[&](Race side){
            Check(arthur.Open(Era::Arthur,side),"owner victory unlocks optional side stage");
            Check(arthur.Win(Era::Arthur,side),"optional side stage can be won");
        };
        if(owner==Race::SadieDuel)check_side(Race::JavierDuel);
        if(owner==Race::Appaloosa){check_side(Race::Kentucky);check_side(Race::Tennessee);}
        if(owner==Race::Paint){check_side(Race::Arabian);check_side(Race::Mustang);}
        Check(arthur.LockPrevious(Era::Arthur),"branch owner progress can roll back");
        const auto closed=[&](Race side){Check(!arthur.Open(Era::Arthur,side)&&!arthur.arthur.won.test(Index(side)),"rollback removes optional unlock and win together");};
        if(owner==Race::SadieDuel)closed(Race::JavierDuel);
        if(owner==Race::Appaloosa){closed(Race::Kentucky);closed(Race::Tennessee);}
        if(owner==Race::Paint){closed(Race::Arabian);closed(Race::Mustang);}
        if(owner!=Race::Paint)Check(arthur.Win(Era::Arthur,owner),"branch owner replay cannot be stranded by prior won marker");
        else {Check(arthur.Win(Era::Arthur,Race::Standardbred)&&arthur.Win(Era::Arthur,Race::Paint),"final branch can replay predecessor then owner");}
        Check(State::Decode(arthur.Encode())==arthur,"rollback state roundtrips existing format");
    }
    State holes;holes.john.unlocked.set(Index(Race::BillDuel));holes.john.won.set(Index(Race::BillDuel));
    Check(holes.LockPrevious(Era::John)&&holes.Open(Era::John,Race::MaryDuel)&&!holes.Open(Era::John,Race::BillDuel),"rollback clears latest open stage despite old holes");
    Check(holes.UnlockNext(Era::John)&&holes.Open(Era::John,Race::PearsonDuel),"next after rollback fills earliest hole instead of skipping ahead");
}
void HonoraryQualificationAndMigration(){
    using namespace progression;
    for(auto era:{Era::John,Era::Arthur}){
        State state;std::size_t count=0;
        for(std::size_t i=0;i<kKeys.size();++i)if(Eligible(era,static_cast<Race>(i)))++count;
        Check(count==(era==Era::John?16u:15u),"honorary criteria use every protagonist-eligible race exactly once");
        for(auto race:Order(era))Check(state.Win(era,race),"actual main-chain wins recorded");
        if(era==Era::Arthur){
            Check(!state.HonoraryMembershipEligible(era),"Arthur main chain alone omits required western races");
            const std::array<Race,5> sides{Race::JavierDuel,Race::Kentucky,Race::Tennessee,Race::Arabian,Race::Mustang};
            for(std::size_t i=0;i<sides.size();++i){
                Check(state.Win(era,sides[i]),"actual optional race victory recorded");
                Check(state.HonoraryMembershipEligible(era)==(i+1==sides.size()),"Arthur honorary status waits for final missing eligible race");
            }
        }
        Check(state.AllEligibleWon(era)&&state.HonoraryMembershipEligible(era)&&!state.Betting(era),"full actual wins grant qualification only");
        const auto before=state.For(era).won;const auto other=state.For(era==Era::John?Era::Arthur:Era::John);
        Check(!state.ClearHonoraryQualification(Era::None),"unknown era cannot clear honorary state");
        Check(state.ClearHonoraryQualification(era)&&!state.ClearHonoraryQualification(era)&&state.For(era).won==before,"clear qualification returns exact change and retains every win");
        Check(state.For(era==Era::John?Era::Arthur:Era::John)==other,"clear qualification cannot affect other protagonist");
        const auto explicit_clear=State::Decode(state.Encode());
        Check(explicit_clear&&*explicit_clear==state&&!explicit_clear->HonoraryMembershipEligible(era),"new explicit honorary zero survives reload despite complete wins");
        Check(!state.Win(era,Race::MaryDuel,true)&&!state.Win(era,Race::MaryDuel,false,true)&&!state.HonoraryMembershipEligible(era),"practice and substituted-host replay cannot regrant qualification");
        Check(state.Win(era,Race::MaryDuel)&&state.HonoraryMembershipEligible(era)&&state.For(era).won==before,"actual repeat victory after clearing full completion regrants once without fabricating wins");
        Check(!state.Win(era,Race::MaryDuel),"repeated victory while already qualified is a no-op");
        auto old=state.Encode();
        for(const char* value:{"honorary=1\n","honorary=0\n"}){
            const std::string key=value;std::size_t at=0;
            while((at=old.find(key))!=std::string::npos)old.erase(at,key.size());
        }
        const auto upgraded=State::Decode(old);
        Check(upgraded&&upgraded->HonoraryMembershipEligible(era)&&!upgraded->Betting(era),"old complete actual wins upgrade to qualification without betting");
        State incomplete;
        // Persisted pre-0.28.61 open-only data still must not infer victories.
        for(std::size_t i=0;i<kKeys.size();++i)if(Eligible(era,static_cast<Race>(i)))incomplete.For(era).unlocked.set(i);
        Check(incomplete.Win(era,Race::MaryDuel)&&!incomplete.HonoraryMembershipEligible(era),"incomplete wins cannot recover honorary qualification from a single replay");
        Check(incomplete.Wins(era)==1,"legacy availability inferred unrecorded campaign wins");
        const auto untouched=incomplete.For(era==Era::John?Era::Arthur:Era::John);
        incomplete.UnlockAll(era);
        Check(incomplete.AllEligibleWon(era)&&incomplete.For(era).won.count()==count&&
            incomplete.For(era).unlocked.count()==count&&incomplete.HonoraryMembershipEligible(era)&&!incomplete.Betting(era),
            "F11 unlock-all must complete every eligible race including Arthur branches without enrollment");
        Check(incomplete.For(era==Era::John?Era::Arthur:Era::John)==untouched,"F11 unlock-all changed the other protagonist");
        const auto completed=incomplete;incomplete.UnlockAll(era);
        Check(incomplete==completed&&State::Decode(incomplete.Encode())==completed,"F11 complete-all is not idempotent/persistent");
    }
    const auto legacy=State::Decode("TheJockeyClubProgress=1\n[John]\nbetting=1\n[Arthur]\nbetting=1\n");
    Check(legacy&&legacy->HonoraryMembershipEligible(Era::John)&&!legacy->HonoraryMembershipEligible(Era::Arthur)&&!legacy->Betting(Era::John)&&!legacy->Betting(Era::Arthur),"legacy John betting migrates to qualification while historically ignored Arthur flag stays ignored");
    const auto explicit_zero=State::Decode("TheJockeyClubProgress=1\n[John]\nbetting=1\nhonorary=0\n[Arthur]\nhonorary=0\nbetting=1\n");
    Check(explicit_zero&&!explicit_zero->HonoraryMembershipEligible(Era::John)&&!explicit_zero->HonoraryMembershipEligible(Era::Arthur),"explicit new zero overrides legacy flags regardless of ordering");
    Check(!State::Decode("TheJockeyClubProgress=1\n[John]\nhonorary=2\n[Arthur]\n"),"invalid honorary field rejected");
    Check(!State::Decode("TheJockeyClubProgress=1\n[John]\nhonorary=0\nhonorary=1\n[Arthur]\n"),"duplicate honorary fields rejected");
}
void MembershipEffectiveBetting(){
    using namespace progression;
    FakeArchive archive;archive.Install();
    const auto old_state=runtime::state;
    const auto old_initialized=runtime::initialized;
    const auto old_era=runtime::current_era;
    auto old_provider=std::move(runtime::membership_provider);
    runtime::initialized=true;runtime::state={};
    bool arthur_member=false,john_member=false;
    unsigned queries=0;Era queried=Era::None;
    runtime::SetMembershipProvider([&](Era era){++queries;queried=era;
        return era==Era::Arthur?arthur_member:era==Era::John&&john_member;});
    const auto unchanged=runtime::state.Encode();
    runtime::current_era=Era::Arthur;
    runtime::Perform(Action::LockPrevious,false);
    Check(runtime::state.Encode()==unchanged&&!runtime::unsaved,"floor rollback cannot write or fabricate a progress mutation");
    runtime::current_era=Era::Arthur;
    Check(!runtime::Betting(),"Arthur requires own membership when legacy gate locked");
    arthur_member=true;
    Check(runtime::Betting()&&queried==Era::Arthur,"Arthur membership opens effective betting");
    runtime::current_era=Era::John;
    Check(!runtime::Betting()&&queried==Era::John,"Arthur membership cannot carry into John era");
    arthur_member=false;john_member=true;
    Check(runtime::Betting(),"John membership independently opens betting");
    runtime::current_era=Era::Arthur;
    Check(!runtime::Betting(),"John membership cannot carry back to Arthur era");
    Check(runtime::state.Encode()==unchanged&&!runtime::unsaved,"membership queries do not persist transient state into progress.ini");
    runtime::current_era=Era::John;john_member=false;
    archive.value.john.unlocked.set(Index(Race::ArthurDuel));
    Check(archive.value.Win(Era::John,Race::ArthurDuel),"Arthur duel victory recorded");
    const auto before_legacy=queries;
    Check(!runtime::Betting()&&queries==before_legacy+1,"duel alone bypassed agent enrollment");
    archive.value.john.honorary=true;
    Check(runtime::HonoraryMembershipEligible(Era::John)&&!runtime::Betting(),"honorary offer granted betting before enrollment");
    john_member=true;
    Check(archive.value.LockPrevious(Era::John)&&!runtime::HonoraryMembershipEligible(Era::John)&&runtime::Betting(),"qualification rollback revoked already enrolled membership");
    john_member=false;archive.value.john.honorary=true;
    runtime::current_era=Era::Arthur;
    Check(!runtime::Betting()&&!runtime::HonoraryMembershipEligible(Era::John)&&!runtime::HonoraryMembershipEligible(Era::Arthur),"John offer crossed protagonists");
    arthur_member=true;Check(runtime::Betting(),"save-bound provider can reactivate Arthur membership");
    arthur_member=false;Check(!runtime::Betting(),"provider removal after save rollback immediately relocks membership route");
    const auto before_none=queries;runtime::current_era=Era::None;
    Check(!runtime::Betting()&&queries==before_none,"unknown protagonist never queries or inherits membership");
    runtime::SetMembershipProvider({});runtime::current_era=Era::Arthur;
    Check(!runtime::Betting(),"missing provider fails closed for Arthur");
    runtime::current_era=Era::John;Check(!runtime::Betting(),"missing provider bypassed required enrollment");
    runtime::initialized=false;Check(runtime::Betting(),"isolated uninitialized diagnostic behavior unchanged");
    runtime::state=old_state;runtime::initialized=old_initialized;runtime::current_era=old_era;
    runtime::SetMembershipProvider(std::move(old_provider));
    runtime::SetArchiveProvider({},{});
}
void ArchiveOwnedRefreshAndRejection(){
    using namespace progression;
    FakeArchive archive;archive.Install();runtime::Initialize("ignored",{});
    model=world::kJohnModel;runtime::Tick(0);
    runtime::Win("emerald-valentine",false,false);
    const auto save_a=archive.value;
    // Unchanged generation AND bytes reuse the parsed archive, never a caller's
    // write to the compatibility view. Changed bytes in one world must refresh.
    runtime::state.UnlockAll(Era::John);
    Check(runtime::Refresh()&&runtime::state==save_a,"unchanged archive cache trusted externally modified view");
    archive.value.UnlockNext(Era::John);
    Check(runtime::Refresh()&&runtime::state==archive.value&&runtime::state!=save_a,
        "same-generation changed archive bytes reused stale parsed progress");
    archive.value=save_a;
    Check(runtime::Refresh()&&runtime::state==save_a,"same-world archive rollback failed to replace parsed cache");
    Check(save_a.Wins(Era::John)==1&&runtime::Open("rhodes-vanhorn"),"accepted victory did not update central working archive");
    archive.value={};++archive.generation;
    Check(!runtime::Open("rhodes-vanhorn")&&runtime::WinSummary()=="Won 0/16","read query leaked old archive without waiting for Tick");
    archive.value=save_a;++archive.generation;
    Check(runtime::Open("rhodes-vanhorn")&&runtime::WinSummary()=="Won 1/16","loading older archive did not restore its exact progression view");
    Check(runtime::Perform(Action::UnlockNext,false)==ActionResult::Applied&&runtime::state==archive.value,"F11 did not synchronously accept into archive");
    const auto before=archive.value;archive.accept=false;
    std::vector<std::optional<Race>> notices;runtime::SetNoticeHandler([&](std::optional<Race> race){notices.push_back(race);});
    Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Blocked&&runtime::state==before,"rejected F11 write polluted view or claimed success");
    runtime::Win("rhodes-vanhorn",false,false);
    Check(archive.value==before&&runtime::state==before&&notices.empty(),"rejected victory changed cache or emitted a toast");
    archive.accept=true;archive.value.UnlockAll(Era::John);runtime::Refresh();
    archive.accept=false;runtime::ClearHonoraryQualification();
    Check(runtime::HonoraryMembershipEligible(Era::John),"rejected qualification clear polluted archive view");
    archive.accept=true;runtime::ClearHonoraryQualification();
    Check(!runtime::HonoraryMembershipEligible(Era::John)&&runtime::WinSummary()=="Won 16/16","accepted qualification clear destroyed victories or failed to sync");
    archive.value=save_a;++archive.generation;
    archive.before_write=[&](){archive.value={};++archive.generation;};
    Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Blocked&&archive.value==State{}&&runtime::state==State{},"write raced a world switch and backwrote cached progress into new archive");
    archive.before_write=[&](){archive.value.UnlockNext(Era::John);};
    Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Blocked&&archive.value.Wins(Era::John)==0&&runtime::state==archive.value,
        "same-world changed prior bytes accepted stale replacement");
    archive.before_write={};archive.ready=false;const auto writes=archive.writes;
    Check(runtime::Summary()=="--"&&runtime::WinSummary()=="--"&&!runtime::Open("emerald-valentine")&&!runtime::Camp()&&
        !runtime::HonoraryMembershipEligible(Era::John)&&!runtime::Betting(),"unbound archive exposed old progress or access");
    Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Blocked,"unbound F11 action claimed success");
    runtime::Win("emerald-valentine",false,false);runtime::ClearHonoraryQualification();runtime::Tick(1000);
    Check(archive.writes==writes,"unbound/retry path wrote cached progress");
    runtime::SetArchiveProvider([]()->std::optional<runtime::ArchiveView>{return runtime::ArchiveView{1,"corrupt archive progress"};},
        [](std::uint64_t,const std::string&,const std::string&)->bool{throw std::runtime_error("corrupt view attempted a write");});
    Check(runtime::Summary()=="--"&&runtime::Perform(Action::UnlockAll,false)==ActionResult::Blocked,"invalid central archive progress was treated as a usable view");
    runtime::SetNoticeHandler({});runtime::SetArchiveProvider({},{});runtime::initialized=false;
}
void ArchiveWarningsReachF11WithoutResettingProgress(){
    using namespace progression;const auto language=i18n::language;
    for(const int selected:{0,12}){
        i18n::language=selected;FakeArchive archive;archive.value.Win(Era::John,Race::MaryDuel);
        archive.Install();runtime::Initialize("ignored",{});const auto original=archive.value;
        archive.ready=false;
        archive.issue=i18n::Text("This save does not match its racing records. Your records have been kept; load a matching save or repair the archive.",
            "此存档与赛马记录不匹配，原有记录已保留。请载入匹配的存档或修复档案。");
        Check(!runtime::Refresh()&&runtime::status==archive.issue,"F11 hid permanent archive mismatch behind loading text");
        Check(runtime::Perform(Action::Reset,false)==ActionResult::Blocked&&runtime::status==archive.issue&&
            archive.value==original&&archive.writes==0,"unavailable F11 archive warning reset progress or claimed reset");
        archive.ready=true;
        archive.issue=i18n::Text("Racing records could not be saved. Your records have been kept. Check storage and save again.",
            "赛马记录保存失败，原有记录已保留。请检查存储空间后重新保存。");
        Check(runtime::Refresh()&&runtime::status==archive.issue&&runtime::state==archive.value,
            "save publication failure hid working progress or its F11 warning");
        Check(runtime::Perform(Action::UnlockNext,false)==ActionResult::Applied&&runtime::status==archive.issue,
            "successful live F11 edit concealed outstanding save failure");
        archive.issue.clear();Check(runtime::Refresh()&&runtime::status.empty(),"successful archive recovery retained obsolete F11 warning");
        archive.ready=false;
        Check(!runtime::Refresh()&&runtime::status==i18n::Text("Current save archive is not ready.","当前存档档案尚未就绪。"),
            "ordinary loading invented a persistent archive error");
    }
    runtime::SetArchiveProvider({},{});runtime::initialized=false;i18n::language=language;
}
}
int main(){
    try {
        using namespace progression;
        RollbackProgress();
        HonoraryQualificationAndMigration();
        MembershipEffectiveBetting();
        State state;
        for(auto era:{Era::John,Era::Arthur}){
            Check(state.Open(era,Race::MaryDuel),"Mary-Beth not available initially");
            for(std::size_t i=1;i<kKeys.size();++i)
                Check(!state.Open(era,static_cast<Race>(i)),"fresh campaign leaked a later challenge");
            Check(!state.Betting(era) && !state.Camp(era),"fresh campaign leaked endgame content");
        }
        Check(!state.Win(Era::John,Race::PearsonDuel),"locked race advanced progression");
        Check(!state.Win(Era::John,Race::MaryDuel,true) && !state.Win(Era::John,Race::MaryDuel,false,true),
            "practice/debug substituted host advanced progression");
        for(std::size_t i=0;i<kJohnOrder.size();++i){
            const auto race=kJohnOrder[i];
            Check(state.Open(Era::John,race),"John chain blocked before next challenge");
            Check(state.Win(Era::John,race),"John first official victory missing");
            Check(!state.Win(Era::John,race),"replay victory unlocked multiple challenges");
            Check(state.Wins(Era::John)==i+1,"victory count incorrect");
            if(i+1<kJohnOrder.size())Check(state.Open(Era::John,kJohnOrder[i+1]),"John next challenge was not opened");
            Check(state.HonoraryMembershipEligible(Era::John)==(race==Race::ArthurDuel)&&!state.Betting(Era::John),"all John races grant honorary eligibility without betting enrollment");
            Check(!state.Open(Era::John,Race::JohnBreed),"John meets a duplicate protagonist");
        }
        Check(state.Camp(Era::John) && state.HonoraryMembershipEligible(Era::John)&&!state.Betting(Era::John),"John finale qualifies free enrollment, never direct betting");
        // A finale mount variation is not a separate race completion key.
        for(int horse=0;horse<6;++horse){
            State one;
            for(auto race:kJohnOrder)Check(one.Win(Era::John,race),"one official win per distinct John race");
            Check(one.HonoraryMembershipEligible(Era::John)&&one.Wins(Era::John)==16,"finale does not require all six mounts");
        }
        Check(state.Wins(Era::Arthur)==0,"John's campaign fabricated Arthur-era wins");
        for(auto race:kArthurOrder){
            Check(state.Open(Era::Arthur,race) && state.Win(Era::Arthur,race),"accessible Arthur chain was blocked by a western course");
            Check(!state.Camp(Era::Arthur) && !state.Betting(Era::Arthur),"Arthur-era victory unlocked forbidden endgame content");
        }
        for(auto optional:{Race::JavierDuel,Race::Kentucky,Race::Tennessee,Race::Arabian,Race::Mustang})
            Check(state.Open(Era::Arthur,optional) && state.Win(Era::Arthur,optional),"Arthur western optional race failed");
        Check(state.HonoraryMembershipEligible(Era::Arthur)&&!state.Betting(Era::Arthur)&&!state.Camp(Era::Arthur),"all fifteen Arthur races qualify honorary status without betting or duplicate-protagonist camp");
        state.Reset(Era::John);
        for(std::size_t i=1;i<kJohnOrder.size();++i){
            Check(state.UnlockNext(Era::John) && state.Open(Era::John,kJohnOrder[i]),"F11 unlock-next order incorrect");
            Check(state.Wins(Era::John)==0,"F11 unlock-next forged a win");
        }
        Check(state.UnlockNext(Era::John) && state.HonoraryMembershipEligible(Era::John) && !state.UnlockNext(Era::John),
            "F11 terminal unlock-next failed");
        state.Reset(Era::Arthur);state.UnlockAll(Era::Arthur);
        Check(state.HonoraryMembershipEligible(Era::Arthur)&&!state.Camp(Era::Arthur) && !state.Betting(Era::Arthur) &&
            !state.Open(Era::Arthur,Race::ArthurBreed) && !state.Open(Era::Arthur,Race::ArthurDuel),
            "F11 unlock-all resurrected the current protagonist/endgame");
        Check(State::Decode(state.Encode())==state,"progress roundtrip lost victories or unlocks");
        Check(!State::Decode("TheJockeyClubProgress=99\n[John]\n[Arthur]\n") &&
            !State::Decode("TheJockeyClubProgress=1\n[John]\n"),"invalid/partial file accepted");
        const auto forged=State::Decode("TheJockeyClubProgress=1\n[John]\n[Arthur]\narthur-camp=2\narthur-camp-duel=2\nbetting=1\n");
        Check(forged && !forged->Camp(Era::Arthur) && !forged->Betting(Era::Arthur),"persisted forbidden flags defeated hard era restrictions");
        setTestNativeHandler(Native);
        ArchiveOwnedRefreshAndRejection();
        ArchiveWarningsReachF11WithoutResettingProgress();
        const auto dir=std::filesystem::temp_directory_path()/("tjc-progress-tests-"+std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        State old_global;old_global.UnlockAll(Era::John);const auto legacy_bytes=old_global.Encode();
        {std::ofstream old(dir/"TheJockeyClub-progress.ini",std::ios::binary);old<<legacy_bytes;}
        FakeArchive archive;archive.Install();
        runtime::Initialize(dir.string(),{});
        Check(runtime::Open("emerald-valentine") && !runtime::Betting() && !runtime::Camp(),"runtime defaults not gated");
        runtime::Perform(Action::UnlockAll,true);
        Check(!runtime::Betting(),"busy event accepted a progression mutation");
        runtime::Win("emerald-valentine",true,false);
        Check(!runtime::Open("rhodes-vanhorn"),"runtime practice advanced unlocks");
        runtime::Win("emerald-valentine",false,false);
        Check(runtime::Open("rhodes-vanhorn") && archive.value.Wins(Era::John)==1,"official win did not persist/open next station");
        runtime::Initialize(dir.string(),{});
        Check(runtime::Open("rhodes-vanhorn") && runtime::state.Wins(Era::John)==1,"restart lost progress");
        archive.value.UnlockAll(Era::John);
        archive.value.For(Era::John).won.reset(); // legacy all-open, partly-won state
        for(std::size_t i=0;i<9;++i)archive.value.For(Era::John).won.set(Index(kJohnOrder[i]));
        runtime::Refresh();
        Check(runtime::Perform(Action::UnlockNext,false)==ActionResult::Unchanged&&!runtime::unsaved,
            "retrying an old disk save falsely reports a new progress step");
        Check(runtime::Summary()=="Open 16/16"&&runtime::WinSummary()=="Won 9/16","all-open nine-victory progress is ambiguous");
        i18n::language=12;
        Check(runtime::Summary()=="开放 16/16"&&runtime::WinSummary()=="获胜 9/16","Chinese progress counts lack labels");
        i18n::language=0;
        Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Applied&&runtime::Summary()=="Open 16/16"&&runtime::WinSummary()=="Won 16/16",
            "F11 unlock-all did not complete legacy all-open progress");
        runtime::Initialize(dir.string(),{});
        Check(runtime::WinSummary()=="Won 16/16"&&runtime::HonoraryMembershipEligible(Era::John),"completed campaign did not survive reload");
        Check(runtime::Perform(Action::UnlockAll,false)==ActionResult::Unchanged,"repeated complete-all claimed another change");
        Check(runtime::Perform(Action::Reset,false)==ActionResult::Applied&&runtime::Summary()=="Open 1/16"&&runtime::WinSummary()=="Won 0/16",
            "reset did not change open and won counters immediately");
        runtime::Initialize(dir.string(),{});
        Check(runtime::Summary()=="Open 1/16"&&runtime::WinSummary()=="Won 0/16","reset did not survive reload");
        Check(runtime::Perform(Action::Reset,false)==ActionResult::Unchanged,"unchanged reset reported another mutation");
        const auto reset_state=runtime::state;
        Check(runtime::Perform(Action::UnlockAll,true)==ActionResult::Blocked&&runtime::state==reset_state,"blocked action returned success or mutated progress");
        runtime::Perform(Action::UnlockAll,false);
        Check(!runtime::Betting() && runtime::Camp()&&runtime::HonoraryMembershipEligible(Era::John),"John F11 all must offer free enrollment without opening betting");
        model=world::kArthurModel;runtime::Tick(100);
        runtime::Perform(Action::UnlockAll,false);
        Check(!runtime::Betting() && !runtime::Camp() && !runtime::Open("arthur-camp-duel"),"actual Arthur model bypassed era restrictions");
        runtime::Perform(Action::Reset,false);
        Check(runtime::state.Wins(Era::John)==0 && runtime::state.Wins(Era::Arthur)==0,"reset retained old campaign victories");
        std::vector<std::optional<Race>> notices;
        runtime::SetNoticeHandler([&](std::optional<Race> race){notices.push_back(race);});
        for(auto race:kArthurOrder){if(race==Race::Appaloosa)break;runtime::Win(kKeys[Index(race)],false,false);}
        notices.clear();
        runtime::Win("appaloosa-stakes",true,false);runtime::Win("appaloosa-stakes",false,true);
        Check(notices.empty(),"practice/substituted victory created notices");
        runtime::Win("appaloosa-stakes",false,false);
        Check(notices==std::vector<std::optional<Race>>{Race::Kentucky,Race::Tennessee,Race::JohnBreed},"one Arthur win must queue three distinct newly opened races in stable order");
        runtime::Win("appaloosa-stakes",false,false);Check(notices.size()==3,"repeated win repeated availability notices");
        notices.clear();
        for(auto race:{Race::JohnBreed,Race::Standardbred,Race::Paint,Race::JavierDuel,Race::Kentucky,Race::Tennessee,Race::Arabian,Race::Mustang})
            runtime::Win(kKeys[Index(race)],false,false);
        Check(!notices.empty()&&!notices.back()&&std::count(notices.begin(),notices.end(),std::nullopt)==1&&
            runtime::HonoraryMembershipEligible(Era::Arthur)&&!runtime::Betting(),"completion notice missing or honorary offer directly opened betting");
        Check(runtime::Summary()=="Open 15/15"&&runtime::WinSummary()=="Won 15/15","Arthur summary hid the optional challenges required for honorary status");
        const auto notice_count=notices.size();runtime::Win("new-austin",false,false);
        Check(notices.size()==notice_count,"repeat completion replayed honorary notice");
        for(bool chinese:{false,true}){
            for(const auto race:notices){const auto message=Notice(race,chinese);
                Check(!message.title.empty()&&!message.body.empty()&&message.body.find("解锁")==std::string::npos&&message.body.find("unlocked")==std::string::npos,"notification missing localized text or used unlock wording");}
            Check(Notice(std::nullopt,chinese).duration_ms==8000,"honorary notice too short");
        }
        runtime::ClearHonoraryQualification();
        runtime::Initialize(dir.string(),{});
        Check(!runtime::HonoraryMembershipEligible(Era::Arthur)&&runtime::WinSummary()=="Won 15/15","cleared eligibility returned automatically or destroyed race wins");
        runtime::SetNoticeHandler({});
        {std::ifstream old(dir/"TheJockeyClub-progress.ini",std::ios::binary);const std::string retained{std::istreambuf_iterator<char>(old),{}};Check(retained==legacy_bytes,"archive progression imported or modified old global ini");}
        Check(!std::filesystem::exists(dir/"TheJockeyClub-progress.ini.bak"),"archive progression created a legacy ini backup");
        runtime::SetArchiveProvider({},{});
        for(const char* filename:{"TheJockeyClub-progress.ini","TheJockeyClub-progress.ini.bak","TheJockeyClub-progress.ini.tmp","TheJockeyClub-progress.ini.bak.tmp"})
            std::filesystem::remove(dir/filename);
        std::filesystem::remove(dir);runtime::initialized=false;
        std::cout<<"John/Arthur progression, any-one-horse finale, administrative gates and archive-owned views passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
