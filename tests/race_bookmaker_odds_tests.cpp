#include "../src/race_bookmaker_odds.h"
#include "../src/race_bookmaking.h"
#include "../src/race_bookmaker_venues.h"
#include "../src/race_bookmaker_hud.h"
#include "../src/race_host_boost.h"
#include "../src/horse_motion_curve.h"
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
}
int main(){
    using namespace horse_races::bookmaking;
    try{
        Check(kVenues[0].shared_attributes && kVenues[0].entrants==10 && kVenues[0].columns==5 &&
            !kVenues[1].shared_attributes && kVenues[1].entrants==7 && kVenues[1].columns==2 &&
            !kVenues[1].greeting && !kVenues[1].crowd &&
            kVenues[2].shared_attributes && kVenues[2].entrants==6 && kVenues[2].columns==3 &&
            kVenues[2].greeting && kVenues[2].crowd,
            "venue rules mixed Handicap and WFA attributes or lap policies");
        Check(kVenues[1].viewer.z==157.831757F && kVenues[1].clerk.heading==164.545639F,
            "Strawberry recorded reception/viewer poses changed");
        Check(kVenues[2].viewer.x==1230.731812F && kVenues[2].viewer.y==-1279.179443F &&
            kVenues[2].viewer.z==79.192116F && kVenues[2].viewer.heading==326.002747F &&
            kVenues[2].clerk.x==1322.086914F && kVenues[2].clerk.y==-1346.935425F &&
            kVenues[2].clerk.z==77.922546F &&
            kVenues[2].clerk.heading==228.819611F &&
            std::string_view(kVenues[2].clerk_model)=="CS_CLAY",
            "Rhodes lost its latest viewer pose, original reception or Clay model");
        for(const auto [width,height]:{std::pair{1920.0F,1080.0F},std::pair{3440.0F,1440.0F},std::pair{5120.0F,1440.0F}})
            Check(std::abs(RoundIconWidth(.035F,width/height)*width-.035F*height)<.001F,
                "native circular icon is distorted by the display aspect ratio");
        for(int level=1;level<=10;++level){
            const horse_motion::Config base;
            const float force=horse_races::host_boost::BonusForce(base.high_speed_forces,level,.5F);
            if(level<6)Check(force==0,"hard minimum displayed burst level of six was lowered");
            else Check(force>base.high_speed_forces[level-6],
                "eligible burst lost calibrated half-level force interpolation");
            const float bar=force>0?horse_races::host_boost::kGaitBar:0.0F;
            horse_races::host_boost::Gate gate;
            for(int tick=0;tick<=80;++tick){
                const bool active=gate.Update(tick/10.0,3.0F,bar,true,3,5);
                Check(active==(level>=6 && tick>=30 && tick<80),
                    "burst violated the six-level gate or 3/5-second charge/duration");
            }
        }
        odds::Calibration seed;
        std::vector<odds::Prediction> runners;
        for(int i=0;i<7;++i)runners.push_back({odds::Key("horse"+std::to_string(i)),150});
        const auto equal=odds::Probabilities(runners,seed);
        Check(equal.size()==7 && std::abs(std::accumulate(equal.begin(),equal.end(),0.0)-1)<.00001,
            "probabilities are not normalized for the whole field");
        for(double value:equal)Check(std::abs(value-1.0/7)<.00001 &&
            odds::AttributeQuote(8,5,value,value)==odds::BaseQuote(8,5),
            "identical WFA priors favour a name/grid position");
        Check(odds::BaseQuote(10,10)==110 && odds::BaseQuote(6,1)==2000,
            "authored displayed-attribute endpoints are not 1.1 and 20-to-1");
        for(int speed=6;speed<=10;++speed)for(int acceleration=1;acceleration<=10;++acceleration){
            const int base=odds::BaseQuote(speed,acceleration);
            Check(base>=110 && base<=2000 && base%10==0,"base price has more than one decimal");
            if(speed<10)Check(odds::BaseQuote(speed+1,acceleration)<=base,"faster horse has longer base odds");
            if(acceleration<10)Check(odds::BaseQuote(speed,acceleration+1)<=base,"better acceleration has longer base odds");
            for(const double probability:{.000001,.05,.25,.75,.999999}){
                Check(odds::AttributeQuote(speed,acceleration,probability,probability)==base,
                    "unlearned price changed with field composition instead of attributes");
                const int longer=odds::AttributeQuote(speed,acceleration,.25,.000001);
                const int shorter=odds::AttributeQuote(speed,acceleration,.25,.999999);
                Check(longer>=base && longer*5<=base*6 && shorter<=base && shorter*5>=base*4 &&
                    longer%10==0 && shorter%10==0,"local correction/rounding exceeds twenty percent");
            }
        }
        for(int speed=1;speed<6;++speed)for(int acceleration=1;acceleration<=10;++acceleration){
            const int base=odds::BaseQuote(speed,acceleration);
            Check(base>0 && base%10==0 && base>=odds::BaseQuote(speed+1,acceleration),
                "below-six WFA attributes have no quote or shorter odds than a faster horse");
            Check(odds::AttributeQuote(speed,acceleration,.25,.25)==base,
                "below-six WFA quote broke local calibration");
        }
        Check(odds::BaseQuote(0,1)==0 && odds::BaseQuote(11,10)==0 &&
            odds::BaseQuote(6,0)==0 && odds::BaseQuote(10,11)==0 &&
            odds::AttributeQuote(10,10,0,.1)==0 && odds::AttributeQuote(10,10,.1,1)==0 &&
            odds::LocalQuoteFactor(.1,std::numeric_limits<double>::quiet_NaN())==0,
            "invalid displayed levels or probabilities accepted");
        runners[0].seconds=135;runners[1].seconds=175;
        const auto varied=odds::Probabilities(runners,seed);
        Check(varied[0]>varied[2] && varied[2]>varied[1],
            "stronger runtime horse/course estimate does not shorten Win odds");
        for(const auto& r:runners){
            for(int i=0;i<30;++i)Check(seed.Observe(r,true,r.seconds*(r.key==runners[0].key?1.2:1)),
                "valid local completion rejected");
        }
        const auto learned=odds::Probabilities(runners,seed);
        Check(learned[0]<varied[0],"local slower actual times did not gradually update the prior");
        Check(odds::AttributeQuote(8,5,varied[0],learned[0])>odds::BaseQuote(8,5),
            "slower actual local results did not lengthen the attribute base quote");
        odds::Calibration restored;
        Check(restored.Parse(seed.Serialize()) && restored.Serialize()==seed.Serialize() &&
            odds::Probabilities(runners,restored)==learned,
            "local calibration changed on serialization/restart");
        const auto preserved=restored.Serialize();
        Check(!restored.Parse("TJC_WFA_ODDS 999\n") &&
            !restored.Parse("TJC_WFA_ODDS 1\nnot-a-key 1 0 0 0\n") &&
            !restored.Parse("TJC_WFA_ODDS 1\n"+runners[0].key+" 0 0 1 0\n") &&
            restored.Serialize()==preserved,
            "corrupt/incompatible history replaced previously valid state");
        Check(!restored.Observe(runners[0],true,0) &&
            !restored.Observe(runners[0],true,std::numeric_limits<double>::quiet_NaN()) &&
            !restored.Observe({"bad",100},false) && restored.Serialize()==preserved,
            "invalid times or keys poisoned local observations");
        odds::Calibration dnf;
        const auto baseline=dnf.Predict(runners[0]).dnf;
        Check(dnf.Observe(runners[0],false) && dnf.Predict(runners[0]).dnf>baseline,
            "natural DNF was not reflected in the finishing prior");
        Check(odds::Key("route-A/ranks-9-2")!=odds::Key("route-A/ranks-8-2") &&
            odds::Key("route-A/ranks-9-2")!=odds::Key("route-B/ranks-9-2"),
            "changed local horse mods or routes reuse incompatible history");
        for(int i=0;i<500;++i)Check(dnf.Observe(runners[0],true,150),"long-running learning stopped");
        Check(dnf.Records().at(runners[0].key).finishes+dnf.Records().at(runners[0].key).outs<=129 &&
            restored.Parse(dnf.Serialize()),"bounded effective history became unreadable");
        Ticket fraction;
        Check(fraction.BeginQuoted(horse_races::characters::Id::Sadie,Market::Win,1000,1000,330) &&
            fraction.LockedNetOdds()==330 && fraction.DebitSucceeded() &&
            fraction.Settle(Finish::Placed,1) && fraction.PendingCredit()==4300 &&
            !fraction.Settle(Finish::Placed,1),"fractional locked odds or once-only payout wrong");
        Ticket rounded;
        Check(!rounded.BeginQuoted(horse_races::characters::Id::Karen,Market::Win,103,103,330) &&
            !rounded.BeginQuoted(horse_races::characters::Id::Karen,Market::Win,1000,1000,333) &&
            rounded.BeginQuoted(horse_races::characters::Id::Karen,Market::Win,1000,1000,110) &&
            rounded.DebitSucceeded() && rounded.Settle(Finish::Placed,1) && rounded.PendingCredit()==2100,
            "one-dollar denomination or one-decimal quote not enforced in settlement");
        Ticket dollar;
        Check(dollar.BeginQuoted(horse_races::characters::Id::Arthur,Market::Win,100,100,110) &&
            dollar.DebitSucceeded() && dollar.Settle(Finish::Placed,1) && dollar.PendingCredit()==210 &&
            dollar.CreditSucceeded() && dollar.PendingCredit()==0,
            "$1 at 1.1-to-1 must credit $2.10 gross exactly once, not round to whole dollars");
        Ticket place;
        Check(place.BeginQuoted(horse_races::characters::Id::Karen,Market::TopThree,1000,1000,325) &&
            place.LockedNetOdds()==100 && place.DebitSucceeded() && place.Settle(Finish::Placed,3) &&
            place.PendingCredit()==2000,"WFA Win odds leaked into fixed 1-to-1 Place");
        Check(odds::Label(1000)=="10-to-1" && odds::Label(325)=="3.3-to-1" && odds::Label(310)=="3.1-to-1" &&
            odds::Quantize(999)==9900 && odds::Quantize(.001)==10 && odds::Quantize(0)==0,
            "odds formatting or safe quote bounds wrong");
        Ticket invalid;
        Check(!invalid.BeginQuoted(horse_races::characters::Id::Karen,Market::Win,1000,1000,0) &&
            !invalid.BeginQuoted(horse_races::characters::Id::Karen,Market::Win,1000,1000,9901),
            "invalid quotes accepted");
        std::cout<<"WFA attribute curve, bounded local learning, persistence and locked one-decimal settlement passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
