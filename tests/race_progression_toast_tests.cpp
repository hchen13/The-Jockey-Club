#include "../src/race_progression_toast.h"
#include "../src/race_progression_messages.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
__declspec(dllimport) void setTestNativeHandler(void (*)(UINT64,const UINT64*,UINT64*));
using namespace horse_races::progression_toast;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Hash HashOf(std::string_view text){Hash hash=0;for(unsigned char c:text){if(c>='A'&&c<='Z')c+=32;hash+=c;hash+=hash<<10;hash^=hash>>6;}hash+=hash<<3;hash^=hash>>11;hash+=hash<<15;return hash;}
struct Posted {std::string title,body;int duration=0,handle=0;Hash dictionary=0,sprite=0;};
std::map<UINT64,std::string> tokens;
std::map<int,int> states;
std::vector<Posted> posts;
UINT64 next_token=0x1000;
int next_handle=100,attempts=0;
bool paused=false,fail_post=false,fail_token=false;
bool icon_loaded=true,icon_glyph=true;
std::map<std::string,int> icon_requests,icon_releases;
void Native(UINT64 hash,const UINT64* a,UINT64* out){
    switch(hash){
    case 0xFD340785ADF8CFB7:out[0]=HashOf(reinterpret_cast<const char*>(a[0]));break;
    case 0xFA925AC00EB830B9:{
        Check(a[0]==10&&std::string(reinterpret_cast<const char*>(a[1]))=="LITERAL_STRING","toast did not use literal localization-safe tokens");
        if(fail_token){out[0]=0;break;}
        out[0]=++next_token;tokens[out[0]]=reinterpret_cast<const char*>(a[2]);break;
    }
    case 0x535384D6067BA42E:out[0]=paused;break;
    case 0xC1BA29DF5631B0F8:++icon_requests[reinterpret_cast<const char*>(a[0])];break;
    case 0x54D6900929CCF162:out[0]=icon_loaded;break;
    case 0xA2A51869BDED733B:out[0]=icon_glyph;break;
    case 0x4ACA10A91F66F1E2:++icon_releases[reinterpret_cast<const char*>(a[0])];break;
    case 0x59FA676177DBE4C9:Check(states.contains(static_cast<int>(a[0])),"queried foreign feed handle");out[0]=states[static_cast<int>(a[0])];break;
    case 0x26E87218390E6729:{
        ++attempts;
        const auto* params=reinterpret_cast<const UINT64*>(a[0]);const auto* data=reinterpret_cast<const UINT64*>(a[1]);
        Check(a[2]==1&&a[3]==1,"native feed did not use proven queue flags");
        Check(params[0]>=4000&&params[3]==1,"native mission toast duration/flags wrong");
        Check(std::string(reinterpret_cast<const char*>(params[1]))=="Mission_Complete_Sounds"&&
            std::string(reinterpret_cast<const char*>(params[2]))=="Mission_Complete_Enter","native mission-completion cue fields missing");
        Check(data[4]&&data[5]&&data[7]==1,"native toast icon or flags missing");
        if(fail_post){out[0]=0;break;}
        const int handle=++next_handle;states[handle]=0;out[0]=handle;
        posts.push_back({tokens.at(data[1]),tokens.at(data[2]),static_cast<int>(params[0]),handle,
            static_cast<Hash>(data[4]),static_cast<Hash>(data[5])});break;
    }
    default:throw std::runtime_error("toast touched foreign HUD/input/camera/audio/channel native");
    }
}
void TestQueue(){
    Presenter queue;Check(!queue.Show("","body")&&!queue.Show("title",""),"blank toast accepted");
    Check(queue.Show("Race invitation","Bill invites you to race.")&&queue.Show("赛事邀请","比尔邀请你参加对决赛。",8000),"queue rejected EN/ZH");
    Check(posts.empty(),"Show posted or played sound while queued");queue.TickAt(1,false);Check(posts.empty(),"busy race HUD did not defer toast");
    paused=true;queue.TickAt(2);Check(posts.empty(),"pause menu did not defer toast");paused=false;
    queue.TickAt(3);Check(posts.size()==1&&posts[0].duration==6000&&posts[0].title=="Race invitation"&&queue.Pending()==1,"first toast incorrect");
    Check(posts[0].dictionary==HashOf("HUD_TOASTS")&&posts[0].sprite==HashOf("TOAST_MEDAL_COLLECTABLE"),"default native mission medal changed");
    const int first=queue.OwnedHandle();
    for(int state:{0,1,2,3,4,5}){states[first]=state;queue.TickAt(100000+state);Check(posts.size()==1,"next toast overtook native lifecycle or repeated cue");}
    states[first]=6;queue.TickAt(200000);Check(posts.size()==2&&posts[1].duration==8000&&posts[1].title=="赛事邀请"&&posts[1].body=="比尔邀请你参加对决赛。","second native toast changed order or Chinese literal");
    queue.Show("Queued stale event","Must disappear on load.");queue.Clear();Check(queue.Pending()==0,"Clear retained old-world queued toast");
    const int owned=queue.OwnedHandle();queue.Show("New world","Wait for the old native toast to finish.");queue.TickAt(300000);Check(posts.size()==2&&queue.OwnedHandle()==owned,"Clear overwrote active native toast");
    states[owned]=6;queue.TickAt(300001);Check(posts.size()==3&&posts.back().title=="New world","post-reset queue did not resume after native completion");
    states[queue.OwnedHandle()]=6;queue.TickAt(300002);Check(queue.OwnedHandle()==0,"completed handle retained");
    queue.Show("Old timeline","Active before loading.");queue.TickAt(300003);
    const int oldWorldHandle=queue.OwnedHandle();const auto priorPosts=posts.size();
    queue.Show("Old queued event","Must not survive a load.");queue.ResetWorld();
    Check(queue.OwnedHandle()==0&&queue.Pending()==0,"world reset retained old ownership or pending events");
    states.erase(oldWorldHandle); // The new native session cannot resolve it.
    queue.Show("Fresh timeline","A new race invitation.");queue.TickAt(300004);
    Check(posts.size()==priorPosts+1&&posts.back().title=="Fresh timeline"&&queue.OwnedHandle()!=oldWorldHandle,
        "old-world handle blocked the new world's native toast");
}
void TestStateDiagnostics(){
    Presenter queue;std::vector<std::string> log;
    queue.SetLogger([&](const std::string& value){log.push_back(value);});
    queue.Show("First","Race completed.");queue.Show("Second","Next location.");
    const auto before=posts.size();queue.TickAt(1,false);queue.TickAt(2,false);
    Check(log.size()==1&&log.back().find("hold external")!=std::string::npos,"external hold logged every frame or missing");
    paused=true;queue.TickAt(3);queue.TickAt(4);
    Check(log.size()==2&&log.back().find("hold pause")!=std::string::npos,"pause hold diagnostic missing/repeated");
    paused=false;fail_post=true;queue.TickAt(5);queue.TickAt(6);queue.TickAt(505);
    Check(log.size()==3&&log.back().find("retry")!=std::string::npos,"retry diagnostic missing/repeated");
    fail_post=false;queue.TickAt(1005);const int first=queue.OwnedHandle();
    Check(posts.size()==before+1&&log.size()==4&&log.back().find("posted handle=")!=std::string::npos,"post diagnostic missing");
    queue.TickAt(1006);queue.TickAt(1007);
    Check(log.size()==5&&log.back().find("state=0")!=std::string::npos,"feed state logged every frame or missing");
    states[first]=3;queue.TickAt(1008);queue.TickAt(1009);
    Check(log.size()==6&&log.back().find("state=3")!=std::string::npos,"feed transition missing/repeated");
    states[first]=6;queue.TickAt(1010);
    Check(log.size()==9&&log[6].find("state=6")!=std::string::npos&&log[7].find("dismissed handle=")!=std::string::npos&&
        log[8].find("posted handle=")!=std::string::npos&&posts.size()==before+2&&posts.back().title=="Second",
        "dismissal/next location sequence incorrect");
    queue.SetLogger({});queue.ResetWorld();
}
void TestFailuresAndClamp(){
    Presenter queue;const auto count=posts.size();const auto before=attempts;
    queue.Show("Retry","Native unavailable",1);fail_post=true;queue.TickAt(100);
    Check(posts.size()==count&&queue.Pending()==1&&queue.OwnedHandle()==0&&attempts==before+1,"failed post consumed or became active");
    queue.TickAt(599);Check(attempts==before+1,"failed post retried every frame");fail_post=false;queue.TickAt(600);
    Check(posts.size()==count+1&&posts.back().duration==4000&&queue.Pending()==0,"failed post retry lost duration clamp");
    Presenter tokenFailure;tokenFailure.Show("Title","Body");fail_token=true;const auto tries=attempts;tokenFailure.TickAt(1);
    Check(attempts==tries&&tokenFailure.Pending()==1,"invalid var-string posted or discarded");fail_token=false;
}
void TestQueuedIconsStayWithTheirEntries(){
    Presenter queue;const auto before=posts.size();
    using horse_races::map_textures::Dictionary;
    Icon first=Dictionary::Story,second=Dictionary::Multiplayer;
    queue.Show("First race","First location",6000,first);
    queue.Show("Second race","Second location",6000,second);
    first=Dictionary::Multiplayer;second=Dictionary::Story;
    fail_post=true;queue.TickAt(1);Check(posts.size()==before&&queue.Pending()==2,"icon-bearing entry lost on retry");
    fail_post=false;queue.TickAt(501);
    Check(posts.size()==before+1&&posts.back().dictionary==HashOf("blips")&&posts.back().sprite==0xD99F0C2B,
        "first queued race icon changed after enqueue/retry");
    queue.TickAt(90000);Check(posts.size()==before+1,"second race icon overtook active native toast");
    states[queue.OwnedHandle()]=6;queue.TickAt(90001);
    Check(posts.size()==before+2&&posts.back().dictionary==HashOf("blips_mp")&&posts.back().sprite==564457427,
        "second queued race borrowed the previous race icon");
    queue.ResetWorld();
}
void TestMembershipRegistrationUsesSameNativeQueue(){
    Presenter queue;const auto before=posts.size();
    const auto earned=horse_races::progression::Notice(std::nullopt,true);
    const auto joined=horse_races::progression::MembershipRegistered(true,true);
    const auto paid=horse_races::progression::MembershipRegistered(false,false);
    const auto earnedEnglish=horse_races::progression::Notice(std::nullopt,false);
    Check(earned.body=="出售一匹有胜绩的赛马后，即可前往经纪人处免费入会。"&&
        earnedEnglish.body=="After selling a winning horse, visit the Club Agent for free enrollment.",
        "honorary completion toast changed the approved immersive enrollment wording");
    Check(joined.title=="赛马会荣誉会员"&&joined.duration_ms==8000&&
        paid.title=="CLUB MEMBERSHIP"&&paid.duration_ms==6000,"registration labels or reading durations wrong");
    Check(joined.body.find("解锁")==std::string::npos&&paid.body.find("unlock")==std::string::npos,"registration used unwanted unlock wording");
    queue.Show(earned.title,earned.body,earned.duration_ms);
    queue.Show(joined.title,joined.body,joined.duration_ms);
    queue.Show(paid.title,paid.body,paid.duration_ms);
    Check(posts.size()==before,"membership success posted/played cue at enqueue time");
    queue.TickAt(1);Check(posts.size()==before+1&&posts.back().title==earned.title,"qualification toast lost before registration");
    queue.TickAt(90000);Check(posts.size()==before+1,"registration overwrote active qualification toast");
    states[queue.OwnedHandle()]=6;queue.TickAt(90001);
    Check(posts.size()==before+2&&posts.back().title==joined.title&&posts.back().body==joined.body&&posts.back().duration==8000,
        "honorary registration did not use same native left toast");
    queue.TickAt(90002);Check(posts.size()==before+2,"honorary registration repeated cue");
    states[queue.OwnedHandle()]=6;queue.TickAt(90003);
    Check(posts.size()==before+3&&posts.back().title==paid.title&&posts.back().body==paid.body,"paid registration did not use same native queue");
    Check(posts.back().dictionary==HashOf("HUD_TOASTS")&&posts.back().sprite==HashOf("TOAST_MEDAL_COLLECTABLE"),
        "membership registration icon changed");
}
void TestVictoryBeforeNewRaces(){
    using namespace horse_races::progression;
    for(bool chinese:{false,true}){
        Presenter queue;const auto before=posts.size();
        const auto victory=Victory(Race::Appaloosa,chinese);
        Check(victory.duration_ms==4000&&victory.title==(chinese?"比赛获胜":"RACE WON")&&
            victory.body==(chinese?"阿帕卢萨马限定赛":"Appaloosa Challenge"),"victory copy or duration wrong");
        queue.Show(victory.title,victory.body,victory.duration_ms);
        for(auto race:{Race::Kentucky,Race::Tennessee,Race::JohnBreed}){
            const auto opened=Notice(race,chinese);queue.Show(opened.title,opened.body,opened.duration_ms,opened.icon);
        }
        queue.TickAt(1);Check(posts.size()==before+1&&posts.back().duration==4000&&posts.back().body==victory.body,
            "first native toast was not the four-second victory");
        Check(posts.back().dictionary==HashOf("HUD_TOASTS")&&posts.back().sprite==HashOf("TOAST_MEDAL_COLLECTABLE"),
            "RACE WON no longer uses its existing medal icon");
        queue.TickAt(100000);Check(posts.size()==before+1,"wall-clock timer replaced a still-active native toast");
        for(auto race:{Race::Kentucky,Race::Tennessee,Race::JohnBreed}){
            states[queue.OwnedHandle()]=6;queue.TickAt(100001);
            Check(posts.back().body==Notice(race,chinese).body&&posts.back().duration==6000,"new races lost order or original duration");
            Check(posts.back().dictionary==HashOf("blips_mp")&&posts.back().sprite==564457427,
                "sequential field-race unlock toast did not retain its map horse icon");
        }
        queue.ResetWorld();
    }
    for(std::size_t i=0;i<kKeys.size();++i)for(bool chinese:{false,true})
        Check(!Victory(static_cast<Race>(i),chinese).body.empty(),"a real race has no localized victory name");
}
void TestEveryRaceUsesItsMapIcon(){
    using namespace horse_races::progression;
    // Independent expected categories in progression order, including Arthur's
    // duel after his breed challenge and John's separate Thoroughbred race.
    constexpr std::array<bool,17> field{false,false,false,false,false,false,false,
        true,true,true,true,true,true,true,true,false,true};
    static_assert(field.size()==kKeys.size());
    for(bool chinese:{false,true}){
        Presenter queue;
        for(std::size_t i=0;i<field.size();++i){
            const auto text=Notice(static_cast<Race>(i),chinese);
            Check(text.icon.has_value(),"real race availability notice has no map icon");
            queue.Show(text.title,text.body,text.duration_ms,text.icon);
        }
        const auto before=posts.size();
        for(std::size_t i=0;i<field.size();++i){
            if(queue.OwnedHandle())states[queue.OwnedHandle()]=6;
            queue.TickAt(1000+i);
            Check(posts.size()==before+i+1&&posts.back().body==Notice(static_cast<Race>(i),chinese).body,
                "multiple availability toasts lost their race association/order");
            Check(posts.back().dictionary==HashOf(field[i]?"blips_mp":"blips")&&
                posts.back().sprite==(field[i]?564457427U:0xD99F0C2BU),"race availability icon differs from its map category");
        }
        queue.ResetWorld();
    }
    Check(!Notice(std::nullopt,false).icon&&!Victory(Race::Kentucky,false).icon&&
        !MembershipRegistered(true,false).icon,"non-availability message acquired a race icon");
}
void TestMapIconWaitAndOwnership(){
    using namespace horse_races;
    using map_textures::Dictionary;using map_textures::Owner;
    Presenter queue;const auto before=posts.size();const auto requests=icon_requests["blips_mp"];
    const auto releases=icon_releases["blips_mp"];
    queue.Show("New field race","Its own map icon",6000,Dictionary::Multiplayer);
    icon_loaded=false;queue.TickAt(1);queue.TickAt(2);
    Check(posts.size()==before&&queue.Pending()==1&&icon_requests["blips_mp"]==requests+1,
        "unloaded race atlas posted a fallback toast or requested every frame");
    icon_loaded=true;icon_glyph=false;queue.TickAt(3);
    Check(posts.size()==before&&queue.Pending()==1,"missing race glyph used a medal fallback");
    icon_glyph=true;fail_post=true;queue.TickAt(4);
    fail_post=false;queue.TickAt(504);
    Check(posts.size()==before+1&&posts.back().sprite==564457427&&icon_releases["blips_mp"]==releases,
        "streaming/native retry lost the race glyph or released its active atlas");
    queue.Clear();queue.TickAt(505);
    Check(icon_releases["blips_mp"]==releases,"clearing pending events released the active icon");
    states[queue.OwnedHandle()]=6;queue.TickAt(506);
    Check(icon_releases["blips_mp"]==releases+1,"completed native toast retained its atlas request");
    map_textures::Acquire(Dictionary::Story,Owner::RaceMarkers);
    const auto story_releases=icon_releases["blips"];
    icon_loaded=false;queue.Show("New duel","Waiting icon",6000,Dictionary::Story);queue.TickAt(507);queue.ResetWorld();
    Check(icon_releases["blips"]==story_releases,"toast world reset released the race-map owner's atlas");
    map_textures::Release(Dictionary::Story,Owner::RaceMarkers);icon_loaded=true;
}
}
int main(){setTestNativeHandler(Native);try{TestQueue();TestStateDiagnostics();TestFailuresAndClamp();TestQueuedIconsStayWithTheirEntries();TestMembershipRegistrationUsesSameNativeQueue();TestVictoryBeforeNewRaces();TestEveryRaceUsesItsMapIcon();TestMapIconWaitAndOwnership();std::cout<<"Native victory, progression and membership toast lifecycle, race icons, streaming, queue, localization and cue fields passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
