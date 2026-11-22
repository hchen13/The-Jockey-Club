// Pure: which dose was taken, how long the burst runs, and what force it is.
#include "../src/player_stimulant.h"
#include "../src/stimulant_hud.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace horse_races::stimulant;
namespace {
void Check(bool ok,const std::string& what){if(!ok)throw std::runtime_error(what);}
}
int main() {
    try {
        Config config;Check(config.Valid(),"default config");
        Tracker t;
        // The burst layer sees confirmed doses, never inventory or core state.
        Check(t.Update(10,Tier::None,2,config)==Tier::None && !t.Active(10),"no consumption started a burst");
        // A regular dose at bonding 2: 8 s x 1.5.
        Check(t.Update(11,Tier::Regular,2,config)==Tier::Regular && t.Active(11) && std::abs(t.Seconds()-12.0)<1e-9 && t.Bonding()==2,"regular dose");
        Check(t.Active(22.9) && !t.Active(23.01) && std::abs(t.Remaining(12)-11.0)<1e-9,"regular burst length");
        // A potent dose at bonding 4 restarts the burst: 10 s x 2.5.
        Check(t.Update(12,Tier::Potent,4,config)==Tier::Potent && std::abs(t.Seconds()-25.0)<1e-9 && t.Active(36.9) && !t.Active(37.1),"potent dose");
        // Special at bonding 1: 20 s. Bonding outside 1..5 is clamped.
        Check(t.Update(30,Tier::Special,0,config)==Tier::Special && std::abs(t.Seconds()-20.0)<1e-9 && t.Bonding()==1,"special dose");
        Check(t.Update(40,Tier::None,9,config)==Tier::None,"no dose counted at clamp");
        Check(t.Doses()==3,"dose count");
        Check(t.Update(41,static_cast<Tier>(-1),1,config)==Tier::None,"an invalid tier was a dose");
        t.Reset();Check(t.Update(50,Tier::None,1,config)==Tier::None && t.Update(51,Tier::Regular,3,config)==Tier::Regular && std::abs(t.Seconds()-16.0)<1e-9,"reset burst");
        // Level 5 is the game's bonding rank 4, one more than level 4: 8 s x 3.
        Check(t.Update(52,Tier::Regular,5,config)==Tier::Regular && std::abs(t.Seconds()-24.0)<1e-9 && t.Bonding()==5,"level 5 (rank 4) not distinct from level 4");
        t.Expire();Check(!t.Active(51.5),"expire");
        // The burst's levels: 11/10 over a low mount, never below the mount's own, nothing for an unreported mount.
        Check(Boosted(config,3,2)==Levels{11,10,10,10} && Boosted(config,10,10)==Levels{11,10,10,10},"a low mount was not lifted to 11/10");
        Config mild=config;mild.speed_level=8;mild.acceleration_level=6;
        Check(Boosted(mild,9,7)==Levels{9,7,9,7} && Boosted(mild,6,5)==Levels{8,6,8,6},"a stimulant slowed a horse, or failed to lift one");
        Check(Boosted(config,0,0)==Levels{0,0,0,0} && Boosted(config,7,0)==Levels{7,0,7,0},"an unreported mount was boosted");
        // The force of a level on the curve; level 11 continues the curve's last step.
        const std::array<float,5> forces{1.70F,3.18F,4.77F,6.44F,8.22F};
        Check(BurstForce(forces,6)==1.70F && BurstForce(forces,10)==8.22F,"a level's own force");
        Check(std::abs(BurstForce(forces,11)-(8.22F+(8.22F-6.44F)))<1e-4F,"level 11 does not continue the curve's last step");
        Check(BurstForce(forces,5)==0 && BurstForce(forces,1)==0,"below the curve there is no force");
        Config bad=config;bad.bonding_factor[2]=-1;Check(!bad.Valid(),"invalid config accepted");
        Config high=config;high.speed_level=12;Check(!high.Valid(),"a speed level past 11 accepted");
        Config accel=config;accel.acceleration_level=11;Check(!accel.Valid(),"an acceleration level past 10 accepted");
        Tracker u;Check(u.Update(1,Tier::Regular,1,bad)==Tier::None && u.Update(2,Tier::Special,1,bad)==Tier::None,"a burst ran on an invalid config");
        // The sixth core: the whole ring is the longest burst; the centre on the orbit, in pixels.
        const float factors[5]{1,1.5F,2,2.5F,3};
        Check(std::abs(horse_races::stimulant_hud::Whole(20,factors,5)-60)<1e-9,"the whole ring is not the longest burst");
        horse_races::stimulant_hud::Config hud;Check(hud.Valid(),"default core settings invalid");
        hud.ring_thickness=0;Check(!hud.Valid(),"a ring without thickness accepted");
        hud=horse_races::stimulant_hud::Config{};hud.radar_x=.1F;hud.radar_y=.8F;hud.radar_radius=.1F;hud.orbit=2;hud.angle_degrees=0;
        auto at=horse_races::stimulant_hud::Centre(hud,2000,1000);Check(std::abs(at.x-400)<1e-3 && std::abs(at.y-800)<1e-3,"orbit to the right");
        hud.angle_degrees=-90;at=horse_races::stimulant_hud::Centre(hud,2000,1000);Check(std::abs(at.x-200)<1e-3 && std::abs(at.y-600)<1e-3,"orbit straight up");
        // The disc's rows.
        float half=0;
        Check(horse_races::stimulant_hud::DiscRow(0,10,half) && std::abs(half-10)<1e-4 && horse_races::stimulant_hud::DiscRow(-6,10,half) &&
            std::abs(half-8)<1e-4 && !horse_races::stimulant_hud::DiscRow(10,10,half),"disc rows");
        // The ring's rows, an annulus from 20 to 30 pixels: full, every piece lit; empty, none.
        using horse_races::stimulant_hud::RingRow;
        horse_races::stimulant_hud::Span s[4];
        int n=RingRow(-10,20,30,1,s);
        Check(n==2 && s[0].lit && s[1].lit && std::abs(s[0].x0-17.32F)<.01F && std::abs(s[0].x1-28.28F)<.01F &&
            std::abs(s[1].x0+28.28F)<.01F && std::abs(s[1].x1+17.32F)<.01F,"full ring row");
        n=RingRow(-10,20,30,0,s);Check(n==2 && !s[0].lit && !s[1].lit,"empty ring row");
        // The arc from twelve, clockwise: three quarters reaches nine o'clock - above the centre the
        // right side is lit and the left is not, below the centre both are; a half reaches six; a
        // quarter reaches three; as the burst drains the end comes back from nine through six and three.
        n=RingRow(-10,20,30,.75F,s);Check(n==2 && s[0].lit && !s[1].lit,"three quarters above the centre");
        n=RingRow(10,20,30,.75F,s);Check(n==2 && s[0].lit && s[1].lit,"three quarters below the centre");
        n=RingRow(10,20,30,.5F,s);Check(n==2 && s[0].lit && !s[1].lit,"a half below the centre");
        n=RingRow(-10,20,30,.25F,s);Check(n==2 && s[0].lit && !s[1].lit,"a quarter above the centre");
        n=RingRow(10,20,30,.25F,s);Check(n==2 && !s[0].lit && !s[1].lit,"a quarter below the centre");
        n=RingRow(0,20,30,.25F,s);Check(n==2 && s[0].lit && !s[1].lit,"the row through the centre");
        // An eighth ends at 45 degrees: on the row 20 pixels above the centre the right side splits at x=20, lit inside.
        n=RingRow(-20,20,30,.125F,s);
        Check(n==3 && s[0].lit && std::abs(s[0].x0)<1e-4F && std::abs(s[0].x1-20)<.01F && !s[1].lit && std::abs(s[1].x1-22.36F)<.01F && !s[2].lit,
            "the arc's end at 45 degrees");
        Check(RingRow(30,20,30,1,s)==0 && RingRow(-31,20,30,1,s)==0,"rows past the ring");
        const Scope scope{42,11,22};
        const auto injecting=[](const char* item,std::uint32_t started,int state=5,int mount=22) {
            return Interaction{true,state,mount,ItemHash(item),started,state>=6};
        };
        Check(ItemTier(747460826U)==Tier::Special && ItemTier(ItemHash("consumable_horse_stimulant_used"))==Tier::Regular,
            "native crafted/used item keys");
        // Fling's unchanged 99/99 stacks and an already gold core do not enter
        // the consumption API at all. Each actual completion still counts.
        DoseDetector d;Check(d.Update(60,scope,100,{})==Tier::None,"stat first read is a dose");
        const auto potent=injecting("consumable_potent_horse_stimulant",60000);
        const auto special=injecting("consumable_special_horse_stimulant_crafted",62000);
        Check(d.Update(60.1,scope,100,potent)==Tier::None,"starting injection is consumption");
        Check(d.Update(60.2,scope,101,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::Potent,
            "unchanged stock/already gold potent dose missed");
        Check(d.Update(60.3,scope,101,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,
            "same completion counted twice");
        Check(d.Update(62,scope,101,special)==Tier::None &&
            d.Update(62.1,scope,102,injecting("consumable_special_horse_stimulant_crafted",62000,6))==Tier::Special,
            "second infinite-items dose or its actual special tier missed");
        // A completed action without the stat is not enough; an increment
        // alone, inventory removal, selecting a wheel item, and cancellation
        // cannot create a dose. Nor can other tonics or another horse.
        d.Reset();d.Update(70,scope,200,{});
        auto selection=injecting("consumable_potent_horse_stimulant",70000,3);
        Check(d.Update(70.1,scope,200,selection)==Tier::None,"wheel selection is a dose");
        Check(d.Update(70.2,scope,200,potent)==Tier::None && d.Update(70.3,scope,200,{})==Tier::None,"cancelled action is a dose");
        Check(d.Update(70.4,scope,201,{})==Tier::None,"unattributed stat change is a dose");
        Check(d.Update(70.5,scope,201,injecting("consumable_potent_horse_stimulant",70500))==Tier::None &&
            d.Update(70.6,scope,201,injecting("consumable_potent_horse_stimulant",70500,6))==Tier::None,
            "older stat increase paid for a later item");
        Check(d.Update(70.7,scope,202,injecting("consumable_potent_horse_medicine",70700,6))==Tier::None,
            "horse medicine is a stimulant");
        Check(d.Update(70.8,scope,203,injecting("consumable_potent_horse_stimulant",70800,6,33))==Tier::None,
            "another horse's dose boosted this mount");
        // Sampling may see the stat before or after the script's completion.
        // The pending completion retains its item when the next action starts.
        d.Reset();d.Update(80,scope,300,{});d.Update(80.1,scope,300,potent);
        Check(d.Update(80.2,scope,301,potent)==Tier::None &&
            d.Update(80.3,scope,301,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::Potent,
            "stat-before-marker ordering missed");
        d.Update(81,scope,301,special);
        Check(d.Update(81.1,scope,301,injecting("consumable_special_horse_stimulant_crafted",62000,6))==Tier::None,
            "completion without increment counted");
        const auto regular=injecting("consumable_horse_stimulant",81500);
        Check(d.Update(81.2,scope,301,regular)==Tier::None && d.Update(81.3,scope,302,regular)==Tier::Special,
            "next item stole pending special completion's tier");
        Check(d.Update(81.4,scope,302,injecting("consumable_horse_stimulant",81500,6))==Tier::None,
            "previous dose's delayed stat credited again");
        Check(d.Update(81.5,scope,303,{})==Tier::Regular,"next dose's own late increment missed");
        // Finite confirmation windows do not revive abandoned actions.
        d.Reset();d.Update(90,scope,400,{});d.Update(90.1,scope,400,potent);
        d.Update(90.2,scope,400,injecting("consumable_potent_horse_stimulant",60000,6));
        Check(d.Update(91.3,scope,401,{})==Tier::None,"expired completion revived");
        // Baselines and action credits cannot cross a race, player, mount,
        // unreadable-stat gap, clock reset or a save's changed stat total.
        for(const Scope other:{Scope{43,11,22},Scope{42,12,22},Scope{42,11,23}}) {
            d.Reset();d.Update(100,scope,500,{});d.Update(100.1,scope,500,potent);
            Check(d.Update(100.2,other,501,injecting("consumable_potent_horse_stimulant",60000,6,other.mount))==Tier::None,
                "completion crossed a scope boundary");
        }
        d.Reset();d.Update(110,scope,600,{});d.Update(110.1,scope,600,potent);
        Check(d.Update(110.2,scope,100,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None &&
            d.Update(110.3,scope,101,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,"save rollback replayed a dose");
        Check(d.Update(110.4,scope,700,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,"save forward jump is a dose");
        Check(d.Update(110.5,scope,-1,{})==Tier::None &&
            d.Update(110.6,scope,701,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,"unavailable stat caught up as a dose");
        Check(d.Update(1,scope,702,injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,"clock rollback replayed a dose");
        Check(d.Update(std::numeric_limits<double>::quiet_NaN(),scope,703,potent)==Tier::None,"invalid clock accepted");
        d.Update(120,scope,std::numeric_limits<int>::max(),{});
        Check(d.Update(120.1,scope,std::numeric_limits<int>::max(),injecting("consumable_potent_horse_stimulant",60000,6))==Tier::None,
            "stat maximum overflowed into a dose");
        // Existing timings, restart instead of additive stacking, and mount
        // ownership hold with the newly confirmed dose source.
        t.Reset();t.Bind(scope);t.Update(130,Tier::Special,5,config);
        Check(!t.Bind({42,11,0}) && t.Active(131) && t.Remaining(131)==59,"short dismount changed burst length");
        t.Update(132,Tier::Potent,5,config);
        Check(t.Seconds()==30 && t.Remaining(133)==29,"dose stacked duration instead of restarting");
        Check(t.Bind({42,11,23}) && !t.Active(134),"new mount inherited old mount's burst");
        t.Update(135,Tier::Regular,1,config);
        Check(t.Bind({43,11,23}) && !t.Active(136),"new race inherited old burst");
        std::cout<<"PASS stimulant: confirmed consumption, infinite items, deduplication, attribution, scope, timings, force, and gauge\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
