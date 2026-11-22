#pragma once
#include "horse_save_observer.h"
#include "runtime_paths.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <array>
#include <optional>
#include <algorithm>
#include <utility>
#include <deque>
#include <mutex>

namespace horse_races::save_startup_probe {
// Queue pins this ASI before creating one background bootstrap. No native
// invocation, file I/O, code scan, mutex wait or hash occurs in DllMain.
// Configuration is read by the app-supplied reader only on the worker.
using ProfileReader=std::wstring (*)(const wchar_t* ini);
inline std::wstring ConfiguredProfile(const std::filesystem::path& game_root,ProfileReader reader) {
    // Exactly the same pinned INI as ScriptMain, including an ASI-only update.
    const auto ini=runtime_paths::Resolve(game_root).MainConfig().wstring();
    return reader?reader(ini.c_str()):std::wstring{};
}
void Queue(void* module,ProfileReader read_profile=nullptr) noexcept;
void ScriptMainArrived() noexcept;
void RequestStop() noexcept;
struct Sample {
    save_observer::Raw raw;
    std::uint32_t load_stage{},load_kind{},load_error{};
    bool operator==(const Sample&)const=default;
};
struct FileEvidence {std::string sha;std::uint64_t start{},end{};};
struct ColdLoadProof {
    std::string profile,sha;
    unsigned slot{};
    std::uint64_t request{},terminal{},before_end{},after_start{},after_end{},max_gap{};
};
enum class ProofState { NotReady, Rejected, Ready };
// In-process one-shot capability only; diagnostics on disk are never read back.
std::optional<ColdLoadProof> TakeProof(const std::string& profile,
    const save_observer::Observation& current,std::uint64_t now,std::string& reason,ProofState* state=nullptr);
bool ConfirmProof(const ColdLoadProof&, const save_observer::Observation&, std::uint64_t now);
class ProofBuilder {
    std::array<std::optional<FileEvidence>,16> before_;
    std::optional<Sample> previous_;
    std::optional<ColdLoadProof> proof_;
    std::string profile_,reason_;
    std::uint64_t last_tick_=0,max_gap_=0,request_=0,terminal_=0,prehash_after_=0;
    std::uint32_t error_=0,stage_=0,stages_=0;
    unsigned slot_=0;
    bool consumed_=false;
    static std::optional<unsigned> Slot(const Sample& s){
        const auto& f=s.raw.filename;if(f[0]!='S'||f[1]!='R'||f[2]!='D'||f[3]!='R'||f[4]!='3'||f[9]!=0)return {};
        unsigned n=0;for(int i=5;i<9;++i){if(f[i]<'0'||f[i]>'9')return {};n=n*10+static_cast<unsigned>(f[i]-'0');}
        return n<16?std::optional<unsigned>(n):std::nullopt;
    }
public:
    static constexpr std::uint64_t MaxGapMs=250;
    std::uint64_t PrehashAfter()const{return prehash_after_;}
    std::optional<std::pair<unsigned,std::uint64_t>> PendingPosthash()const{
        if(!reason_.empty()||consumed_||proof_||!terminal_)return {};
        return std::pair{slot_,terminal_};
    }
    // An idle-menu interruption can be followed by a new witnessed load. Forget
    // all earlier evidence; never bridge a gap inside a load or its handoff.
    void Interrupted(std::uint64_t tick,const char* reason){
        if(request_||consumed_){Reject(reason);return;}
        before_={};previous_.reset();max_gap_=0;prehash_after_=tick;
    }
    std::string Diagnostic()const {
        const char* phase=!reason_.empty()?"rejected":consumed_?"consumed":proof_?"ready":
            terminal_?"await_posthash":request_?"loading":previous_?"await_request":"await_baseline";
        return std::string("phase=")+phase+" stage="+std::to_string(stage_)+
            " observed_stage_mask="+std::to_string(stages_)+
            " source_slot="+(request_?std::to_string(slot_):"unknown")+
            " reason="+(reason_.empty()?"none":reason_);
    }
    void Reject(const char* reason){if(reason_.empty())reason_=reason;proof_.reset();}
    void Profile(std::string profile){if(!profile_.empty()&&profile_!=profile)Reject("profile changed");else profile_=std::move(profile);}
    void Hash(unsigned slot,const FileEvidence& hash,bool preload){
        if(consumed_||!reason_.empty()||slot>15)return;
        if(hash.sha.size()!=64||hash.start>hash.end){Reject("invalid file evidence");return;}
        if(preload){if(!request_&&hash.start>=prehash_after_)before_[slot]=hash;return;}
        if(terminal_&&slot==slot_&&hash.start>=terminal_){
            if(!before_[slot]||hash.sha!=before_[slot]->sha){Reject("source bytes changed across load");return;}
            proof_=ColdLoadProof{profile_,hash.sha,slot_,request_,terminal_,before_[slot]->end,hash.start,hash.end,max_gap_};
        }
    }
    void Observe(const Sample& s,std::uint64_t tick){
        if(!reason_.empty())return;
        if(previous_){
            if(tick<last_tick_){Reject("startup clock reversed");return;}
            if(tick-last_tick_>MaxGapMs){
                Interrupted(tick,"startup sample gap during load/handoff");
                if(!reason_.empty())return;
            }else max_gap_=std::max(max_gap_,tick-last_tick_);
        }
        if(!previous_){
            if(s.load_stage||s.raw.operation||s.raw.pending||s.raw.load_state||s.raw.pack_counter||s.raw.disk_counter){Reject("startup baseline was not idle");return;}
            error_=s.load_error;
        }
        last_tick_=tick;
        if(s.load_kind!=2||s.load_error!=6||s.load_error!=error_||s.raw.pending||s.raw.load_state||s.raw.pack_state||
            s.raw.pack_counter||s.raw.disk_counter||s.raw.disk_state||s.raw.save_state||s.raw.auto_state==1){Reject("startup load changed or write overlapped");return;}
        if(terminal_){if(s.raw!=previous_->raw||s.load_stage){Reject("activity after startup completion");return;}previous_=s;return;}
        if(!request_){
            if(s.raw.operation==3){
                const auto slot=Slot(s);
                if(!previous_){Reject("request missing idle baseline");return;}
                // Stage 1 is a real request state; a 20ms poll may first see 1
                // (.62) or 2 (.61). Both still require the observed success gate at stage 7.
                if(s.load_stage!=1&&s.load_stage!=2){Reject("request first stage is not 1 or 2");return;}
                if(!slot){Reject("request source filename invalid");return;}
                if(profile_.empty()){Reject("request profile not ready");return;}
                if(!before_[*slot]){Reject("request source prehash missing");return;}
                if(before_[*slot]->end>=tick){Reject("request source prehash was not before request");return;}
                slot_=*slot;request_=tick;stage_=s.load_stage;stages_=1u<<stage_;
            }else if(s.load_stage|| (s.raw.operation!=0&&s.raw.operation!=18)){Reject("unexpected startup operation");return;}
        }else{
            if(Slot(s)!=std::optional<unsigned>(slot_)){Reject("source slot changed");return;}
            if(s.load_stage==0&&s.raw.operation==0){
                // The guarded dispatcher reaches stage 7 (BCA769) only on
                // successful completion. Intermediate stages 2..6 can pass
                // between polls; their absence is not evidence of failure.
                // Require the witnessed request, monotonic loading and this
                // success gate, in addition to the source/hash/error fences.
                if(stage_!=7){Reject("startup ended without observed success stage 7");return;}
                terminal_=tick;
            }else{
                if(s.raw.operation!=3||s.load_stage<stage_||s.load_stage>7){Reject("failed or regressed startup stage");return;}
                if(s.load_stage>=4&&!s.raw.packed_bytes){Reject("load bytes missing");return;}
                stage_=s.load_stage;stages_|=1u<<stage_;
            }
        }
        previous_=s;
    }
    bool Confirm(const ColdLoadProof& proof,const save_observer::Observation& current,std::uint64_t now)const {
        return reason_.empty()&&proof_&&previous_&&proof.request==proof_->request&&
            proof.sha==proof_->sha&&proof.profile==profile_&&current.healthy&&current.raw==previous_->raw&&
            current.tick_ms>=terminal_&&now>=last_tick_&&now-last_tick_<=MaxGapMs&&
            now>=current.tick_ms&&now-current.tick_ms<=MaxGapMs;
    }
    std::optional<ColdLoadProof> Take(const std::string& profile,const save_observer::Observation& current,
        std::uint64_t now,std::string& reason,ProofState* state=nullptr){
        if(state)*state=ProofState::Rejected;
        if(consumed_){reason="startup proof already consumed";return {};}
        if(!reason_.empty()){reason=reason_;return {};}
        if((!profile_.empty()&&profile!=profile_)||(current.sequence&&!current.healthy)){
            Reject("startup profile or observer failed");reason=reason_;return {};
        }
        // The runtime observer is now in the world. If no startup request was
        // witnessed, waiting longer cannot reconstruct that past load.
        if(current.sequence&&previous_&&!request_&&current.raw.operation==0&&current.raw==previous_->raw){
            Reject("startup load request was not witnessed");reason=reason_;return {};
        }
        if(!proof_||!previous_||!current.sequence){
            if(state)*state=ProofState::NotReady;reason=Diagnostic();return {};
        }
        if(profile!=profile_||!current.healthy||current.raw!=previous_->raw||
            current.tick_ms<terminal_||now<last_tick_||now-last_tick_>MaxGapMs||
            now<current.tick_ms||now-current.tick_ms>MaxGapMs){reason="no fresh exact startup proof";return {};}
        consumed_=true;if(state)*state=ProofState::Ready;
        proof_->max_gap=max_gap_;reason.clear();return proof_;
    }
};
// Identical to the manual observer's conservative paired-read policy, but has
// no shared sequence, queue, state or failure effect on that observer.
template<class Reader> bool StableRead(Reader&& reader,Sample& result){
    for(int attempt=0;attempt<3;++attempt){const auto first=reader(),second=reader();
        if(first==second){result=second;return true;}}
    return false;
}
struct Limits {
    // Diagnostic limits only. The runtime explicitly releases startup discovery
    // after adoption, rejection or a superseding operation; there is no clock
    // deadline for title menus or pending file evidence.
    static constexpr std::size_t MaxLines=4096,MaxBytes=4*1024*1024;
    std::size_t lines=0,bytes=0;
    bool Admit(std::size_t count){
        if(lines>=MaxLines || count>MaxBytes-bytes)return false;
        ++lines;bytes+=count;return true;
    }
};
// Optional output never runs disk I/O on the sampling worker or holds its lock
// while a writer runs. A busy/full queue drops diagnostics, not source evidence.
class DiagnosticQueue {
    std::mutex mutex_;
    Limits budget_;
    std::deque<std::string> lines_;
public:
    bool Push(std::string line){
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||!budget_.Admit(line.size()+2))return false;
        lines_.push_back(std::move(line));return true;
    }
    std::deque<std::string> Drain(){
        std::deque<std::string> lines;
        {std::lock_guard lock(mutex_);lines.swap(lines_);}
        return lines;
    }
};
}
