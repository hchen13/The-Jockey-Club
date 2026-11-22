#pragma once
#include "horse_ledger_binding.h"
#include "horse_race_ledger.h"
#include <map>
#include <memory>

namespace horse_races::save_observer::archive_capture {
// Pure state machine. The owner serializes ALL methods with the observer's
// sample mutex. No game natives, mutable Ledger access, disk I/O or callbacks.
// A capture is data, NOT proof of a completed native write. Publication also
// requires matching CompletedWriteEvidence, its live fence, and Store CAS.
class Tracker {
public:
    static constexpr std::size_t MaxPacks=8;
    struct Capture {
        std::shared_ptr<const horse_ledger::Frozen> frozen;
        std::uint64_t request_tick_ms{}, request_sequence{};
        std::uint32_t pack_counter{};
    };
private:
    horse_ledger::binding::Detector detector_;
    std::optional<Observation> latest_;
    std::shared_ptr<const horse_ledger::Frozen> mirror_;
    std::uint64_t mutation_tick_{}, mirror_sequence_{}, invalidation_sequence_{};
    std::uint64_t largest_epoch_{}, largest_mutation_{}, forbidden_epoch_{};
    bool have_epoch_{}, forbid_epoch_{};
    std::map<std::uint32_t,Capture> packs_;
    void Invalidate(std::uint64_t sequence,bool world){
        mirror_.reset();packs_.clear();invalidation_sequence_=sequence;
        if(world&&have_epoch_){forbidden_epoch_=largest_epoch_;forbid_epoch_=true;}
    }
public:
    // Call Capture() under the same mutex BEFORE Offer. expected_sequence is
    // runtime's LAST PROCESSED observation, never the newer Capture result.
    // Call Revoke before every possible world/economic/ledger mutation, and
    // republish only once the operation is complete and the coordinator is bound.
    bool Offer(std::shared_ptr<const horse_ledger::Frozen> frozen,
               std::uint64_t expected_sequence,std::uint64_t mutation_tick){
        if(!latest_||!latest_->healthy||!expected_sequence||
           latest_->sequence!=expected_sequence||expected_sequence<invalidation_sequence_||
           latest_->raw.pending||latest_->raw.load_state||latest_->raw.save_state||latest_->raw.disk_state||
           (latest_->raw.operation!=0&&latest_->raw.operation!=2)||
           !frozen||!frozen->capture||!frozen->parent.Valid()||mutation_tick>latest_->tick_ms||
           (forbid_epoch_&&frozen->epoch<=forbidden_epoch_)||
           (have_epoch_&&(frozen->epoch<largest_epoch_||
              (frozen->epoch==largest_epoch_&&frozen->mutation<largest_mutation_))))return false;
        if(have_epoch_&&frozen->epoch!=largest_epoch_)packs_.clear();
        largest_epoch_=frozen->epoch;largest_mutation_=frozen->mutation;have_epoch_=true;
        mirror_=std::move(frozen);mutation_tick_=mutation_tick;mirror_sequence_=expected_sequence;
        return true;
    }
    // Existing freezes precede this mutation and must retain their exact data.
    void Revoke(){mirror_.reset();}
    void Observe(const Observation& observation){
        const auto actions=detector_.Consume(observation);
        using horse_ledger::binding::Kind;
        for(const auto& action:actions){
            switch(action.kind){
            case Kind::LoadBegin:case Kind::LoadFailed:case Kind::Uncertain:
                Invalidate(action.sequence,true);break;
            case Kind::SaveCaptureUncertain:
                Invalidate(action.sequence,false);break;
            case Kind::WriteRejected:
                packs_.erase(action.pack_counter);break;
            case Kind::PackRequested:
                if(!mirror_||!observation.healthy||observation.raw.pending||observation.raw.load_state||
                   (observation.raw.operation!=0&&observation.raw.operation!=2&&
                    observation.raw.operation!=6&&observation.raw.operation!=7)||
                   mutation_tick_>=action.tick_ms||mirror_sequence_>=action.sequence)break;
                if(packs_.size()>=MaxPacks&&!packs_.contains(action.pack_counter))packs_.clear();
                packs_.insert_or_assign(action.pack_counter,Capture{mirror_,action.tick_ms,action.sequence,action.pack_counter});
                break;
            default:break;
            }
        }
        latest_=observation;
    }
    std::optional<Capture> Find(std::uint32_t counter)const{
        const auto it=packs_.find(counter);return it==packs_.end()?std::nullopt:std::optional(it->second);
    }
    bool Valid(const Capture& capture)const{
        const auto current=Find(capture.pack_counter);
        return current&&current->frozen==capture.frozen&&
            current->request_sequence==capture.request_sequence&&
            current->request_tick_ms==capture.request_tick_ms;
    }
    void Release(std::uint32_t counter){packs_.erase(counter);}
    bool HasMirror()const{return static_cast<bool>(mirror_);}
    std::size_t PackCount()const{return packs_.size();}
    std::uint64_t LatestSequence()const{return latest_?latest_->sequence:0;}
};
}

