#pragma once
#include "horse_ledger_binding.h"
#include <array>
#include <deque>
#include <map>
#include <algorithm>
#include <cstdio>

namespace horse_races::save_observer::write_evidence {
// Serialized by the observer sampling mutex. Disk I/O happens outside this
// object; Begin/Complete fence the entire asynchronous double-hash interval.
class Tracker {
public:
    static constexpr std::size_t MaxPending=32,MaxCompleted=128;
    struct Job {
        horse_ledger::binding::Action action;
        std::uint64_t fence{},epoch{},target_request{};
    };
private:
    horse_ledger::binding::Detector detector_;
    std::optional<Observation> latest_;
    std::optional<unsigned> write_target_;
    std::uint64_t fence_{},epoch_{};
    std::array<std::uint64_t,16> target_request_{};
    std::map<std::uint64_t,Job> pending_;
    std::deque<std::uint64_t> queue_;
    struct Proven {CompletedWriteEvidence evidence;std::uint64_t epoch{},target_request{};};
    std::map<std::uint64_t,Proven> completed_;
    static bool IdleFor(const Observation& observation,const horse_ledger::binding::Action& action){
        const auto& r=observation.raw;
        const auto end=std::find(r.filename.begin(),r.filename.end(),'\0');
        char expected[16]{};std::snprintf(expected,sizeof expected,"SRDR3%04u",action.slot);
        return observation.healthy&&r.pending==0&&r.load_state==0&&r.operation==0&&
            r.save_state==0&&r.disk_state==0&&r.disk_counter==action.disk_counter&&
            r.save_slot==action.slot&&std::string(r.filename.begin(),end)==expected;
    }
public:
    void Observe(const Observation& observation){
        const auto& r=observation.raw;
        // Packing alone does not write the game file. Any load/write/unknown
        // operation during hashing, including an activity that later returns
        // to the same idle fields, rejects that hash job.
        if(!observation.healthy||r.pending||r.load_state||r.operation||r.save_state||r.disk_state||
            (latest_&&r.disk_counter!=latest_->raw.disk_counter))++fence_;
        // Even an incomplete/rejected later native write supersedes an older
        // capability for that target. Never rely solely on witnessing phase1.
        const auto native_target=r.operation==7?15u:r.save_slot;
        if((r.operation==6||r.operation==7)&&r.save_state&&native_target<16)
            target_request_[native_target]=observation.sequence;
        using horse_ledger::binding::Kind;
        const auto actions=detector_.Consume(observation);
        for(const auto& action:actions)if(action.kind==Kind::WriteBegin&&action.slot<16)write_target_=action.slot;
        const bool advanced=latest_&&r.disk_counter!=latest_->raw.disk_counter;
        const bool attributed=latest_&&write_target_&&*write_target_==native_target&&observation.healthy&&
            r.pending==0&&r.load_state==0&&(r.operation==6||r.operation==7)&&r.save_state&&
            static_cast<std::uint32_t>(r.disk_counter-latest_->raw.disk_counter)==1;
        // A counter-only idle advance can hide a completely missed overwrite,
        // including one of any prior target. Completed capabilities must not
        // survive it merely because the filename/slot fields stayed unchanged.
        const bool unexplained=advanced&&!attributed;
        if(unexplained)++epoch_;
        for(const auto& action:actions){
            if(action.kind==Kind::Uncertain)++epoch_;
            if((action.kind==Kind::WriteBegin||action.kind==Kind::WriteRejected)&&action.slot<16)
                target_request_[action.slot]=action.sequence;
            if(action.kind==Kind::WriteCandidate&&!unexplained&&action.slot<16&&IdleFor(observation,action)&&
                target_request_[action.slot]&&pending_.size()<MaxPending){
                pending_.emplace(action.sequence,Job{action,fence_,epoch_,target_request_[action.slot]});
                queue_.push_back(action.sequence);
            }
            if(action.kind==Kind::WriteCandidate||action.kind==Kind::WriteRejected||action.kind==Kind::Uncertain||
                action.kind==Kind::SaveCaptureUncertain||action.kind==Kind::LoadBegin)write_target_.reset();
        }
        latest_=observation;
    }
    std::optional<Job> Next(){
        while(!queue_.empty()){
            const auto sequence=queue_.front();queue_.pop_front();
            if(const auto found=pending_.find(sequence);found!=pending_.end())return found->second;
        }
        return {};
    }
    bool Begin(const Job& job)const{
        return pending_.contains(job.action.sequence)&&latest_&&job.fence==fence_&&job.epoch==epoch_&&
            job.target_request==target_request_[job.action.slot]&&IdleFor(*latest_,job.action);
    }
    bool Complete(const Job& job,const std::optional<CompletedWriteEvidence>& evidence){
        const bool accepted=evidence&&Begin(job)&&latest_->tick_ms>=evidence->read_end_tick_ms&&
            evidence->sequence==job.action.sequence&&evidence->slot==job.action.slot&&
            evidence->pack_counter==job.action.pack_counter&&evidence->disk_counter==job.action.disk_counter&&
            evidence->request_tick_ms==job.action.request_tick_ms&&evidence->terminal_tick_ms==job.action.tick_ms&&
            evidence->read_start_tick_ms>=job.action.tick_ms&&evidence->read_end_tick_ms>=evidence->read_start_tick_ms&&
            !evidence->profile_directory.empty()&&evidence->sha256.size()==64&&evidence->size>0&&
            completed_.size()<MaxCompleted;
        pending_.erase(job.action.sequence);
        if(accepted)completed_.emplace(job.action.sequence,Proven{*evidence,job.epoch,job.target_request});
        return accepted;
    }
    bool Valid(const CompletedWriteEvidence& evidence)const{
        const auto found=completed_.find(evidence.sequence);
        return found!=completed_.end()&&found->second.evidence==evidence&&found->second.epoch==epoch_&&
            evidence.slot<16&&found->second.target_request==target_request_[evidence.slot];
    }
    std::optional<CompletedWriteEvidence> Find(std::uint64_t sequence)const{
        const auto found=completed_.find(sequence);
        return found!=completed_.end()&&Valid(found->second.evidence)?std::optional(found->second.evidence):std::nullopt;
    }
    void Release(std::uint64_t sequence){pending_.erase(sequence);completed_.erase(sequence);std::erase(queue_,sequence);}
};
}
