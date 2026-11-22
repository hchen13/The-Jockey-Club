#pragma once
#include "horse_speed_learning_store.h"
#include "horse_native_environment.h"
#include "horse_gait_percentile.h"
#include <array>
#include <utility>

namespace horse_motion::speed_learning {
// Pure, bounded frame adapter. The native bridge supplies observations only;
// replay/tests use exactly this production state machine.
struct ContextReference : PersistentReference {
    struct Frame {
        Position position{};
        float dt=0,wall_dt=0;
        std::uint32_t resource=0;
        bool grounded=false,eligible=false,burst=false;
        float gait=std::numeric_limits<float>::quiet_NaN();
        // Native bridges pass one shared steady clock, so separate riders and
        // consecutive races can corroborate without comparing local clocks.
        double observed_at=0;
        bool agitation_blocked=false;
        std::uint32_t material=0;
    };
    Key live{};
    TravelContext travel{};
    // Geometry for pace lookup is independent of surface-learning windows.
    // A material switch must not discard a still-valid slope observation.
    TravelContext pace_travel{};
    float prior=0,window_wall=0,burst_tail=0;
    float window_gait=0,window_seconds=0;
    unsigned window_reject=0;
    static bool ValidGait(float gait){return std::isfinite(gait) && gait>=0 && gait<=4;}
    // The horse has one short-lived sample ring, not an array reserved for
    // each material and slope. Queries filter it for the active context.
    GaitPercentile gait_history{};
    bool dirty=false,gait_history_dirty=false;
    double gait_clock=0,last_qualified_at=0,qualified_span_start=0;
    float qualified_min_speed=0,qualified_max_speed=0;
    ContextKey last_qualified_context{};
    bool candidate_active=false;
    // One horse, one current material/resource, at most one unfinished
    // qualified slice per slope. A brief grade change need not erase the
    // earlier grade's slice, but unsafe control or a material change must.
    struct SlopeSlice {float pace=0;double clock=0;bool valid=false;};
    std::array<SlopeSlice,5> slope_slices{};
    std::uint32_t slope_slice_resource=0,slope_slice_material=0;
    unsigned slope_slice_pending=0;
    SamplingObservation sampling{};
    LearningDiagnostics diagnostics{};
    enum Reject : unsigned {Ineligible=1,ClockMismatch=2,Burst=4,InvalidGait=8,
        Unsettled=16,BelowGait=32,Agitation=64};
    std::uint64_t store_revision=0;
    const Store* stamped_store=nullptr;
    ProfileKey stamped_profile{};
    Store::ProfileStamp profile_stamp{};
    bool profile_stamp_valid=false;
    // Pair tokens overlapping this horse's current strict candidate. Tokens
    // are globally unique, never persisted and bounded by the six slices in
    // a three-second strict candidate.
    std::array<std::uint64_t,4> strict_pair_tokens{};
    unsigned strict_pair_count=0;
    std::uint64_t strict_event_cursor=0;
    void Reset(){
        if(!dirty)return;
        *this=ContextReference{};
    }
    void BreakWindow(){window_wall=window_gait=window_seconds=0;window_reject=0;}
    void BreakCandidates(CandidateBreakReason reason=CandidateBreakReason::Other){
        if(candidate_active){
            ++diagnostics.breaks[static_cast<unsigned>(reason)];
            reference.Break();
        }
        diagnostics.candidate_seconds=0;
        candidate_active=false;last_qualified_context={};
        strict_pair_tokens={};strict_pair_count=0;strict_event_cursor=0;
    }
    void DiscardOwnedPairs(){
        if(store)for(auto token:strict_pair_tokens)if(token)store->DiscardProvisional(profile,token);
        strict_pair_tokens={};strict_pair_count=0;strict_event_cursor=0;
    }
    void TrackStrictPair(std::uint64_t token){
        if(!store || !token)return;
        if(!strict_pair_count)strict_event_cursor=store->ConsumptionSequence();
        if(strict_pair_count==strict_pair_tokens.size()){
            // Losing provenance would risk double-counting. The independent
            // submitted pairs remain available; abandon only strict history.
            BreakCandidates(CandidateBreakReason::Other);reference.DiscardStrictWindow();return;
        }
        strict_pair_tokens[strict_pair_count++]=token;
    }
    void ClearSlopeSlices(){slope_slices={};slope_slice_resource=slope_slice_material=0;slope_slice_pending=0;}
    void ClearSlopeSlice(Slope grade){
        const auto index=static_cast<unsigned>(grade);
        if(index>=1 && index<=5 && slope_slices[index-1].valid){
            slope_slices[index-1]={};if(slope_slice_pending)--slope_slice_pending;
            if(!slope_slice_pending)slope_slice_resource=slope_slice_material=0;
        }
    }
    void BreakLearningTravel(){travel.Reset();BreakWindow();BreakCandidates(CandidateBreakReason::Travel);ClearSlopeSlices();}
    void BreakTravel(){BreakLearningTravel();pace_travel.Reset();}
    void DiscardGaitHistory(){ClearSlopeSlices();if(gait_history_dirty)gait_history.Clear();gait_history_dirty=false;gait_clock=last_qualified_at=qualified_span_start=0;
        qualified_min_speed=qualified_max_speed=0;last_qualified_context={};
        diagnostics.last_window_clock=diagnostics.last_pair_clock=std::numeric_limits<double>::quiet_NaN();}
    void RefreshStoredCapability(){
        store_revision=store?store->Revision():0;
        if(!store || !profile.context.Valid())return;
        const auto stamp=store->Stamp(profile);
        if(profile_stamp_valid && stamped_store==store && profile_stamp.epoch!=stamp.epoch){
            // Reload at the same Store address invalidates all local evidence
            // and inherited values, not just the active slope's pending slice.
            BreakCandidates(CandidateBreakReason::Other);reference.Reset();reference.Bind(live,prior);
            ClearSlopeSlices();
        }else if(profile_stamp_valid && stamped_store==store && strict_pair_count){
            // Another confirmation matters only if it consumed a pair that
            // overlaps this strict candidate. Eviction/expiry is not proof of
            // consumption. Event-ring overflow conservatively breaks strict.
            if(store->ConsumedSince(strict_event_cursor,strict_pair_tokens)){
                BreakCandidates(CandidateBreakReason::Other);reference.Break();
            }else strict_event_cursor=store->ConsumptionSequence();
        }
        stamped_store=store;stamped_profile=profile;profile_stamp=stamp;profile_stamp_valid=true;
        if(const auto* saved=store->Find(profile)){
            const auto& current=reference.Observe();
            // Restore accepted capability only. Restore preserves pending
            // sample sums; a cached bucket cannot overwrite newer shared
            // evidence, nor can a lower store value erase a local maximum.
            if(!current.learned || saved->base>current.base)
                reference.Restore(saved->base,saved->samples);
            else reference.History(std::max(current.history_samples,saved->samples));
        }
    }
    void BindContext(Key key,std::string_view host,float fallback,Store* repository,ContextKey context){
        dirty=true;
        if(profile.context==context && key.speed==profile.speed && store==repository){
            reference.Bind(key,fallback);
            if(store && store->Revision()!=store_revision)RefreshStoredCapability();
            return;
        }
        // A grade change interrupts unfinished speed evidence immediately,
        // even if no window in the new grade reaches P90.
        if(profile.context!=context)BreakCandidates(CandidateBreakReason::Context);
        // Accepted values are already published to the shared store. A
        // context switch breaks pending slices and rebinds only the current
        // reference; it must not keep references for other materials.
        Bind(key,host,fallback,repository,context);
        if(context.Valid())RefreshStoredCapability();
    }
    float FlatBase() const {
        if(profile.context.resource==native_environment::kDefaultKey && profile.context.slope==Slope::Flat && reference.Observe().learned)
            return reference.Base();
        if(store && travel.Material()){
            auto key=profile;key.context={native_environment::kDefaultKey,Slope::Flat,travel.Material()};
            if(const auto* value=store->Find(key))return value->base;
        }
        const auto flat=ContextKey{native_environment::kDefaultKey,Slope::Flat};
        if(profile.context==flat && reference.Observe().learned)return reference.Base();
        if(store){auto key=profile;key.context=flat;if(const auto* value=store->Find(key))return value->base;}
        return prior;
    }
    float MeasuredPace() const {
        return travel.Key().Valid() && profile.context==travel.Key() && reference.Observe().learned?reference.Base():0;
    }
    struct EstimateCache { ProfileKey key{};const Store* repository=nullptr;std::uint64_t revision=0;float flat=0,value=0;bool valid=false; };
    mutable std::array<EstimateCache,4> estimates{};
    float Estimate(ContextKey context,std::size_t slot,std::uint64_t minimum_samples=1) const {
        if(!store || !context.Valid() || profile.speed<1 || profile.speed>10)return 0;
        const auto key=ProfileKey{profile.speed,context};const float flat=FlatBase();
        auto& cache=estimates[slot];
        if(!cache.valid || !(cache.key==key) || cache.repository!=store || cache.revision!=store->Revision() || cache.flat!=flat) {
            cache={key,store,store->Revision(),flat,store->Estimate(key,native_environment::kDefaultKey,flat,minimum_samples),true};
        }
        return cache.value;
    }
    // Control can query a fresh geometric slope with the CURRENT material,
    // without binding that synthesized key to the learning reference.
    ContextKey PaceContext() const {
        // Prefer the ordinary material-specific window when it is ready.
        // The material-independent window fills only its transition gaps.
        if(travel.Key().Valid())return travel.Key();
        const auto geometry=pace_travel.Key();
        if(!geometry.Valid() || !travel.Resource())return {};
        return {travel.Resource(),geometry.slope,travel.Material()};
    }
    float PaceGrade() const {return travel.Key().Valid()?travel.Rise():pace_travel.Rise();}
    float ContextPaceFor(ContextKey context,bool* estimated=nullptr) const {
        if(estimated)*estimated=false;
        const float exact=context==profile.context?MeasuredPace():0;
        if(exact>0){
            // An unclassified row is a legacy resource-only fallback, never
            // a measurement of the currently unknown ground material.
            if(estimated && !profile.context.material)*estimated=true;
            return exact;
        }
        if(!context.Valid() || context.resource!=travel.Resource() || context.material!=travel.Material())return 0;
        if(store)if(const auto* value=store->Find({profile.speed,context})){
            if(estimated && !context.material)*estimated=true;return value->base;
        }
        float value=Estimate(context,0,2);
        // Legacy data never populates a material-specific reference; use it
        // only as an explicitly estimated fallback after exact material data.
        if(value<=0 && store && context.material &&
            (context.resource!=native_environment::kSnowKey || context.material==0x6ce4d538u)) {
            auto legacy=context;legacy.material=0;
            if(const auto* old=store->Find({profile.speed,legacy}))value=old->base;
        }
        const bool snow_grade=context.resource==native_environment::kSnowKey && context.slope!=Slope::Flat;
        // Snow grades may use a better-supported ordinary-grade ratio from
        // a different rank. Other resources preserve their existing order.
        if(value<=0 && store && context.slope!=Slope::Flat && context.resource!=native_environment::kDefaultKey) {
            const float terrain_flat=TerrainFlatPace(context.resource);
            const auto* slope=store->Find({profile.speed,{native_environment::kDefaultKey,context.slope}});
            const float normal_flat=FlatBase();
            const float normal_grade=slope && slope->samples>=2?slope->base:
                (snow_grade?Estimate({native_environment::kDefaultKey,context.slope},3,2):0.F);
            if(normal_grade>=3 && terrain_flat>=3 && normal_flat>=3 && normal_flat<=25) {
                const float candidate=terrain_flat*normal_grade/normal_flat;
                if(std::isfinite(candidate) && candidate>=3 && candidate<=25)value=candidate;
            }
        }
        // Snow grade estimation requires more than one cross-rank sample;
        // otherwise Compose can use snow-flat. Keep the previous single-donor
        // fallback for every other resource and for snow-flat itself.
        if(value<=0 && !snow_grade)value=Estimate(context,2);
        if(value>0 && estimated)*estimated=true;
        return value;
    }
    float ContextPace(bool* estimated=nullptr) const {return ContextPaceFor(travel.Key(),estimated);}
    float TerrainFlatPace(std::uint32_t resource=0,bool* estimated=nullptr) const {
        if(estimated)*estimated=false;
        // A confirmed resource is sufficient for flat fallback; waiting for
        // the first completed slope window would needlessly use the preset.
        if(!resource)resource=travel.Resource();
        if(!resource)return 0;
        const ContextKey flat{resource,Slope::Flat,travel.Material()};
        if(profile.context==flat && reference.Observe().learned){
            if(estimated && !flat.material)*estimated=true;
            return reference.Base();
        }
        if(store){auto key=profile;key.context=flat;
            if(const auto* value=store->Find(key)){if(estimated && !flat.material)*estimated=true;return value->base;}}
        const float value=Estimate(flat,1);
        if(value>0 && estimated)*estimated=true;
        if(value>0)return value;
        if(store && flat.material && (resource!=native_environment::kSnowKey || flat.material==0x6ce4d538u)){
            auto legacy=flat;legacy.material=0;
            if(const auto* old=store->Find({profile.speed,legacy})){if(estimated)*estimated=true;return old->base;}
        }
        return 0;
    }
    bool FlatContext() const {const auto key=travel.Key();return key.resource==native_environment::kDefaultKey && key.slope==Slope::Flat;}
    void ObserveFrame(Key key,std::string_view host,float fallback,Store* repository,const Frame& f) {
        dirty=true;
        if(key!=live || repository!=store){
            BreakTravel();burst_tail=0;live=key;
            DiscardGaitHistory();
            sampling={};diagnostics={};PersistentReference::Reset();
        }
        prior=fallback;
        if(std::isfinite(f.wall_dt) && f.wall_dt>0)gait_clock+=std::min(f.wall_dt,60.F);
        for(auto& slice:slope_slices)if(slice.valid && (gait_clock<slice.clock || gait_clock-slice.clock>2.)){
            slice={};if(slope_slice_pending)--slope_slice_pending;
        }
        const bool clock=std::isfinite(f.dt) && f.dt>0 && f.dt<=.25F &&
            std::isfinite(f.wall_dt) && f.wall_dt>0 && f.wall_dt<=.5F;
        if(!f.grounded || !f.resource || !clock || !f.eligible || f.burst || f.agitation_blocked || burst_tail>0 ||
            (slope_slice_resource && (slope_slice_resource!=f.resource || slope_slice_material!=f.material)))ClearSlopeSlices();
        if(!f.grounded || !f.resource || !clock) {
            BreakTravel();sampling.minimum_gait=std::numeric_limits<float>::quiet_NaN();
        }else if(travel.Resource() && (f.resource!=travel.Resource() || f.material!=travel.Material())) {
            // Only the learning window changes ownership. Geometry observes
            // uninterrupted motion without carrying samples across contexts.
            BreakLearningTravel();sampling.minimum_gait=std::numeric_limits<float>::quiet_NaN();
        }
        // Geometry derives only from displacement and time. A constant valid
        // identity prevents a surface/resource seam erasing the slope, while
        // invalid resource, teleport and airborne states still reset it.
        pace_travel.Update(f.position,f.resource?1u:0u,f.dt,f.grounded && clock);
        const bool sampled=travel.Update(f.position,f.resource,f.dt,f.grounded && clock,f.material);
        if(!sampled && !travel.Resource())ClearSlopeSlices(); // Teleport/invalid motion, not a fresh seed.
        BindContext(key,host,fallback,repository,travel.Key());
        if(f.burst)burst_tail=1.F;
        else if(std::isfinite(f.dt) && f.dt>0)burst_tail=std::max(0.F,burst_tail-f.dt);
        if(!f.eligible || f.burst || f.agitation_blocked || burst_tail>0){BreakCandidates(CandidateBreakReason::Ineligible);
            sampling.minimum_gait=std::numeric_limits<float>::quiet_NaN();}
        sampling.actual_gait=ValidGait(f.gait)?f.gait:std::numeric_limits<float>::quiet_NaN();
        if(sampled){
            window_wall+=f.wall_dt;window_seconds+=f.dt;
            if(ValidGait(f.gait)){
                window_gait+=f.gait*f.dt;
            }
            else window_reject|=InvalidGait;
            if(!f.eligible)window_reject|=Ineligible;
            if(f.agitation_blocked)window_reject|=Agitation;
            if(burst_tail>0)window_reject|=Burst;
        }else BreakWindow(); // Seed/rejected steps belong to neither position nor gait.
        if(travel.completed.ready) {
            // Grade and speed belong to exactly the same displacement window.
            // Reject each window before accumulating it: slow/fast clock
            // visits must not cancel into a plausible aggregate clock ratio.
            const float ratio=window_wall>0?travel.completed.seconds/window_wall:0;
            if(ratio<.75F || ratio>1.25F)window_reject|=ClockMismatch;
            if(travel.Coherence()<.985F || !travel.completed.settled || !travel.Key().Valid())window_reject|=Unsettled;
            sampling.mean_gait=window_seconds>0?window_gait/window_seconds:0;
            sampling.resource=travel.Key().resource;sampling.slope=static_cast<unsigned>(travel.Key().slope);
            ++sampling.windows;sampling.reject_mask=window_reject;
            ++diagnostics.completed_windows;diagnostics.last_window_clock=gait_clock;
            diagnostics.last_detail=travel.completed.detail;
            diagnostics.last_first_grade=travel.completed.first_grade;
            diagnostics.last_second_grade=travel.completed.second_grade;
            diagnostics.last_first_grade_ratio=travel.completed.first_grade_ratio;
            diagnostics.last_second_grade_ratio=travel.completed.second_grade_ratio;
            diagnostics.last_half_speed_delta=travel.completed.half_speed_delta;
            diagnostics.last_coherence=travel.Coherence();diagnostics.last_p90_samples=0;
            diagnostics.last_p90_source=diagnostics.last_p90_resource_samples=diagnostics.last_p90_slope_samples=0;
            sampling.p90_source=sampling.p90_resource_samples=sampling.p90_slope_samples=0;
            diagnostics.last_p90=std::numeric_limits<float>::quiet_NaN();
            if(!window_reject){
                // A query filters the one recent ring to this exact material
                // and resource. Its slope subset is preferred only after four
                // qualified windows. Query first so this sample cannot lower
                // the threshold that selects itself.
                const auto gait=gait_history.Query(travel.Key().resource,travel.Key().material,
                    static_cast<unsigned>(travel.Key().slope),gait_clock);
                const float resource_threshold=gait.resource;
                const float slope_threshold=gait.slope;
                const bool use_slope=slope_threshold>0;
                const float threshold=use_slope?slope_threshold:resource_threshold;
                const unsigned source=use_slope?2U:resource_threshold>0?1U:0U;
                diagnostics.last_p90_source=sampling.p90_source=source;
                diagnostics.last_p90_resource_samples=sampling.p90_resource_samples=gait.resource_samples;
                diagnostics.last_p90_slope_samples=sampling.p90_slope_samples=gait.slope_samples;
                diagnostics.last_p90_samples=use_slope?gait.slope_samples:gait.resource_samples;
                const auto piece_bin=GaitPercentile::Bin(sampling.mean_gait);
                const float piece=GaitPercentile::Value(piece_bin);
                sampling.minimum_gait=threshold>0?threshold:std::numeric_limits<float>::quiet_NaN();
                diagnostics.last_p90=sampling.minimum_gait;
                gait_history.Add(travel.Key().resource,travel.Key().material,
                    static_cast<unsigned>(travel.Key().slope),piece_bin,gait_clock);
                gait_history_dirty=true;
                if(threshold>0 && piece>=threshold){
                    ++diagnostics.p90_qualified;
                    // Two nearby, non-overlapping P90 slices can provide an
                    // initial low-confidence observation without requiring
                    // an unrealistically long unbroken mountain grade.
                    const float pace=travel.completed.speed;
                    const bool context_changed=last_qualified_context!=travel.Key();
                    const bool span_exceeded=candidate_active && gait_clock-qualified_span_start>30.;
                    const bool speed_range=std::max(qualified_max_speed,pace)-std::min(qualified_min_speed,pace)>.35F;
                    if(context_changed || span_exceeded || speed_range) {
                        BreakCandidates(context_changed?CandidateBreakReason::Context:
                            span_exceeded?CandidateBreakReason::Span:CandidateBreakReason::SpeedRange);
                        qualified_span_start=gait_clock;
                        qualified_min_speed=qualified_max_speed=pace;
                    }
                    qualified_min_speed=std::min(qualified_min_speed,pace);
                    qualified_max_speed=std::max(qualified_max_speed,pace);
                    const Reference::Sample accepted{travel.completed.seconds,window_wall,travel.completed.speed,true,false,true};
                    const auto before=reference.Observe().accepted;
                    // Independently accumulate three seconds of qualified
                    // samples within the existing 30-second candidate span.
                    // Below-P90 windows are never included in that total.
                    Update(accepted);
                    if(reference.Observe().accepted>before) {
                        // Own Merge already consumed these samples; mark the
                        // resulting stamp observed before any later frame.
                        if(store){stamped_store=store;stamped_profile=profile;profile_stamp=store->Stamp(profile);profile_stamp_valid=true;}
                        // The strict path already consumed this evidence;
                        // never count its final slice as a second observation.
                        reference.DiscardQualifiedSlice();
                        DiscardOwnedPairs();
                        // Other slopes' unsubmitted slices are independent.
                        ClearSlopeSlice(travel.Key().slope);
                    } else {
                        const double at=std::isfinite(f.observed_at) && f.observed_at>0?f.observed_at:gait_clock;
                        Store::PairResult pair{};
                        const auto slope=static_cast<unsigned>(travel.Key().slope);
                        if(store && profile.Valid() && slope>=1 && slope<=5){
                            if(slope_slice_resource!=travel.Key().resource || slope_slice_material!=travel.Key().material){
                                ClearSlopeSlices();slope_slice_resource=travel.Key().resource;slope_slice_material=travel.Key().material;
                            }
                            auto& pending=slope_slices[slope-1];
                            const double elapsed=gait_clock-pending.clock;
                            if(pending.valid && elapsed>=0 && elapsed<=2. && std::abs(pending.pace-pace)<=.35F){
                                const float candidate=std::min(pending.pace,pace);
                                pending={};if(slope_slice_pending)--slope_slice_pending;
                                pair=store->SubmitProvisional(profile,candidate,at);
                                if(pair.state==Store::PairState::Promoted){
                                    const auto previous=reference;
                                    const auto previous_count=reference.Observe().accepted;
                                    reference.AcceptCorroborated(pair.pace);
                                    const auto& value=reference.Observe();
                                    const auto* saved=value.learned && value.accepted>previous_count?
                                        store->Merge(profile,value.base,value.accepted-previous_count):nullptr;
                                    if(saved){reference.History(saved->samples);store->RecordConsumed(pair.consumed_token);
                                        DiscardOwnedPairs();stamped_store=store;stamped_profile=profile;
                                        profile_stamp=store->Stamp(profile);profile_stamp_valid=true;}
                                    else {reference=previous;pair.state=Store::PairState::Unavailable;}
                                }
                            }else{
                                if(!pending.valid)++slope_slice_pending;
                                pending={pace,gait_clock,true};
                            }
                        }
                        if(pair.submitted_token)TrackStrictPair(pair.submitted_token);
                        if(pair.state!=Store::PairState::Unavailable){
                            ++diagnostics.pair_completed;diagnostics.last_pair_state=static_cast<unsigned>(pair.state);
                            diagnostics.last_pair_pace=pair.pace;diagnostics.last_pair_pending=pair.pending;
                            diagnostics.last_pair_clock=gait_clock;
                        }
                        if(pair.state==Store::PairState::Promoted){
                            ++diagnostics.pair_promoted;
                            // The corroborated pair consumed selected slices;
                            // do not later count them again through strict 3 s.
                            reference.DiscardStrictWindow();
                        }
                    }
                    diagnostics.candidate_seconds=reference.PendingSeconds()+
                        (reference.HasPreviousWindow()?1.5F:0.F);
                    candidate_active=diagnostics.candidate_seconds>0 || reference.PendingQualifiedSeconds()>0;
                    last_qualified_at=gait_clock;
                    last_qualified_context=candidate_active?travel.Key():ContextKey{};
                    ++sampling.qualified;
                }else {sampling.reject_mask|=BelowGait;
                    // A below-P90 window is never included in learned pace.
                    // Retain already qualified strict samples within their
                    // existing 30-second candidate span.
                    // Other slopes' short slices retain their independent TTL.
                    reference.DiscardQualifiedSlice();
                    const auto slope=static_cast<unsigned>(travel.Key().slope);
                    if(slope>=1 && slope<=5 && slope_slices[slope-1].valid){slope_slices[slope-1]={};if(slope_slice_pending)--slope_slice_pending;}
                    if(threshold>0)++diagnostics.p90_below;else ++diagnostics.p90_warmup;}
            }else {
                // A pure slope-boundary transition is not an unsafe riding
                // state: discard this window but let earlier qualified
                // same-material slices expire naturally after two seconds.
                // Every other rejection still invalidates all pending slices.
                if(window_reject!=Unsettled || travel.completed.detail!=TravelContext::GradeVariation)ClearSlopeSlices();
                BreakCandidates(CandidateBreakReason::RejectedWindow);sampling.minimum_gait=std::numeric_limits<float>::quiet_NaN();
            }
            // Fully completed provisional pairs remain available across
            // normal context changes until their bounded expiration.
            BreakWindow();
        }
    }
};
} // namespace horse_motion::speed_learning
