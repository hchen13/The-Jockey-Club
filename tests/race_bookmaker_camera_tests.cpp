#include "../src/race_bookmaker_camera.h"
#include "../src/race_bookmaker_moments.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
const auto first=[](int){return 0;};
}
int main(){
    using namespace horse_races::bookmaking;
    try{
        for(const std::size_t count:{2,6,7,10}){
            std::vector<std::size_t> entrants;
            for(std::size_t i=0;i<count;++i)entrants.push_back(i);
            CameraDirector camera;
            const std::size_t backed=count-1;
            double now=0;
            for(int cycle=0;cycle<4;++cycle){
                std::vector<std::size_t> observed;
                for(std::size_t turn=0;turn<count;++turn){
                    camera.Tick(true,false,false,now,entrants,first,backed);
                    Check(camera.target.has_value(),"follow could not select a live rider");
                    if(turn==0)Check(camera.target==backed,"backed rider did not get priority in a new follow cycle");
                    observed.push_back(*camera.target);
                    Check(camera.cinematic_pool.viewed.empty(),"follow consumed the cinematic pool");
                    camera.Tick(false,false,false,now+.1,entrants,first,backed);
                    Check(!camera.target,"releasing Aim did not return to player view");
                    now+=1;
                }
                std::sort(observed.begin(),observed.end());
                Check(observed==entrants,"follow repeated or starved someone before completing a cycle");
            }
            camera.Tick(false,true,false,now,entrants,first,backed);
            Check(camera.target==backed && camera.last_cut==CameraDirector::Cut::Cinematic,
                "cinematic entry did not lock the wager independently of follow history");
            std::vector<std::size_t> cuts{*camera.target};
            for(std::size_t turn=1;turn<count;++turn){
                now+=1;camera.Tick(false,false,true,now,entrants,first,backed);
                cuts.push_back(*camera.target);
            }
            std::sort(cuts.begin(),cuts.end());
            Check(cuts==entrants,"cinematic manual C repeated or starved a rider");
            camera.Tick(false,false,false,now+14.99,entrants,first);
            const auto last=camera.target;
            Check(camera.last_cut==CameraDirector::Cut::None,"normal rider hold was shortened");
            camera.Tick(false,false,false,now+1800,entrants,first);
            Check(camera.target==last && camera.last_cut==CameraDirector::Cut::None,
                "time passing changed the locked cinematic rider");
            camera.Tick(false,false,true,now+1801,entrants,first);
            Check(camera.target!=last && camera.cinematic_pool.viewed.size()==1,
                "C did not refill its pool or repeated across cycle boundary");
            camera.Reset();
            Check(!camera.target && !camera.automatic && camera.follow_pool.viewed.empty() &&
                camera.cinematic_pool.viewed.empty(),"a new race retained an old camera cycle");
            camera.Tick(false,true,false,0,entrants,first);
            Check(camera.target==entrants.front(),"unbacked cinematic entry did not lock the leader");
            const auto locked=camera.target;
            for(std::size_t turn=1;turn<count;++turn){
                camera.Tick(false,false,false,300.0*static_cast<double>(turn),entrants,first);
                Check(camera.target==locked,"native shot time automatically selected another rider");
            }
        }
        {
            CameraDirector camera;
            const std::array<std::size_t,4> entrants{0,1,2,3};
            camera.Tick(true,false,false,0,entrants,first,3);
            Check(camera.target==3,"wager priority missing");
            camera.Tick(true,false,true,1,entrants,first,3);
            Check(camera.target==0 && camera.last_cut==CameraDirector::Cut::Manual,
                "C while following did not consume another follow turn");
            camera.Tick(false,false,false,2,entrants,first);
            camera.Tick(false,true,false,3,entrants,first,3);
            Check(camera.target==3 && camera.cinematic_pool.viewed==std::vector<std::size_t>{3},
                "cinematic did not independently lock the backed rider");
            camera.Tick(false,true,false,4,entrants,first);
            camera.Tick(true,false,false,5,entrants,first,3);
            Check(camera.target==1,"switching modes reset the follow pool or repeated the wager");
            camera.Tick(false,false,false,6,entrants,first);
            camera.Tick(false,true,false,7,entrants,first);
            Check(camera.target==0 && camera.cinematic_pool.viewed==std::vector<std::size_t>({3,0}),
                "unbacked cinematic re-entry did not lock the leader or preserved history incorrectly");
            const auto state=camera.cinematic_pool.viewed;
            camera.Tick(false,false,false,7,entrants,first);
            Check(camera.cinematic_pool.viewed==state,"a non-advancing game clock consumed a turn");
            const std::array<std::size_t,1> survivor{2};
            camera.Tick(false,false,false,8,survivor,first);
            Check(camera.target==2 && camera.last_cut==CameraDirector::Cut::Fallback,
                "an OUT/finished camera target was not replaced by a survivor");
            camera.Tick(false,false,false,9,{},first);
            Check(!camera.target && !camera.cinematic_pool.viewed.empty(),"empty eligibility destroyed cycle history");
        }
        {
            CameraDirector camera;
            const std::array<std::size_t,4> race_order{2,0,3,1},new_order{1,3,0,2};
            camera.Tick(false,true,false,0,race_order,first);
            Check(camera.target==2,"cinematic confused roster index zero with first place");
            camera.Tick(false,false,false,300,new_order,first);
            Check(camera.target==2,"an overtake automatically changed the locked rider");
            camera.Tick(false,true,false,301,new_order,first);
            camera.Tick(false,true,false,302,new_order,first);
            Check(camera.target==1,"cinematic re-entry did not use the new leader");
            camera.Tick(false,true,false,303,new_order,first,3);
            camera.Tick(false,true,false,304,new_order,first,3);
            Check(camera.target==3,"bet selection did not override the current leader on re-entry");
            camera.Tick(false,false,true,305,new_order,first,3);
            Check(camera.target==0,"manual C repeated an already-viewed rider instead of finishing its cycle");
            camera.Tick(false,true,false,306,new_order,first,3);
            camera.Tick(false,true,false,307,new_order,first,3);
            Check(camera.target==3 && camera.cinematic_pool.viewed.size()==4,
                "explicit backed re-entry failed or duplicated a viewed-pool entry");
            const std::array<std::size_t,3> survivors{1,0,2};
            camera.Tick(false,true,false,308,survivors,first,3);
            camera.Tick(false,true,false,309,survivors,first,3);
            Check(camera.target==1,"an unavailable backed rider did not fall back to the live leader");
            camera.Tick(false,true,false,310,{},first,0);
            camera.Tick(false,true,false,311,{},first,0);
            Check(!camera.target && camera.cinematic_entry_pending,"empty entry lost its pending initial selection");
            camera.Tick(false,false,false,312,survivors,first,0);
            Check(camera.target==0,"delayed eligibility did not honor the entry's bet priority");
            const auto locked=camera.target;
            camera.Tick(true,false,false,313,new_order,first,3);
            Check(camera.target==locked && camera.automatic,"holding Aim hijacked cinematic mode");
            camera.Tick(false,false,false,std::numeric_limits<double>::quiet_NaN(),new_order,first,3);
            Check(camera.target==locked,"invalid game time changed the camera lock");
        }
        {
            CameraTurnPool pool;
            const std::array<std::size_t,3> entrants{0,1,2};
            Check(pool.Pick(entrants,{}, {},false,first)==0,"initial pool selection failed");
            const std::array<std::size_t,2> temporary{1,2};
            Check(pool.Pick(temporary,{},0,false,first)==1,"temporary camera failure blocked survivors");
            Check(pool.Pick(entrants,{},1,false,first)==2,"returning viewed rider stole a new turn");
            const std::array<std::size_t,1> not_eligible{9};
            const auto viewed=pool.viewed;
            Check(!pool.Pick(entrants,not_eligible,2,true,first) && pool.viewed==viewed,
                "an ineligible highlight erased/refilled a completed cycle");
            const std::array<std::size_t,1> preferred{1};
            Check(pool.Pick(entrants,preferred,2,true,first)==1 && pool.viewed.size()==1,
                "eligible preferred incident could not begin a completed pool's new cycle");
            const std::array<std::size_t,4> duplicate{0,0,1,2};
            pool.Reset();
            for(int turn=0;turn<3;++turn)Check(pool.Pick(duplicate,{}, {},false,
                [](int count){return count+50;}).has_value(),"out-of-range RNG did not clamp safely");
            Check(pool.viewed.size()==3,"duplicate eligibility counted as extra entrants");
        }
        {
            CameraMoments moments;
            std::array<CameraMoments::Sample,3> samples{{
                {0,{10,0,0},10,true},{1,{6,1,0},6,true},{2,{70,0,0},1,true}}};
            const std::array<std::size_t,3> entrants{0,1,2};
            moments.Observe(samples,0);
            Check(moments.Candidates(entrants,samples,0).empty(),"grid positions became an overtake event");
            samples[0].metres=12;samples[1].metres=13;moments.Observe(samples,4);
            Check(moments.Candidates(entrants,samples,4).empty(),"sub-threshold rank jitter became a pass");
            samples[1].metres=14;moments.Observe(samples,4.1);
            Check(moments.Candidates(entrants,samples,4.1)==std::vector<std::size_t>({1,0}),
                "pass did not prioritize the overtaking rider then its counterpart");
            Check(moments.Candidates(entrants,samples,4.2,1).empty(),"an already filmed pass caused a redundant cut");
            samples[0].metres=1000;samples[1].metres=5;moments.Observe(samples,8);
            Check(moments.Candidates(entrants,samples,8).empty(),"different laps or stale moments caused a pass cut");
            moments.Fall(0,{5,0,0},9);
            samples[0].racing=false;samples[1].point={7,0,0};samples[2].point={50,0,0};
            moments.Observe(samples,9);
            const std::array<std::size_t,2> survivors{1,2};
            Check(moments.Candidates(survivors,samples,9)==std::vector<std::size_t>({1}),
                "fall coverage selected an OUT rider or a rider far from the incident");
            Check(moments.Candidates(survivors,samples,9,1).empty(),"an already visible fall caused a redundant cut");
            Check(moments.Candidates(survivors,samples,12.1).empty(),"an old fall remained a new exciting moment");
            moments.Reset();samples[0].racing=true;samples[0].metres=10;samples[1].metres=6;
            moments.Observe(samples,20);samples[1].metres=12;moments.Observe(samples,21);
            Check(moments.Candidates(entrants,samples,21).empty(),"opening grid acceleration caused premature highlights");
            samples[0].point.x=std::numeric_limits<float>::quiet_NaN();
            samples[0].metres=20;moments.Observe(samples,25);
            moments.Fall(0,samples[0].point,25);
            Check(moments.Candidates(entrants,samples,25).empty(),"invalid coordinates generated an exciting cut");
        }
        std::cout<<"Separate fair camera cycles, backed/leader cinematic entry, persistent locks, manual C and fallback passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
