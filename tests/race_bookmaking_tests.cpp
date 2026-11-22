#include "../src/race_bookmaking.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void Check(bool okay,const char* what) {if(!okay)throw std::runtime_error(what);}
}
int main() {
    using namespace horse_races;
    using namespace bookmaking;
    try {
        const auto john=Roster(characters::Id::John),arthur=Roster(characters::Id::Arthur);
        Check(kHosts.size()==13 && kExhibitionRacers==10 && john.size()==12 && arthur.size()==12,
            "candidate pool must grow without changing the largest venue's ten-rider capacity");
        for(const auto id:{characters::Id::Bill,characters::Id::Javier})
            Check(std::find(john.begin(),john.end(),id)!=john.end() &&
                std::find(arthur.begin(),arthur.end(),id)!=arthur.end(),"new duel host absent from betting pool");
        Check(std::find(john.begin(),john.end(),characters::Id::Arthur)!=john.end() &&
            std::find(john.begin(),john.end(),characters::Id::John)==john.end(),"John-era field must contain Arthur, not John");
        Check(std::find(arthur.begin(),arthur.end(),characters::Id::John)!=arthur.end() &&
            std::find(arthur.begin(),arthur.end(),characters::Id::Arthur)==arthur.end(),"Arthur-era field must contain John, not Arthur");
        Check(Roster(characters::Id::OtherRival).empty(),"unknown protagonist cannot build a narrative field");
        const auto seven=DrawRoster(characters::Id::John,7,[](int minimum,int){return minimum;});
        Check(seven.size()==7 && std::find(seven.begin(),seven.end(),characters::Id::John)==seven.end(),
            "seven-rider WFA draw includes the protagonist");
        auto distinct=seven;std::sort(distinct.begin(),distinct.end());
        Check(std::adjacent_find(distinct.begin(),distinct.end())==distinct.end(),
            "WFA draw duplicated a host");
        Check(DrawRoster(characters::Id::Arthur,13,[](int,int){return 0;}).empty() &&
            DrawRoster(characters::Id::OtherRival,7,[](int,int){return 0;}).empty() &&
            DrawRoster(characters::Id::John,7,[](int,int maximum){return maximum;}).empty(),
            "invalid WFA draws were accepted");
        int draws=0;
        const auto low=DrawSharedRanks([&](int minimum,int maximum){++draws;return minimum;});
        Check(draws==2 && low.speed==5 && low.acceleration==1,"6/2 displayed shared ranks");
        const auto high=DrawSharedRanks([](int,int maximum){return maximum-1;});
        Check(high.speed==9 && high.acceleration==9,"10/10 displayed shared ranks");
        Check(!DrawSharedRanks([](int,int){return -1;}).Valid(),"invalid random result escaped");
        Check(kBurstLevels==.5F && kBurstChargeSeconds==3.0 && kBurstDurationSeconds==5.0,
            "exhibition burst timing or half-level bonus changed");
        for(const auto market:{Market::Win,Market::TopThree})for(int place=1;place<=4;++place) {
            Ticket ticket;
            Check(ticket.Begin(characters::Id::Karen,market,1000,1000),"ticket reservation failed");
            Check(ticket.PendingDebit()==1000 && ticket.DebitSucceeded(),"stake was not debited first");
            Check(ticket.Settle(Finish::Placed,place),"placed ticket did not settle");
            const int expected=Wins(market,place)?1000*(ProfitMultiplier(market)+1):0;
            Check(ticket.PendingCredit()==expected,"gross return disagrees with net-profit odds");
            Check(!ticket.Settle(Finish::Placed,place),"ticket settled twice");
            if(expected)Check(ticket.CreditSucceeded() && ticket.PendingCredit()==0,"credit did not close once");
            Check(ticket.Phase()==Ticket::State::Closed,"settled ticket remained live");
        }
        for(const int count:{6,10}) {
            Ticket handicap;
            Check(handicap.Begin(characters::Id::Sadie,Market::Win,1000,1000,count) &&
                handicap.DebitSucceeded() && handicap.Settle(Finish::Placed,1) &&
                handicap.PendingCredit()==(count+1)*1000,
                "ten-runner Handicap Win must return stake plus 10-to-1 net profit");
            Ticket place;
            Check(place.Begin(characters::Id::Sadie,Market::TopThree,1000,1000,count) &&
                place.DebitSucceeded() && place.Settle(Finish::Placed,3) &&
                place.PendingCredit()==2000,
                "Handicap Place must retain 1-to-1 net profit");
        }
        for(const auto [finish,expected]:{std::pair{Finish::AccidentalVoid,1000},
            std::pair{Finish::PlayerSabotage,0},std::pair{Finish::Lost,0}}) {
            Ticket ticket;
            Check(ticket.Begin(characters::Id::Sadie,Market::Win,1000,1200) && ticket.DebitSucceeded(),
                "cancellation fixture did not reserve stake");
            Check(ticket.Settle(finish,1) && ticket.PendingCredit()==expected,
                "refund or player-sabotage forfeiture is wrong");
        }
        Ticket invalid;
        Check(!invalid.Begin(characters::Id::Dutch,Market::Win,1000,1000) &&
            !invalid.Begin(characters::Id::Karen,Market::Win,1001,1000) &&
            !invalid.Begin(characters::Id::Karen,Market::Win,99,1000) &&
            !invalid.Begin(characters::Id::Karen,Market::Win,101,2000) &&
            !invalid.Begin(characters::Id::Karen,Market::Win,0,1000),"illegal stake or racer accepted");
        std::cout<<"Bookmaker roster, shared ranks, burst policy and once-only cash settlement passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
