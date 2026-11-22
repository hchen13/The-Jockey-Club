#include "../src/race_bookmaker_start.h"
#include "../src/race_starter.h"
#include "../src/race_cash_receipt.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

__declspec(dllimport) void setTestNativeHandler(void (*)(UINT64,const UINT64*,UINT64*));
using namespace horse_races;
namespace {
void Check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
Hash H(const char* s){Hash h=0;while(*s){auto c=*s++;if(c>='A'&&c<='Z')c+=32;h+=c;h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;return h+(h<<15);}
float F(UINT64 value){return std::bit_cast<float>(static_cast<std::uint32_t>(value));}
struct Fake {
    int ticks=0,gos=0,rears=0,aims=0,shots=0,deleted=0,game_ms=0,random=0;
    int feeds=0,amount=0,reveals=0;bool accept_feed=true,black=true;
    Hash starter_model=0;std::string label;std::map<int,float> agitation;
} f;
// A deliberately unreadable token, like the real VAR_STRING return value.
constexpr UINT64 kPlayerString=0x7ffffffffff1ULL;
void Native(UINT64 hash,const UINT64* a,UINT64* out){
    switch(hash){
    case 0xFD340785ADF8CFB7:out[0]=H(reinterpret_cast<const char*>(a[0]));break;
    case 0xD42BD6EB2E0F1677:out[0]=a[0]>0;break;
    case 0xDA76A9F39210D365:out[0]=f.starter_model;break;
    case 0x4F67E8ECA7D3F667:out[0]=f.game_ms;break;
    case 0xD53343AA4FB7DD28:out[0]=a[0]+f.random++%(a[1]-a[0]);break;
    case 0xD9130842D7226045:out[0]=1;break;
    case 0x67C540AA08E4A6F5:
        if(std::string(reinterpret_cast<const char*>(a[0]))==cues::Settings().tick.name)++f.ticks;
        else if(std::string(reinterpret_cast<const char*>(a[0]))==cues::Settings().go.name)++f.gos;
        break;
    case 0x42688E94E96FD9B4:out[0]=std::bit_cast<std::uint32_t>(.4F);break;
    case 0xA09CFD29100F06C3:
        Check((a[1]==1 || a[1]==5) && a[3]<=1,"rear used an unreviewed/dismounting action");
        f.agitation[static_cast<int>(a[0])]=.9F;++f.rears;break;
    case 0x06D26A96CA1BCA75:f.agitation[static_cast<int>(a[0])]=F(a[2]);break;
    case 0xA86D5F069399F44D:{Vector3 p{0,0,0};std::memcpy(out,&p,sizeof(p));break;}
    case 0x2A29CA9A6319E6AB:
        *reinterpret_cast<float*>(a[3])=0;*reinterpret_cast<Vector3*>(a[4])={0,0,1};out[0]=1;break;
    case 0x392C8D8E07B70EFC:case 0x1283B8B89DD5D1B6:out[0]=1;break;
    case 0xD49F9B0955C367DE:f.starter_model=static_cast<Hash>(a[0]);out[0]=77;break;
    case 0x6671F3EEC681BDA1:Check(F(a[3])==starter::kAimMetres,"starter aimed at racers");++f.aims;break;
    case 0x46A6CC01E0826106:
        Check(F(a[3])==starter::kAimMetres && a[5]==H("FIRING_PATTERN_SINGLE_SHOT"),"starter shot is not safely upward/single");
        ++f.shots;break;
    case 0xF5472C80DF2FF847:out[0]=f.black;break;
    case 0xFFC96ECB7FA404CA:case 0x613C15D5D8DB781F:out[0]=1;break;
    case 0xCC0EF140F99365C5:++f.deleted;break;
    case 0xFA925AC00EB830B9:
        if(a[0]==2){f.label=reinterpret_cast<const char*>(a[1]);f.amount=static_cast<int>(a[2]);}
        else Check(a[0]==10 && std::string(reinterpret_cast<const char*>(a[1]))=="LITERAL_STRING",
            "starter label/cash receipt used unexpected native formatter");
        out[0]=kPlayerString;break;
    case 0xB249EBCB30DD88E0:{
        const auto* data=reinterpret_cast<const UINT64*>(a[1]);
        Check(data[1]==kPlayerString && data[5]==H("COLOR_PURE_WHITE") && f.label=="FEED_MONEY_EARN",
            "native return receipt is not opaque-token/white earnings style");
        ++f.feeds;out[0]=f.accept_feed?91:0;break;
    }
    case 0xC9CAEAEEC1256E54:
        Check(a[0]==H("HUD_CTX_INPUT_REVEAL_HUD"),"receipt used an unverified HUD context");++f.reveals;break;
    case 0xBC3422DC91667621:case 0x466BC8769CF26A7A:
        throw std::runtime_error("presenting a receipt changed the wallet a second time");
    // A custom +$XX drawing path is forbidden; the receipt must call UIFEED.
    case 0xD79334A4BB99BAD1:case 0x405224591DF02025:
        throw std::runtime_error("cash receipt drew its own text/rectangle");
    default:break;
    }
}
}
int main(){
    setTestNativeHandler(Native);
    try{
        for(const int count:{6,7,10}){
            f={};cues::Forget();cues::ForgetRearedHorses();cues::Settings()={};
            std::vector<Ped> mounts;for(int i=0;i<count;++i)mounts.push_back(20+i);
            bookmaking::ExhibitionCountdown countdown;countdown.Begin(mounts);
            Check(!countdown.Go(),"start cue played before a complete countdown");
            Check(countdown.Tick(100,false,false).elapsed==0 && f.ticks==0 && f.rears==0,
                "blackout consumed countdown/audio");
            Check(countdown.Tick(.1,true,false).digit==3 && f.ticks==1,"first visible digit lacks its tick");
            Check(countdown.Tick(100,true,true).elapsed==0 && f.ticks==1,"paused countdown consumed time or sound");
            Check(countdown.Tick(std::numeric_limits<double>::quiet_NaN(),true,false).elapsed==0 && f.ticks==1,
                "nonfinite delta poisoned countdown");
            bool released=false;
            for(int frame=0;frame<180;++frame){
                f.game_ms=frame*20;
                const auto step=countdown.Tick(.02,true,false);
                if(step.release){Check(step.elapsed>=3 && f.ticks==3,"GO happened before three visible ticks");released=true;break;}
            }
            Check(released && f.rears==count && countdown.Go() && f.gos==1 && !countdown.Go() &&
                !countdown.Tick(.02,true,false).release,"GO/rears repeated or omitted an entrant");
            f.game_ms+=3300;Check(cues::CalmRearedHorses()==count,"post-rear agitation not restored for the full field");
            for(const auto horse:mounts)Check(f.agitation[horse]==.4F,"rear changed a horse's lasting motivation");
        }
        f={};cues::Settings().rear_enabled=false;
        bookmaking::ExhibitionCountdown quiet;const std::array<Ped,1> one{20};quiet.Begin(one);
        quiet.Tick(.02,true,false);for(int i=0;i<40;++i)quiet.Tick(.1,true,false);
        Check(f.rears==0 && f.ticks==3 && quiet.Go(),"rear-disabled configuration lost countdown sounds");
        cues::Settings()={};
        starter::Starter gun;gun.Request(3);
        Check(gun.Create({0,0,0},0) && gun.Alive(),"stock starter could not be created");
        gun.Aim();gun.Aim();gun.Fire();gun.Fire();
        Check(f.aims==1 && f.shots==1,"starter aimed or fired more than once");
        f.black=false;Check(!gun.Retire() && f.deleted==0,"visible starter vanished");
        f.black=true;Check(gun.Retire() && f.deleted==1,"owned starter not removed under blackout");gun.Release();
        f={};transaction_feed::CashReceipts receipts;
        Check(!receipts.Queue(0) && !receipts.Queue(-10) && receipts.Queue(210),"invalid receipt accepted");
        Check(!receipts.Tick(1,false) && !receipts.Tick(2,false) && f.feeds==0 && receipts.Pending()==1,
            "receipt was consumed under results/menu/fade");
        auto posted=receipts.Tick(3,true);
        Check(posted && posted->cents==210 && posted->handle==91 && f.feeds==1 && f.amount==210 &&
            f.reveals==1 && receipts.Pending()==0,"visible stock receipt/cash HUD not presented");
        receipts.Tick(3.5,true);receipts.Tick(5,true);
        Check(f.feeds==1 && f.reveals==3,"receipt duplicated or native cash HUD failed to remain visible");
        receipts.Tick(7,true);Check(f.reveals==3,"native cash reveal leaked beyond its short window");
        Check(receipts.Queue(3125000),"large return receipt not queued");f.accept_feed=false;
        Check(!receipts.Tick(8,true) && !receipts.Tick(8.5,true) && receipts.Pending()==1 && f.feeds==2,
            "failed feed consumed a receipt or retried every frame");
        f.accept_feed=true;posted=receipts.Tick(9,true);
        Check(posted && posted->cents==3125000 && receipts.Pending()==0 && f.feeds==3,"large receipt did not recover once");
        receipts.Tick(10,true);Check(f.feeds==3,"successful retry duplicated cash presentation");
        std::cout<<"Native exhibition ticks/GO/rears/starter and once-only white gameplay cash receipts passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
