#pragma once

#include "race_bookmaking.h"
#include "race_segmented_circuit.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace horse_races::bookmaking {
// A race watched from a balcony has no player progress. Each jockey owns the
// same ordered, recorded circuit and can finish or be eliminated independently.
// The complete course is copied once; positions are samples from live horses.
class Field {
public:
    enum class Status {Racing,Finished,Out};
    struct Rider {
        circuit::Progress progress;
        Status status=Status::Racing;
        double finish_at=-1;
        double out_at=-1;
        int place=0;
        double last_progress_at=0;
        float last_progress_metres=0;
        bool stalled=false;
        bool navigation_timeout=false;
        bool survival_win=false;
    };
    // Snapshot before updating any entrant: loop order must not give one
    // jockey a fresher reference. OUT/invalid riders can never be the anchor.
    // A finisher remains at the finish line, not at their departure position.
    static std::optional<float> LeaderMetres(std::span<const Rider> riders,const circuit::Layout& layout) {
        std::optional<float> leader;
        for(const auto& r:riders) {
            if(r.status==Status::Out || r.progress.total_laps<=0 || r.progress.off_course || r.progress.continuity_lost)continue;
            const float metres=r.progress.Metres(layout);
            if(std::isfinite(metres) && (!leader || metres>*leader))leader=metres;
        }
        return leader;
    }
    static bool HasOtherRacer(std::span<const Rider> riders,std::size_t self) {
        for(std::size_t i=0;i<riders.size();++i)if(i!=self&&riders[i].status==Status::Racing)return true;
        return false;
    }
    static std::optional<float> OtherLeaderMetres(std::span<const Rider> riders,const circuit::Layout& layout,std::size_t self) {
        std::optional<float> leader;
        for(std::size_t i=0;i<riders.size();++i)if(i!=self) {
            const auto& r=riders[i];
            if(r.status!=Status::Racing||r.progress.total_laps<=0||r.progress.off_course||r.progress.continuity_lost)continue;
            const float metres=r.progress.Metres(layout);
            if(std::isfinite(metres)&&(!leader||metres>*leader))leader=metres;
        }
        return leader;
    }
private:
    lap::Plan plan_;
    std::vector<Rider> riders_;
    double previous_at_=0;
    bool running_=false;
    void RankFinishers() {
        std::vector<std::size_t> finishers;
        for(std::size_t i=0;i<riders_.size();++i)
            if(riders_[i].status==Status::Finished)finishers.push_back(i);
        std::sort(finishers.begin(),finishers.end(),[&](std::size_t a,std::size_t b){
            return riders_[a].finish_at==riders_[b].finish_at?a<b:
                riders_[a].finish_at<riders_[b].finish_at;
        });
        for(std::size_t order=0;order<finishers.size();++order) {
            const auto index=finishers[order];
            riders_[index].place=order && std::abs(riders_[index].finish_at-
                riders_[finishers[order-1]].finish_at)<.001?
                riders_[finishers[order-1]].place:static_cast<int>(order+1);
        }
    }
public:
    void Reset(){*this={};}
    bool Running() const {return running_;}
    const lap::Plan& Course() const {return plan_;}
    std::span<const Rider> Riders() const {return riders_;}
    bool Start(const lap::Plan& plan,std::span<const Vector3> positions,double now) {
        if(running_ || !plan.course.Ready() || positions.size()<2 ||
            positions.size()>kExhibitionRacers ||
            !std::isfinite(now) || now<0)return false;
        std::vector<Rider> starting(positions.size());
        for(std::size_t i=0;i<positions.size();++i)
            if(!starting[i].progress.Start(plan.course,1,positions[i],now,2))return false;
            else {starting[i].last_progress_at=now;
                starting[i].last_progress_metres=starting[i].progress.Metres(plan.course);}
        plan_=plan;riders_=std::move(starting);previous_at_=now;running_=true;return true;
    }
    // `alive` is the runtime's combined rider+horse+mounted check. Falling,
    // dying or straying out of the strict corridor removes that jockey from
    // contention; a persistent navigation failure has its own bounded DNF.
    // Nobody is silently teleported back into the race.
    bool Update(std::span<const Vector3> positions,std::span<const bool> alive,double now) {
        if(!running_ || positions.size()!=riders_.size() || alive.size()!=riders_.size() ||
            !std::isfinite(now) || now<previous_at_)return false;
        const double dt=now-previous_at_;
        for(std::size_t i=0;i<riders_.size();++i) {
            auto& rider=riders_[i];
            if(rider.status!=Status::Racing)continue;
            if(!alive[i]){rider.status=Status::Out;rider.out_at=now;continue;}
            const auto advance=rider.progress.Update(plan_.course,positions[i],now);
            if(advance.finish_fraction) {
                rider.status=Status::Finished;
                rider.finish_at=previous_at_+dt*std::clamp(*advance.finish_fraction,0.0F,1.0F);
            } else if(rider.progress.off_course){rider.status=Status::Out;rider.out_at=now;}
            else {
                constexpr float kProgressMetres=5.0F;
                // A back-row horse can take ~30 seconds to clear the native
                // launch task even while its rider/horse remain valid. Once
                // travelling, the ordinary 25-second stuck check applies.
                constexpr double kStartupStallSeconds=45.0;
                constexpr double kMovingStallSeconds=25.0;
                const float metres=rider.progress.Metres(plan_.course);
                if(metres>=rider.last_progress_metres+kProgressMetres){
                    rider.last_progress_metres=metres;rider.last_progress_at=now;rider.stalled=false;
                }else rider.stalled=now-rider.last_progress_at>=
                    (rider.last_progress_metres<kProgressMetres?kStartupStallSeconds:kMovingStallSeconds);
                // Task renewal is not progress. Give normal recovery time,
                // then resolve a persistent failure instead of waiting 30 min.
                // This high-water anchor cannot be reset by rocking backwards
                // and forwards or by a successfully reissued native task.
                constexpr double kStartupFailureSeconds=60.0;
                constexpr double kMovingFailureSeconds=45.0;
                if(now-rider.last_progress_at>=
                    (rider.last_progress_metres<kProgressMetres?kStartupFailureSeconds:kMovingFailureSeconds)){
                    rider.navigation_timeout=true;rider.status=Status::Out;rider.out_at=now;
                }
            }
        }
        // Judge the complete frame before choosing a survivor: processing
        // riders in array order must not turn a simultaneous all-out into a
        // win for whichever jockey happens to occupy the last grid slot.
        // A technical failure cannot hand an unfinished competitor a free
        // survival victory. Any remaining rider must complete the course.
        const bool technical_failure=std::any_of(riders_.begin(),riders_.end(),
            [](const Rider& r){return r.navigation_timeout;});
        if(!technical_failure && std::count_if(riders_.begin(),riders_.end(),[](const Rider& r){
            return r.status!=Status::Out;
        })==1) {
            for(auto& rider:riders_)if(rider.status==Status::Racing) {
                rider.status=Status::Finished;rider.finish_at=now;rider.survival_win=true;
            }
        }
        previous_at_=now;RankFinishers();
        running_=std::any_of(riders_.begin(),riders_.end(),[](const Rider& r){return r.status==Status::Racing;});
        return true;
    }
    int Place(std::size_t index) const {return index<riders_.size()?riders_[index].place:0;}
    int DisplayLap(std::size_t index) const {
        if(index>=riders_.size() || riders_[index].status==Status::Out)return 0;
        return riders_[index].status==Status::Finished?plan_.Total():plan_.Current(riders_[index].progress)+1;
    }
    std::vector<std::size_t> VisibleOrder() const {
        std::vector<std::size_t> order;
        for(std::size_t i=0;i<riders_.size();++i)
            if(riders_[i].status!=Status::Out)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[this](std::size_t a,std::size_t b){
            const auto& x=riders_[a],&y=riders_[b];
            if(x.status!=y.status)return x.status==Status::Finished;
            if(x.status==Status::Finished)return x.place<y.place;
            return x.progress.Metres(plan_.course)>y.progress.Metres(plan_.course);
        });
        return order;
    }
    // Include eliminated riders after contenders. A rider eliminated earlier
    // finishes below one eliminated later; neither can regain a prize place.
    std::vector<std::size_t> StandingsOrder() const {
        auto order=VisibleOrder();
        std::vector<std::size_t> out;
        for(std::size_t i=0;i<riders_.size();++i)
            if(riders_[i].status==Status::Out)out.push_back(i);
        std::stable_sort(out.begin(),out.end(),[this](std::size_t a,std::size_t b){
            return riders_[a].out_at>riders_[b].out_at;
        });
        order.insert(order.end(),out.begin(),out.end());
        return order;
    }
    std::size_t ActiveCount() const {
        return std::count_if(riders_.begin(),riders_.end(),[](const Rider& r){return r.status!=Status::Out;});
    }
};
} // namespace horse_races::bookmaking
