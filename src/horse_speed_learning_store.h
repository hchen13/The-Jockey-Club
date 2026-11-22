#pragma once
#include "horse_speed_learning.h"
#include "horse_speed_context.h"
#include "horse_speed_priors.h"
#include <iomanip>
#include <atomic>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <set>
#include <vector>

namespace horse_motion::speed_learning {
// Reusable world pace depends only on displayed speed and the exact native
// resource/grade. Live entity identity remains in Key for sampling safety.
struct ProfileKey {
    int speed=0;
    ContextKey context{};
    bool operator==(const ProfileKey&) const=default;
    bool operator<(const ProfileKey& other) const {
        return std::tie(speed,context.resource,context.slope,context.material)<std::tie(other.speed,other.context.resource,other.context.slope,other.context.material);
    }
    bool Valid() const {return speed>=1 && speed<=10 && context.Valid();}
};
struct LearnedPace {
    float base=0;
    std::uint64_t samples=0;
    std::uint64_t memory_revision=0; // Runtime only; never serialized.
    bool Valid() const{return std::isfinite(base) && base>=3 && base<=25 && samples>0 && samples<=1000000000;}
};
class Store {
    std::map<ProfileKey,LearnedPace> profiles_;
    struct ProvisionalPair {float pace=0;double at=0;std::uint64_t token=0;};
    struct ProvisionalBucket {std::vector<ProvisionalPair> pairs;};
    // Short pairs are evidence, not saved profiles or control targets. A
    // bounded per-key set lets independent later pairs corroborate them.
    std::map<ProfileKey,ProvisionalBucket> provisional_;
    // Each new store gets a distinct revision, even if recreated at the same
    // address during a load; cached estimates must not cross store resets.
    inline static std::atomic<std::uint64_t> next_revision_{1};
    std::uint64_t epoch_=next_revision_.fetch_add(1,std::memory_order_relaxed);
    std::uint64_t revision_=next_revision_.fetch_add(1,std::memory_order_relaxed);
    struct ConsumptionEvent {std::uint64_t sequence=0,token=0;};
    // Empty stores do not reserve 16 KiB on the script stack. The bounded
    // event ring is allocated once, on the first confirmed short pair.
    std::vector<ConsumptionEvent> consumed_;
    std::uint64_t consumed_sequence_=0;
    bool consumption_unavailable_=false;
public:
    struct ProfileStamp {
        std::uint64_t epoch=0,revision=0;
        bool operator==(const ProfileStamp&) const=default;
    };
    static constexpr std::size_t kMaxProfiles=8192,kMaxBytes=1024*1024;
    static constexpr std::size_t kMaxProvisionalProfiles=512;
    static constexpr std::size_t kConsumptionEvents=1024;
    enum class PairState : unsigned {Unavailable,Stored,Waiting,Disagreed,Promoted,Capacity};
    struct PairResult {PairState state=PairState::Unavailable;float pace=0;unsigned pending=0;
        std::uint64_t submitted_token=0,consumed_token=0;};
    std::size_t Size() const{return profiles_.size();}
    std::uint64_t Revision() const{return revision_;}
    ProfileStamp Stamp(const ProfileKey& key) const {
        const auto it=profiles_.find(key);
        return {epoch_,it==profiles_.end()?0:it->second.memory_revision};
    }
    void ClearProvisional(){provisional_.clear();}
    void DiscardProvisional(const ProfileKey& key){provisional_.erase(key);}
    void DiscardProvisional(const ProfileKey& key,std::uint64_t token){
        if(!token)return;
        const auto it=provisional_.find(key);if(it==provisional_.end())return;
        for(auto& pair:it->second.pairs)if(pair.token==token){pair={};break;}
    }
    std::uint64_t ConsumptionSequence() const{return consumed_sequence_;}
    void RecordConsumed(std::uint64_t token){
        if(!token)return;
        if(consumed_.empty() && !consumption_unavailable_){
            try{consumed_.resize(kConsumptionEvents);}
            catch(...){consumption_unavailable_=true;}
        }
        ++consumed_sequence_;
        if(consumption_unavailable_)return;
        consumed_[(consumed_sequence_-1)%kConsumptionEvents]={consumed_sequence_,token};
    }
    bool ConsumedSince(std::uint64_t since,const std::array<std::uint64_t,4>& tokens) const {
        if(since>consumed_sequence_ || consumed_sequence_-since>kConsumptionEvents)return true;
        if(since==consumed_sequence_)return false;
        if(consumed_.size()!=kConsumptionEvents)return true;
        for(auto seq=since+1;seq<=consumed_sequence_;++seq){
            const auto& event=consumed_[(seq-1)%kConsumptionEvents];
            if(event.sequence!=seq)return true;
            for(auto token:tokens)if(token && event.token==token)return true;
        }
        return false;
    }
    PairResult SubmitProvisional(const ProfileKey& key,float pace,double at) {
        if(!key.Valid() || !std::isfinite(pace) || pace<3 || pace>25 ||
            !std::isfinite(at) || at<=0)return {};
        auto it=provisional_.find(key);
        if(it==provisional_.end()) {
            if(provisional_.size()>=kMaxProvisionalProfiles) {
                // Rare full-table path, only on a completed pair. Never scan
                // or allocate on each frame. Expired contexts release slots.
                for(auto old=provisional_.begin();old!=provisional_.end();) {
                    bool live=false;
                    for(const auto& pair:old->second.pairs)
                        if(pair.at && at>=pair.at && at-pair.at<=1800.){live=true;break;}
                    if(!live)old=provisional_.erase(old);else ++old;
                }
                if(provisional_.size()>=kMaxProvisionalProfiles)return {PairState::Capacity,pace,0};
            }
            it=provisional_.emplace(key,ProvisionalBucket{}).first;
        }
        auto& pairs=it->second.pairs;
        for(auto& pair:pairs)if(pair.at && (at<pair.at || at-pair.at>1800.))pair={};
        int match=-1;float difference=std::numeric_limits<float>::infinity();
        bool waiting=false,disagreed=false;
        for(int i=0;i<static_cast<int>(pairs.size());++i)if(pairs[i].at) {
            if(at-pairs[i].at<3.){waiting=true;continue;}
            const float delta=std::abs(pace-pairs[i].pace);
            if(delta<=.35F && delta<difference){difference=delta;match=i;}
            else if(delta>.35F)disagreed=true;
        }
        if(match>=0) {
            const float corroborated=std::min(pace,pairs[match].pace);
            const auto consumed_token=pairs[match].token;
            pairs[match]={};unsigned pending=0;for(const auto& pair:pairs)pending+=pair.at>0;
            return {PairState::Promoted,corroborated,pending,0,consumed_token};
        }
        ProvisionalPair* target=nullptr;
        for(auto& pair:pairs)if(!pair.at){target=&pair;break;}
        if(!target && pairs.size()<64){pairs.emplace_back();target=&pairs.back();}
        if(!target){target=&pairs.front();for(auto& pair:pairs)if(pair.at<target->at)target=&pair;}
        const auto token=next_revision_.fetch_add(1,std::memory_order_relaxed);
        *target={pace,at,token};unsigned pending=0;for(const auto& pair:pairs)pending+=pair.at>0;
        return {disagreed?PairState::Disagreed:waiting?PairState::Waiting:PairState::Stored,pace,pending,token,0};
    }
    const LearnedPace* Find(const ProfileKey& key) const {
        const auto it=profiles_.find(key);return it==profiles_.end()?nullptr:&it->second;
    }
    // Borrow only the same native resource AND grade. At most ten map probes;
    // callers cache the result until the context or store revision changes.
    float Estimate(const ProfileKey& target,std::uint32_t normal_resource,float target_flat,std::uint64_t minimum_samples=1) const {
        if(!target.Valid() || !normal_resource || !std::isfinite(target_flat) || target_flat<3 || target_flat>25)return 0;
        int best_distance=11;std::uint64_t best_samples=0;float best=0;
        for(int rank=1;rank<=10;++rank) {
            if(rank==target.speed)continue;
            const auto* sample=Find({rank,target.context});if(!sample || sample->samples<minimum_samples)continue;
            const auto* flat=target.context.resource==normal_resource && target.context.material?
                Find({rank,{normal_resource,Slope::Flat,target.context.material}}):nullptr;
            if(!flat)flat=Find({rank,{normal_resource,Slope::Flat}});
            const float denominator=flat?flat->base:PriorTopMps(rank);
            if(!std::isfinite(denominator) || denominator<3 || denominator>25)continue;
            const float estimate=target_flat*sample->base/denominator;
            if(!std::isfinite(estimate) || estimate<3 || estimate>25)continue;
            const int distance=std::abs(target.speed-rank);
            // Prefer evidence with more than one accepted window; among equally
            // reliable candidates use the nearest rank then the larger sample.
            const bool trusted=sample->samples>=2,best_trusted=best_samples>=2;
            if(best==0 || (trusted&&!best_trusted) || (trusted==best_trusted &&
                (distance<best_distance || (distance==best_distance && sample->samples>best_samples)))) {
                best=estimate;best_distance=distance;best_samples=sample->samples;
            }
        }
        return best;
    }
    const LearnedPace* Merge(const ProfileKey& key,float base,std::uint64_t samples) {
        if(!key.Valid() || !LearnedPace{base,samples}.Valid())return nullptr;
        auto it=profiles_.find(key);
        if(it==profiles_.end()) {
            if(profiles_.size()>=kMaxProfiles)return nullptr;
            it=profiles_.emplace(key,LearnedPace{base,samples}).first;
        }else {
            // Do not let a slower concurrent/race-local observation erase
            // confirmed capability in the SAME terrain/slope bucket.
            it->second.base=std::max(it->second.base,base);
            it->second.samples=std::min<std::uint64_t>(1000000000,it->second.samples+samples);
        }
        revision_=next_revision_.fetch_add(1,std::memory_order_relaxed);
        it->second.memory_revision=revision_;return &it->second;
    }
    std::string Encode() const {
        std::ostringstream out;out.imbue(std::locale::classic());
        out<<"TJC_SPEED_LEARNING 4\n"<<std::setprecision(std::numeric_limits<float>::max_digits10);
        for(const auto& [key,value]:profiles_)out<<key.speed<<' '<<key.context.resource<<' '<<static_cast<unsigned>(key.context.slope)
            <<' '<<key.context.material<<' '<<value.base<<' '<<value.samples<<'\n';
        return out.str();
    }
    static std::optional<Store> Decode(std::string_view bytes) {return Read(bytes,false);}
    // One-time import only. Unclassified history retained in a v2 file is
    // validated but never promoted into terrain evidence.
    static std::optional<Store> DecodeV2(std::string_view bytes) {return Read(bytes,true);}
private:
    static std::optional<Store> Read(std::string_view bytes,bool legacy) {
        if(bytes.size()>kMaxBytes)return {};
        std::istringstream input{std::string(bytes)};input.imbue(std::locale::classic());
        std::string line;if(!std::getline(input,line))return {};
        if(!line.empty() && line.back()=='\r')line.pop_back();
        const bool v4=line=="TJC_SPEED_LEARNING 4";
        if(legacy ? line!="TJC_SPEED_LEARNING 2" : (!v4 && line!="TJC_SPEED_LEARNING 3"))return {};
        Store result;std::size_t rows=0;
        std::set<std::tuple<std::string,std::uint32_t,int,int,std::uint32_t,unsigned>> old_keys;
        while(std::getline(input,line)) {
            if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
            if(++rows>kMaxProfiles)return {};
            std::istringstream row(line);row.imbue(std::locale::classic());
            ProfileKey key;LearnedPace value;std::uint64_t resource=0,model=0,material=0;unsigned slope=0;
            std::string host;int acceleration=0;
            if(legacy){
                if(!(row>>host>>model>>key.speed>>acceleration) || !model || model>UINT32_MAX ||
                    acceleration<1 || acceleration>10 || host.empty() || host.size()>64 ||
                    host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return {};
            }else if(!(row>>key.speed))return {};
            if(!(row>>resource>>slope) || (v4 && !(row>>material)) || !(row>>value.base>>value.samples) || resource>UINT32_MAX || material>UINT32_MAX || slope>5)return {};
            key.context={static_cast<std::uint32_t>(resource),static_cast<Slope>(slope),static_cast<std::uint32_t>(material)};
            row>>std::ws;
            if(!row.eof() || !value.Valid() || key.speed<1 || key.speed>10)return {};
            if(legacy){
                if(!old_keys.emplace(host,static_cast<std::uint32_t>(model),key.speed,acceleration,key.context.resource,slope).second)return {};
                if(!resource && slope==0)continue;
                if(!key.Valid() || !result.Merge(key,value.base,value.samples))return {};
            }else if(!key.Valid() || !result.profiles_.emplace(key,value).second)return {};
        }
        return result;
    }

};
// The production bridge between an ephemeral sampler and durable profiles.
// Reset discards partial windows, never the shared profile repository.
struct PersistentReference {
    Reference reference;
    ProfileKey profile;
    Store* store=nullptr;
    void Reset(){reference.Reset();profile={};store=nullptr;}
    bool Bind(Key live,std::string_view host,float prior,Store* repository,ContextKey context={}) {
        (void)host; // Kept for callers; host is not a reusable capability key.
        const ProfileKey next{live.speed,context};
        const bool profile_changed=next!=profile || repository!=store;
        if(profile_changed){reference.Reset();profile=next;store=repository;}
        const bool entity_changed=reference.Bind(live,prior);
        if((profile_changed || entity_changed) && store && profile.Valid() && context.Valid())
            if(const auto* saved=store->Find(profile))reference.Restore(saved->base,saved->samples);
        return profile_changed || entity_changed;
    }
    void Update(const Reference::Sample& sample) {
        const auto before=reference.Observe().accepted;
        reference.Update(sample);
        const auto& value=reference.Observe();
        if(store && profile.context.Valid() && value.learned && value.accepted>before)
            if(const auto* saved=store->Merge(profile,value.base,value.accepted-before))
                reference.History(saved->samples);
    }
    Store::PairResult UpdateQualifiedSlice(const Reference::Sample& sample,double at) {
        const auto pair=reference.QualifiedPair(sample);
        if(!pair || !store || !profile.context.Valid())return {};
        auto result=store->SubmitProvisional(profile,*pair,at);
        if(result.state==Store::PairState::Promoted) {
            const auto previous=reference;
            const auto before=reference.Observe().accepted;
            reference.AcceptCorroborated(result.pace);
            const auto& value=reference.Observe();
            const auto* saved=value.learned && value.accepted>before?
                store->Merge(profile,value.base,value.accepted-before):nullptr;
            if(saved){reference.History(saved->samples);store->RecordConsumed(result.consumed_token);}
            else {reference=previous;result.state=Store::PairState::Unavailable;}
        }
        return result;
    }
};
} // namespace horse_motion::speed_learning
