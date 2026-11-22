#include "../src/race_circuit.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

using namespace horse_races::circuit;
void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
Layout Square(){Layout l;Check(l.Build({{0,0,0},{100,0,0},{100,100,0},{0,100,0},{0,0,0}}, {1,2,3}),"fixture layout");return l;}
template<class Update> void Lap(const Layout& l,double& now,Update update) {
    for(std::size_t node=1;node<l.path.points.size();++node) {
        const auto a=l.path.points[node-1],b=l.path.points[node];
        for(int step=1;step<=20;++step) {
            const float t=step/20.0F;now+=.1;
            update(Vector3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t},now);
        }
    }
}
int main(){int passed=0;auto test=[&](const char* name,auto fn){fn();++passed;std::cout<<"PASS "<<name<<'\n';};
    try{
        test("legal inner racing lines qualify in order without touching recorded apex planes",[]{
            {
                // The back rows of a starting grid stand behind the first node.
                // Their distance to the course is their lateral offset from the
                // first segment's line, not the distance to the node: on an 8 m
                // corridor a rider 12 m back and 3 m to the side is on course,
                // and one 12 m to the side is not.
                auto l=Square();l.strict_half_width=8;
                Progress inside;Check(inside.Start(l,1,{-12,3,0},0),"grid start behind the line refused");
                inside.Update(l,{-6,3,0},.5);inside.Update(l,{5,3,0},1.0);
                Check(!inside.off_course,"a back-row grid slot inside the corridor was disqualified at the start");
                Progress outside;Check(outside.Start(l,1,{-12,12,0},0),"grid start fixture");
                outside.Update(l,{-12,12,0},.5);
                Check(!outside.off_course&&outside.boundary.Pending(),"outside grid slot lost its two-second grace");
                outside.Update(l,{-12,12,0},2);
                Check(outside.off_course,"outside grid slot was not disqualified after two seconds");
                Progress deep;Check(!deep.Start(l,1,{-45,3,0},0) || (deep.Update(l,{-44,3,0},.5),deep.off_course), // "far" is a windef macro
                    "the staging allowance extends further back than the grid can");
            }
            for(float width:{8.0F,14.0F})for(float offset:{1.0F,width-.1F}) {
                auto layout=Square();layout.strict_half_width=width;
                Session race;Check(race.Freeze(layout,{1,1},[](int,int){return 1;}) &&
                    race.Start({0,0,0},{0,0,0},0),"inner line fixture");
                const std::vector<Vector3> line{{0,0,0},{50,0,0},{50,offset,0},{100-offset,offset,0},
                    {100-offset,100-offset,0},{offset,100-offset,0},{offset,20,0},{0,0,0}};
                double now=0;Outcome outcome=Outcome::None;
                for(std::size_t i=1;i<line.size();++i) {
                    const auto a=line[i-1],b=line[i];const int steps=int(std::ceil(horse_races::destination::Distance(a,b)/1.5F));
                    for(int j=1;j<=steps;++j) {
                        const float t=float(j)/steps;const Vector3 p{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0};
                        const auto next=race.Update(p,p,now+=.1);if(next!=Outcome::None)outcome=next;
                        Check(!race.player.off_course && !race.opponent.off_course,"inside corner treated as an edge violation");
                        if(i<line.size()-1)Check(outcome==Outcome::None,"intermediate checkpoint finished a lap");
                    }
                }
                Check(outcome==Outcome::Tie && race.player.Finished() && race.opponent.Finished(),
                    "legitimate inner racing line failed to finish equally");
                // None of these inner corner segments cross the original first
                // gate's x=100 plane: success must come from legal route progress.
                Check(!layout.gates.front().Crossing(line[3],line[4]),"fixture accidentally used old apex line");
            }
        });
        test("inner-line checkpoint alternative cannot count stationary proximity or reverse progress",[]{
            auto l=Square();l.strict_half_width=8;Progress p;p.Start(l,1,{0,0,0},0);double now=0;
            for(int x=1;x<=70;++x)p.Update(l,{float(x),0,0},now+=.1);
            for(int i=0;i<100;++i)p.Update(l,{70,0,0},now+=.1);
            Check(p.next_gate==0 && p.completed_laps==0,"stationary course projection awarded a checkpoint");
            for(int x=69;x>=0;--x)p.Update(l,{float(x),0,0},now+=.1);
            Check(p.next_gate==0 && !p.continuity_lost && !p.off_course,"on-track reversal awarded progress or DQ");
        });
        test("finish versus infraction uses chronological fractions, not player-first update order",[]{
            Advance p,o;p.finish_fraction=.2F;o.disqualification_fraction=.8F;
            auto d=Decide(p,o);Check(d.outcome==Outcome::PlayerWon && !d.disqualification,"later foul replaced earlier finish");
            p={};o={};p.disqualification_fraction=.2F;o.finish_fraction=.8F;
            d=Decide(p,o);Check(d.outcome==Outcome::OpponentWon && d.disqualification,"later finish hid earlier foul");
            o.disqualification_fraction=.2F;d=Decide(p,o);
            Check(d.outcome==Outcome::Tie && d.disqualification,"simultaneous fouls biased to one rider");
        });
        test("an actual finish before a later same-frame section jump remains finished",[]{
            auto l=Square();l.strict_half_width=8;Progress p;
            Check(p.Start(l,1,{0,0,0},0)&&p.Resume(l,{0,1,0},0),"finish-before-jump fixture");
            Check(p.next_gate==3&&std::abs(p.Metres(l)-399)<.01F,"finish fixture resumed on wrong leg");
            const auto finished=p.Update(l,{20,-3,0},.5);
            Check(finished.finish_fraction&&std::abs(*finished.finish_fraction-.25F)<.001F&&p.Finished()&&
                !p.off_course&&!p.section_jump&&!finished.section_jump&&!finished.disqualification_fraction,
                "later section jump erased an earlier legal finish or left foul diagnostics");
            Progress earlier;Check(earlier.Start(l,1,{0,0,0},0)&&earlier.Resume(l,{0,20,0},0),"jump-before-finish fixture");
            const auto rejected=earlier.Update(l,{20,-3,0},.7);
            Check(!rejected.finish_fraction&&!earlier.Finished()&&earlier.off_course&&earlier.section_jump&&
                rejected.disqualification_fraction,"a later finish erased an earlier section jump");
        });
        test("boundary transition refinement uses the local road beside a higher bridge",[]{
            Layout l;Check(l.Build({{0,0,0},{100,0,0},{200,0,0},{200,100,0},
                {100,100,7.5F},{100,1.5F,7.5F},{0,1.5F,7.5F},{0,0,0}},{1,3,4}),"bridge fixture");
            l.strict_half_width=8;l.strict_height=6;Progress p;
            Check(p.Start(l,1,{0,0,0},0),"bridge start");double now=0;
            for(int x=5;x<=50;x+=5)p.Update(l,{float(x),0,0},now+=.1);
            p.Update(l,{50,7.1F,0},now+=.2);
            Check(!p.boundary.Outside()&&!l.StrictProjection({50,7.1F,0},0,l.path.Length()).valid,
                "bridge fixture failed to distinguish valid local road from closer elevated projection");
            p.Update(l,{50,9.1F,0},now+=.2);
            Check(p.boundary.Pending()&&std::abs(p.boundary.OutsideElapsed(now)-.11)<.001&&
                !p.off_course&&!p.section_jump,"transition moved from the actual local edge to the bridge shadow");
        });
        test("each circuit has its own lateral tolerance",[]{
            for(float width:{8.0F,14.0F}) {
                auto l=Square();l.strict_half_width=width;
                Progress p;Check(p.Start(l,1,{0,0,0},0),"width fixture");double now=0;
                for(int x=1;x<=40;++x)p.Update(l,{float(x),0,0},now+=.1);
                for(int y=1;y<=int(width);++y)Check(!p.Update(l,{40,float(y),0},now+=.1).disqualification_fraction,
                    "at/inside width incorrectly disqualified");
                Check(!p.Update(l,{40,width+.25F,0},now+=.1).disqualification_fraction && !p.off_course,
                    "leaving configured tolerance bypassed the two-second grace");
                const double entered=now;
                p.Update(l,{40,width+.25F,0},entered+1.8);
                Check(!p.off_course,"short outside episode disqualified");
                Check(p.Update(l,{40,width+.25F,0},entered+2.1).disqualification_fraction.has_value() && p.off_course,
                    "two-second continuous corridor exit did not disqualify");
            }
        });
        test("closed geometry and ordered spaced road gates are mandatory",[]{
            auto l=Square();const auto points=l.path.points;
            Check(!l.Build(points,{1,3,2}) && !l.Ready(),"unordered checkpoint accepted");
            Check(!l.Build(points,{1,2,4}),"lap line accepted as intermediate checkpoint");
            Check(!l.Build(points,{1,2}),"insufficient route qualification");
            auto open=points;open.back().y=30;
            Check(!l.Build(open,{1,2,3}),"open destination route silently treated as lap");
            Check(!l.Build(points,{1,2,3},50),"unbounded road width");
            Check(l.Build(points,{1,2,3}),"valid layout cannot recover");
        });
        test("spawn on finish and repeated position samples cannot grant a lap",[]{
            const auto l=Square();Progress p;Check(p.Start(l,3,{0,0,0},0),"start failed");
            for(int i=1;i<200;++i)p.Update(l,{0,0,0},i*.1);
            Check(p.completed_laps==0 && p.next_gate==0 && !p.Finished(),"spawn/stationary lap counted");
            Check(!p.Start(l,3,{80,0,0},0),"spawned mid-course");
        });
        test("three proper laps qualify independently and only final lap finishes",[]{
            const auto l=Square();Progress p;p.Start(l,3,{0,0,0},0);double now=0;int finishes=0;
            for(int i=1;i<=3;++i){Lap(l,now,[&](Vector3 pos,double t){finishes+=p.Update(l,pos,t).finish_fraction.has_value();});
                Check(p.completed_laps==i && p.DisplayLap()==std::min(i+1,3),"lap/display count mismatch");
                Check(p.Finished()==(i==3),"intermediate lap ended race");}
            Check(finishes==1 && p.Metres(l)==1200,"missing/repeated finish or total distance");
            p.Update(l,{0,0,0},now+.1);Check(p.completed_laps==3,"finish counted twice");
        });
        test("required checkpoints cannot be replaced with arbitrary distance or finish proximity",[]{
            const auto l=Square();Progress p;p.Start(l,1,{0,0,0},0);double now=0;
            for(int round=0;round<5;++round)for(int i=0;i<40;++i){now+=.1;p.Update(l,{i<20?i*4.0F:(40-i)*4.0F,0,0},now);}
            Check(p.completed_laps==0 && p.next_gate==0,"out-and-back miles completed a loop");
        });
        test("reverse crossings, parallel roads and bridges cannot qualify",[]{
            const auto g=Square().gates.front();
            Check(!g.Crossing({105,0,0},{95,0,0}),"wrong-way gate");
            Check(!g.Crossing({95,20,0},{105,20,0}),"parallel road gate");
            Check(!g.Crossing({95,0,12},{105,0,12}),"different bridge height gate");
            const auto crossed=g.Crossing({95,10,3},{105,10,3});
            Check(crossed && std::abs(*crossed-.5F)<.0001F,"normal traffic avoidance rejected");
        });
        test("continuous movement segments catch fast crossings rather than testing exact points",[]{
            const auto g=Square().gates.front();
            const auto crossed=g.Crossing({80,0,0},{115,0,0});
            Check(crossed && *crossed>0 && *crossed<1,"swept crossing missed");
            Check(!g.Crossing({100,0,0},{105,0,0}),"starting inside gate counted twice");
        });
        test("teleport quarantine cannot be cleared by standing at the destination",[]{
            const auto l=Square();Progress p;p.Start(l,1,{0,0,0},0);
            Check(p.Update(l,{100,100,0},.1).discontinuity,"warp not detected");
            p.Update(l,{100,100,0},.2);Check(p.continuity_lost && p.next_gate==0,"stationary warp became legal");
            p.Update(l,{0,0,0},.3);p.Update(l,{0,0,0},.4);
            Check(!p.continuity_lost && p.completed_laps==0,"return recovery counted a gate");
            double now=.4;Lap(l,now,[&](Vector3 pos,double t){p.Update(l,pos,t);});Check(p.Finished(),"legitimate recovery cannot finish");
        });
        test("mod-owned unstick rebases the sample without granting a gate",[]{
            const auto l=Square();Progress p;Check(p.Start(l,1,{0,0,0},0),"start failed");
            double now=0;
            for(int x=5;x<=70;x+=5)p.Update(l,{float(x),0,0},now+=.1);
            Check(p.next_gate==0 && p.Metres(l)>=65,"fixture did not reach the stuck section");
            Check(!p.RebaseAfterModMove(l,{50,50,0},now+.1),"off-line recovery was accepted");
            Check(p.RebaseAfterModMove(l,{55,0,0},now+.1),"on-line mod recovery refused");
            Check(p.next_gate==0 && p.completed_laps==0 && !p.continuity_lost && p.Metres(l)<=70,
                "relocation awarded progress or lost continuity");
            now+=.1;
            for(int x=60;x<=100;x+=5)p.Update(l,{float(x),0,0},now+=.1);
            Check(!p.continuity_lost && p.next_gate==1,"rider never escaped recovery loop at next gate");
        });
        test("pause without movement is safe but missing moving samples cannot earn gates",[]{
            const auto l=Square();Progress p;p.Start(l,1,{0,0,0},0);
            p.Update(l,{0,0,0},20);Check(!p.continuity_lost,"stationary pause invalidated race");
            p.Update(l,{100,0,0},22);Check(p.continuity_lost && p.next_gate==0,"sample gap awarded gate");
        });
        test("lap policy is frozen once per NPC instance, including practice",[]{
            auto l=Square();Session s;int draws=0;
            auto draw=[&](int lo,int hi){++draws;Check(lo==2 && hi==5,"wrong exclusive random range");return 3;};
            Check(s.Freeze(l,{2,4},draw) && s.Laps()==3 && draws==1,"NPC did not freeze random laps");
            Check(!s.Freeze(l,{2,4},draw) && draws==1,"same host redrew laps");
            l.path.points[0].x=800;Check(s.Course().path.points.front().x==0,"live route mutation changed instance");
            Check(s.Start({0,0,0},{0,0,0},0),"frozen session start");
            Check(s.Hud().PlayerRow("Arthur Morgan",false)=="Arthur · Lap 1/3" &&
                s.Hud().PlayerRow("亚瑟·摩根",true)=="亚瑟 · 第 1/3 圈","HUD contract");
            s.Reset();Check(!s.Hud().visible && s.Hud().PlayerRow("Arthur",true).empty() && s.Laps()==0,"cancel leaked HUD/laps");
        });
        test("fixed policy never asks the player or randomizes",[]{
            Session s;int draws=0;Check(s.Freeze(Square(),{3,3},[&](int,int){++draws;return 1;}) && !draws,"fixed laps drew RNG");
            Check(!Policy{0,3}.Valid() && !Policy{4,3}.Valid() && !Policy{1,21}.Valid(),"invalid lap range");
            Session invalid;Check(!invalid.Freeze(Square(),{2,4},[](int,int){return 5;}),"out of range RNG accepted");
        });
        for(int mode=0;mode<3;++mode)test(mode==0?"identical movement yields a dead heat":mode==1?"player can win without opponent reaching final lap":"opponent uses exactly the same lap rules",[=]{
            const auto l=Square();Session s;s.Freeze(l,{2,2},[](int,int){return 2;});s.Start({0,0,0},{0,0,0},0);
            double now=0;int results=0;Outcome result=Outcome::None;
            for(int lap=0;lap<2;++lap)Lap(l,now,[&](Vector3 pos,double t){
                const auto next=s.Update(mode==2?Vector3{}:pos,mode==1?Vector3{}:pos,t);
                if(next!=Outcome::None){++results;result=next;}
            });
            Check(result==(mode==0?Outcome::Tie:mode==1?Outcome::PlayerWon:Outcome::OpponentWon) && results==1,"wrong finish comparison");
            Check(!s.Hud().visible && s.Update({0,0,0},{0,0,0},now+1)==Outcome::None,"duplicate result/HUD after finish");
        });
        for(bool player_first:{true,false})test(player_first?"same-frame final crossings compare swept fractions, player first":"same-frame final crossings compare swept fractions, opponent first",[=]{
            const auto l=Square();Session s;s.Freeze(l,{1,1},[](int,int){return 1;});s.Start({0,0,0},{0,0,0},0);
            double now=0;int step=0;
            Lap(l,now,[&](Vector3 pos,double t){if(++step<80)Check(s.Update(pos,pos,t)==Outcome::None,"finished before final crossing");});
            const Vector3 earlier{0,-5,0},later{0,-1,0};
            Check(s.Update(player_first?earlier:later,player_first?later:earlier,now+.1)==
                (player_first?Outcome::PlayerWon:Outcome::OpponentWon),"same-frame winner ignored crossing fraction");
            Check(s.player.Finished() && s.opponent.Finished(),"both crossings must qualify");
        });
        test("lapping and last-lap labels use qualified distance not straight distance to finish",[]{
            const auto l=Square();Session s;s.Freeze(l,{3,3},[](int,int){return 3;});s.Start({0,0,0},{0,0,0},0);double now=0;
            for(int lap=0;lap<2;++lap)Lap(l,now,[&](Vector3 pos,double t){s.Update(pos,{0,0,0},t);});
            Check(s.player.Metres(l)==800 && s.opponent.Metres(l)==0 && s.Hud().FinalLap(),"lapped ordering or final lap wrong");
        });
        test("rejoining another section of the same lap is a foul, not silent progress",[]{
            // Figure eight: the recorded line crosses itself at (50,50), so a
            // rider can rejoin 240 m further along without ever leaving tarmac.
            Layout eight;
            Check(eight.Build({{0,0,0},{100,100,0},{100,0,0},{0,100,0},{0,0,0}},{1,2,3},14,6),"figure-eight fixture");
            eight.strict_half_width=14;eight.strict_height=6; // Production circuits configure the corridor.
            Progress p;Check(p.Start(eight,1,{0,0,0},0),"figure-eight start");
            double now=0;Advance last;
            auto ride=[&](Vector3 from,Vector3 to) {
                const int steps=int(std::ceil(horse_races::destination::Distance(from,to)));
                for(int i=1;i<=steps && !p.off_course;++i) {
                    const float t=float(i)/steps;now+=.05;
                    last=p.Update(eight,{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,0},now);
                }
            };
            ride({0,0,0},{50,50,0});
            Check(!p.off_course && !last.disqualification_fraction,"legal opening leg rejected");
            ride({50,50,0},{20,80,0}); // The far diagonal: same road, 240 m ahead.
            Check(p.off_course && p.section_jump && last.section_jump && last.disqualification_fraction,
                "shortcut onto a later section counted as ordinary riding");
            Check(p.offence_along>0 && p.offence_along<eight.gates.front().along,"offence recorded at the wrong progress");
        });
        test("leaving the corridor is still reported as leaving it, not as a shortcut",[]{
            // A square has no second section to rejoin, so riding off the side
            // of the road can only ever be an ordinary corridor exit.
            auto square=Square();square.strict_half_width=14;square.strict_height=6;
            Progress p;Check(p.Start(square,1,{0,0,0},0),"square start");
            double now=0;Advance last;
            for(int i=1;i<=50 && !p.off_course;++i){now+=.05;p.Update(square,{float(i),0,0},now);}
            Check(!p.off_course,"opening straight rejected");
            for(int i=1;i<=40 && !p.off_course;++i){now+=.05;last=p.Update(square,{50,-float(i),0},now);}
            if(!p.off_course)last=p.Update(square,{50,-40,0},now+2);
            Check(p.off_course && !p.section_jump && !last.section_jump,"corridor exit mislabelled as a section skip");
        });
        test("NPC boundary offenses require two separated continuous episodes",[]{
            auto l=Square();l.strict_half_width=8;
            Progress p;Check(p.Start(l,1,{0,0,0},0,2),"NPC episode fixture");
            p.Update(l,{10,0,0},.2);p.Update(l,{10,-10,0},.4);
            Check(!p.off_course&&p.boundary.Pending(),"first exit immediately eliminated NPC");
            const auto first=p.Update(l,{10,-10,0},2.4);
            Check(first.boundary_offenses==1&&first.boundary_offense_fraction&&
                !first.disqualification_fraction&&!p.off_course&&p.boundary.Offenses()==1,
                "NPC first offense did not remain a warning");
            const auto sustained=p.Update(l,{10,-10,0},9);
            Check(sustained.boundary_offenses==0&&p.boundary.Offenses()==1&&!p.off_course,
                "one sustained excursion spent both NPC offenses");
            p.Update(l,{10,0,0},9.2);Check(!p.boundary.Outside(),"inside did not rearm next episode");
            p.Update(l,{10,-10,0},9.4);
            const auto second=p.Update(l,{10,-10,0},11.4);
            Check(second.boundary_offenses==1&&second.disqualification_fraction&&p.off_course&&
                p.boundary.Offenses()==2&&!p.section_jump,"second independent episode failed to eliminate NPC");
        });
        test("short excursions reset and tolerated NPC travel retains ordered checkpoints",[]{
            auto l=Square();l.strict_half_width=8;
            Progress player;Check(player.Start(l,1,{0,0,0},0),"short episode fixture");
            player.Update(l,{10,0,0},.2);player.Update(l,{10,-10,0},.4);
            player.Update(l,{10,-10,0},1.5);player.Update(l,{10,0,0},1.7);
            player.Update(l,{10,-10,0},1.9);player.Update(l,{10,-10,0},3);
            player.Update(l,{10,0,0},3.2);
            Check(!player.off_course&&player.boundary.Offenses()==0&&!player.boundary.Pending(),
                "separate sub-two-second episodes accumulated into a foul");
            Progress npc;Check(npc.Start(l,1,{0,0,0},0,2),"checkpoint grace fixture");double now=0;
            for(int x=10;x<=90;x+=10)npc.Update(l,{float(x),0,0},now+=.2);
            npc.Update(l,{90,-10,0},now+=.2);npc.Update(l,{90,-10,0},now+=2.1);
            Check(npc.boundary.Offenses()==1&&!npc.off_course,"first long episode was not tolerated");
            for(auto point:{Vector3{100,-10,0},Vector3{110,-10,0},Vector3{110,0,0},Vector3{110,10,0},Vector3{100,20,0}})
                npc.Update(l,point,now+=.3);
            Check(!npc.off_course&&!npc.section_jump&&!npc.continuity_lost&&npc.next_gate==1&&npc.Metres(l)>=119,
                "tolerated excursion froze waypoint progress or mislabelled reentry as a skipped section");
        });
        test("outside grace does not permit finishing outside the strict corridor",[]{
            for(bool cylinder:{false,true}) {
                // Keep the finish away from the starting grid's deliberate
                // extension and from every other legal section of this route.
                Layout l;Check(l.Build({{0,0,0},{100,0,0},{100,100,0},{200,100,0},{200,0,0}},
                    {1,2,3},14,6,true),"distinct finish fixture");
                l.strict_half_width=8;l.destination_finish=cylinder;Progress p;
                Check(!l.StrictProjection({210,0,0},0,l.path.Length()).valid,"outside finish fixture is actually on another legal section");
                Check(p.Start(l,1,{0,0,0},0,2),"strict finish fixture");double now=0;
                for(std::size_t segment=0;segment<4;++segment) {
                    const auto a=l.path.points[segment],b=l.path.points[segment+1];
                    const int count=segment==3?16:20;
                    for(int k=1;k<=count;++k){const float t=k/20.F;p.Update(l,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0},now+=.1);}
                }
                p.Update(l,{210,20,0},now+=.2);
                const auto finish=p.Update(l,{210,-5,0},now+=.5);
                Check(!finish.finish_fraction&&!p.Finished()&&!p.off_course&&p.boundary.Offenses()==0,
                    "wide finish gate/cylinder accepted an outside rider during grace");
            }
        });
        test("a previous independently surveyed lap cannot widen the final finish corridor",[]{
            Layout l;Check(l.Build({{200,0,0},{300,0,0},{300,100,0},{200,100,0},{200,0,0},
                {100,0,0},{100,100,0},{200,100,0},{200,0,0}},{1,2,3,4,5,6,7}),"two surveyed finish laps");
            const std::array<std::size_t,1> ends{4};Check(l.SetPhysicalLaps(ends),"finish lap identities");
            l.strict_half_width=8;Progress p;Check(p.Start(l,1,{200,0,0},0,2),"two lap finish start");double now=0;
            for(std::size_t segment=0;segment<8;++segment) {
                const auto a=l.path.points[segment],b=l.path.points[segment+1];
                for(int k=1;k<=(segment==7?16:20);++k) {
                    const float t=k/20.F;p.Update(l,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0},now+=.1);
                }
            }
            Check(p.next_gate==7&&!p.off_course,"final finish fixture failed ordered lap transition");
            const auto final=l.LapForGate(p.next_gate);
            Check(l.StrictProjection({210,0,0},0,l.path.Length()).valid&&
                !l.StrictProjection({210,0,0},final.low,final.high).valid,"old lap does not widen fixture finish");
            p.Update(l,{210,20,0},now+=.2);
            const auto result=p.Update(l,{210,-5,0},now+=.5);
            Check(!result.finish_fraction&&!p.Finished()&&!p.off_course&&p.boundary.Pending(),
                "previous lap widened terminal finish or turned a short boundary episode into immediate DQ");
        });
        test("an inside line within the corridor keeps qualifying on a self-crossing lap",[]{
            Layout eight;
            Check(eight.Build({{0,0,0},{100,100,0},{100,0,0},{0,100,0},{0,0,0}},{1,2,3},14,6),"figure-eight fixture");
            eight.strict_half_width=14;eight.strict_height=6; // Production circuits configure the corridor.
            Progress p;Check(p.Start(eight,1,{0,0,0},0),"figure-eight start");
            double now=0;
            // Ride the whole first leg on the inside edge of the corridor,
            // easing off the recorded line rather than teleporting onto it.
            for(int i=1;i<=140 && !p.off_course;++i) {
                now+=.05;const float d=float(i),k=std::min(1.0F,d/10);
                p.Update(eight,{d*.7071F+8*k,d*.7071F-8*k,0},now);
            }
            Check(!p.off_course && !p.section_jump && p.Metres(eight)>100,"legal inside line judged a foul");
        });
        test("long course uses local ordered projections for sixteen normal racers",[]{
            std::vector<Vector3> points{{0,0,0}};
            for(int x=1;x<=1000;++x)points.push_back({float(x),0,0});
            for(int y=1;y<=1000;++y)points.push_back({1000,float(y),0});
            for(int x=999;x>=0;--x)points.push_back({float(x),1000,0});
            for(int y=999;y>=0;--y)points.push_back({0,float(y),0});
            Layout layout;Check(layout.Build(points,{1000,2000,3000}),"long course fixture");layout.strict_half_width=8;
            std::array<Progress,16> riders;
            for(auto& rider:riders)Check(rider.Start(layout,1,{0,0,0},0,2),"long course rider start");
            const auto began=std::chrono::steady_clock::now();
            for(int tick=1;tick<=600;++tick)for(auto& rider:riders)rider.Update(layout,{tick*.5F,0,0},tick*.05);
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            for(const auto& rider:riders)Check(!rider.off_course&&!rider.continuity_lost&&
                rider.boundary.Offenses()==0&&std::abs(rider.Metres(layout)-300)<.01F,"local normal-race projection lost progress");
            // Work bound independent of the elapsed-time report: this normal
            // frame visits the local 51-52 segments, not all 4,000 per query.
            const auto local=layout.path.Segment(325)-layout.path.Segment(275)+2;
            Check(local<=52,"local projection expanded to a whole-course scan");
            std::cout<<"PERF boundary 16 riders x 600 frames, 4000 segments: "<<elapsed
                <<" ms; normal local query <= "<<local<<" segments (full query 4000)\n";
        });
        std::cout<<passed<<" circuit preparation scenarios passed; no game route enabled\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<passed<<": "<<e.what()<<'\n';return 1;}
}
