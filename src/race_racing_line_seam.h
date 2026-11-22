#pragma once
#include "race_racing_line_verification.h"
#include <optional>

namespace horse_races::racing_line::detail {
// Compare actual submitted geometry with an exact recorded alternative. The
// caller supplies an already verified line; unchanged geometry stays trusted.
class RecordedSeamChoice {
    const course::Path& centre;
    const Params& params;
    const std::vector<Vec2>& normals;
    const std::vector<SectionBound>& rows;
    std::vector<unsigned char>& recorded;
    std::vector<Vector3>& points;
    std::vector<float>& offsets;
    const std::size_t n;
    const float usable,limit,reach;
    const std::vector<unsigned char> no_retention;
    std::optional<ChordVerifier> positive;
    std::vector<std::pair<std::size_t,std::size_t>> accepted;
    std::size_t refinement_attempts=0,refinement_work=0,local_accepted=0;
    bool SpendLocal(std::size_t work) {
        if(refinement_attempts>=32||work>4*n-refinement_work)return false;
        ++refinement_attempts;refinement_work+=work;return true;
    }
    static bool Same(Vector3 a,Vector3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
    bool Fixed(std::size_t i) const {return params.fixed_offsets&&std::isfinite((*params.fixed_offsets)[i]);}
    bool Ready() const {return n>=3&&centre.metres.size()==n&&normals.size()==n&&points.size()==n&&
        offsets.size()==n&&recorded.size()+1==n&&(!params.fixed_offsets||params.fixed_offsets->size()==n)&&
        params.obstacles&&params.obstacles->native_mode;}
    ChordVerifier& Proof() {
        if(!positive)positive.emplace(centre,params,rows,no_retention,usable,limit);
        return *positive;
    }
    template<class Point> double Score(Point point,std::size_t left,std::size_t right) const {
        const std::vector<double> zero;
        double total=0;const auto first=std::max<std::size_t>(1,left?left-1:0),last=std::min(n-2,right+1);
        for(auto i=first;i<=last;++i) {
            const auto a=point(i-1),b=point(i),c=point(i+1);Row row;
            row.u={b.x-a.x,b.y-a.y};row.v={c.x-b.x,c.y-b.y};
            const double k=row.TurningCurvature<0>(zero,normals);
            if(!std::isfinite(k))return 1e30;
            const double mass=.5*(std::hypot(row.u.x,row.u.y)+std::hypot(row.v.x,row.v.y));
            total+=mass*k*k*k*k;
        }
        return total;
    }
    static bool Better(double before,double after) {
        return std::isfinite(before)&&std::isfinite(after)&&after<before-std::max(1e-15,before*1e-12);
    }
    template<class Point> bool Validate(Point point,std::size_t left,std::size_t right,
        std::vector<std::size_t>& uncertified,bool allow_recorded=true,std::size_t* failed_chord=nullptr,bool full_reserve=false) {
        const auto fail=[&](std::size_t chord){if(failed_chord)*failed_chord=chord;return false;};
        // Only changed nodes and their order neighbours can invalidate a
        // previously checked line. Each affected chord includes both joins.
        const auto first_node=left?left-1:0,last_node=std::min(n-1,right+1);
        float previous=-1;
        if(first_node) {
            const auto projection=ProjectNear(centre,point(first_node-1),centre.metres[first_node-1],reach);
            if(!projection.valid)return fail(n);
            previous=projection.along;
        }
        for(auto i=first_node;i<=last_node;++i) {
            const auto p=point(i);if(!destination::Finite(p))return fail(i?i-1:0);
            const auto projection=ProjectNear(centre,p,centre.metres[i],reach);
            if(!projection.valid||projection.distance>limit||!(projection.along>previous))return fail(i?i-1:0);
            previous=projection.along;
        }
        const auto first=left?left-1:0,last=std::min(n-2,right);
        for(auto i=first;i<=last;++i) {
            const auto a=point(i),b=point(i+1);
            if(Same(a,points[i])&&Same(b,points[i+1]))continue;
            const float length=destination::Distance(a,b);
            const auto ca=centre.points[i],cb=centre.points[i+1];
            if(length<.05F||length>250.0F||(b.x-a.x)*(cb.x-ca.x)+(b.y-a.y)*(cb.y-ca.y)<=.01F)return fail(i);
            const int samples=static_cast<int>(std::ceil(length/std::min(1.0F,params.verify_step)));
            for(int s=1;s<samples;++s) {
                const float t=static_cast<float>(s)/samples;
                if(Proof().Beyond({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t},i))return fail(i);
            }
            if(!(full_reserve?Proof().NegativeAllowsWithReserve(a,b,i):Proof().NegativeAllows(a,b,i)))return fail(i);
            const bool measured=Proof().Measured(a,b,i);
            if(!measured&&(!allow_recorded||!(Same(a,ca)&&Same(b,cb))))return fail(i);
            if(!measured)uncertified.push_back(i);
        }
        return true;
    }
public:
    RecordedSeamChoice(const course::Path& reference,const Params& policy,const std::vector<Vec2>& normal,
        const std::vector<SectionBound>& measured,std::vector<unsigned char>& mask,
        std::vector<Vector3>& submitted,std::vector<float>& lateral)
        :centre(reference),params(policy),normals(normal),rows(measured),recorded(mask),points(submitted),offsets(lateral),
         n(reference.points.size()),usable(policy.Usable()),limit(usable-.01F),reach(std::max(60.0F,3*usable+30)),
         no_retention(n?n-1:0,0) {}
    bool EvaluateRecordedWindow(std::size_t left,std::size_t right) {
        if(!Ready()||left>right||right>=n)return false;
        bool changed=false;
        for(auto i=left;i<=right;++i) {
            if(Fixed(i)&&(*params.fixed_offsets)[i]!=0)return false;
            changed|=!Same(points[i],centre.points[i]);
        }
        if(!changed)return false;
        auto original=[&](std::size_t i){return points[i];};
        auto candidate=[&](std::size_t i){return i>=left&&i<=right?centre.points[i]:points[i];};
        if(!Better(Score(original,left,right),Score(candidate,left,right)))return false;
        std::vector<std::size_t> uncertified;
        return Validate(candidate,left,right,uncertified);
    }
    bool TryRecordedWindow(std::size_t left,std::size_t right) {
        if(!EvaluateRecordedWindow(left,right))return false;
        auto candidate=[&](std::size_t i){return i>=left&&i<=right?centre.points[i]:points[i];};
        std::vector<std::size_t> uncertified;
        if(!Validate(candidate,left,right,uncertified))return false;
        for(auto i:uncertified)recorded[i]=1;
        for(auto i=left;i<=right;++i){points[i]=centre.points[i];offsets[i]=0;}
        accepted.push_back({left,right});return true;
    }
    bool Select() {
        if(!Ready())return false;
        const auto previous=accepted.size();
        const auto mask=recorded;
        for(std::size_t i=0;i<mask.size();) {
            if(mask[i]){++i;continue;}
            const auto start=i;while(i<mask.size()&&!mask[i])++i;
            const auto end=i;
            if(!start||end==mask.size()||!mask[start-1]||!mask[end])continue;
            auto left=start,right=end;
            while(left&&mask[left-1])--left;
            while(right<mask.size()&&mask[right])++right;
            TryRecordedWindow(left,right);
        }
        return accepted.size()!=previous;
    }
    const auto& windows() const {return accepted;}
    std::vector<unsigned char> FineFree() const {
        std::vector<unsigned char> free(n,0);
        for(auto [left,right]:accepted) {
            for(auto i=left;i&&!recorded[i-1];--i){if(Fixed(i-1))break;free[i-1]=1;}
            for(auto i=right;i+1<n&&!recorded[i];++i){if(Fixed(i+1))break;free[i+1]=1;}
        }
        for(auto [left,right]:accepted)for(auto i=left;i<=right;++i)free[i]=1;
        for(std::size_t i=0;i<n;++i)if(!i||i+1==n||Fixed(i)||
            (i&&recorded[i-1])||(i+1<n&&recorded[i]))free[i]=0;
        return free;
    }
private:
    bool SplitFine(std::vector<Vector3>& candidate,std::vector<float>& blended) {
        std::vector<std::pair<std::size_t,std::size_t>> pending;
        for(std::size_t i=0;i<n;) {
            if(Same(candidate[i],points[i])){++i;continue;}
            const auto left=i;while(i<n&&!Same(candidate[i],points[i]))++i;
            pending.push_back({left,i-1});
        }
        // Preserve the existing split search allowance: successful independent
        // runs do not consume the 32 FAILURE limit, and the preceding full
        // candidate check does not debit this bounded 4N-node split search.
        std::size_t work=0,failures=0;bool kept=false;
        const auto work_budget=4*n;
        while(!pending.empty()&&failures<32) {
            const auto [left,right]=pending.back();pending.pop_back();
            const auto span=right-left+3;
            if(span>work_budget-work)break;
            work+=span;
            // Earlier accepted pieces are now the baseline, including both
            // joins of this proposal and every affected three-point bend.
            auto original=[&](std::size_t i){return points[i];};
            auto proposed=[&](std::size_t i){return i>=left&&i<=right?candidate[i]:points[i];};
            std::size_t failed=n;std::vector<std::size_t> uncertified;
            if(!Validate(proposed,left,right,uncertified,false,&failed)) {
                ++failures;
                const auto first=left?left-1:0;
                if(failed>=n-1||failed<first||failed>right)continue;
                // Preserve the rejected chord's endpoints. Every queued child
                // is strictly smaller; an outside failure cannot requeue it.
                candidate[failed]=points[failed];candidate[failed+1]=points[failed+1];
                blended[failed]=offsets[failed];blended[failed+1]=offsets[failed+1];
                if(left<failed)pending.push_back({left,failed-1});
                if(failed+2<=right)pending.push_back({failed+2,right});
                continue;
            }
            if(!Better(Score(original,left,right),Score(proposed,left,right)))continue;
            for(auto i=left;i<=right;++i){points[i]=candidate[i];offsets[i]=blended[i];}
            kept=true;
        }
        return kept;
    }
    bool AcceptFineCandidate(const std::vector<double>& candidate_offsets,bool allow_split) {
        if(!Ready()||accepted.empty()||candidate_offsets.size()!=n)return false;
        const double aggression=params.Aggression();
        if(!(aggression>0))return false;
        const auto free=FineFree();
        std::vector<Vector3> candidate(n);
        std::vector<float> blended(n);
        for(std::size_t i=0;i<n;++i) {
            const double solved=candidate_offsets[i];if(!std::isfinite(solved))return false;
            // The input line has already received the configured skill blend.
            // Apply the same utilization to this optional additional movement.
            const double d=aggression==1?solved:offsets[i]+aggression*(solved-offsets[i]);
            if(Fixed(i)&&d!=(*params.fixed_offsets)[i])return false;
            blended[i]=static_cast<float>(d);
            candidate[i]={static_cast<float>(centre.points[i].x+blended[i]*normals[i].x),
                static_cast<float>(centre.points[i].y+blended[i]*normals[i].y),centre.points[i].z};
            if(!free[i]&&!Same(candidate[i],points[i]))return false;
        }
        auto original=[&](std::size_t i){return points[i];};
        auto proposed=[&](std::size_t i){return candidate[i];};
        if(!Better(Score(original,0,n-1),Score(proposed,0,n-1)))return false;
        std::vector<std::size_t> uncertified;
        // Fine movement must retain positive coverage; returning to zero never
        // invents a certificate. Check only changed runs and their two joins.
        for(std::size_t i=0;i<n;) {
            if(Same(candidate[i],points[i])){++i;continue;}
            const auto left=i;while(i<n&&!Same(candidate[i],points[i]))++i;
            if(!Validate(proposed,left,i-1,uncertified,false))
                return allow_split?SplitFine(candidate,blended):false;
        }
        points=std::move(candidate);
        offsets=std::move(blended);
        return true;
    }
public:
    bool AcceptFine(const std::vector<double>& candidate_offsets) {
        refinement_attempts=refinement_work=0;
        return AcceptFineCandidate(candidate_offsets,false);
    }
    bool AcceptFineOrSplit(const std::vector<double>& candidate_offsets) {
        refinement_attempts=refinement_work=0;
        return AcceptFineCandidate(candidate_offsets,true);
    }
private:
    // A coarse/fine solve can leave a local oscillation even though returning
    // towards the reference improves its complete curvature objective. This
    // bounded final comparison is independent of missing-coverage seam triggers.
    // It never pins a recording or treats recorded zero as positive evidence.
    bool ImproveLocal() {
        if(!Ready()||!(params.Aggression()>0))return false;
        struct Candidate {std::size_t node;double excess;};
        std::vector<Candidate> candidates;
        auto original=[&](std::size_t i){return points[i];};
        auto reference=[&](std::size_t i){return centre.points[i];};
        for(std::size_t i=1;i+1<n;++i) {
            if(Fixed(i)||recorded[i-1]||recorded[i])continue;
            const double current=Score(original,i,i),base=Score(reference,i,i);
            if(Better(current,base))candidates.push_back({i,current-base});
        }
        std::sort(candidates.begin(),candidates.end(),[](auto a,auto b) {
            return a.excess>b.excess||(a.excess==b.excess&&a.node<b.node);
        });
        // Same physical windows as bounded negative repair. A dense captured
        // core does not change their metres or the finite node-work allowance.
        std::vector<std::pair<float,float>> visited;
        const auto before=local_accepted;
        for(auto selected:candidates) {
            if(refinement_attempts>=32||refinement_work>=4*n)break;
            const float at=centre.metres[selected.node];
            if(std::any_of(visited.begin(),visited.end(),[&](auto span){return at>=span.first&&at<=span.second;}))continue;
            visited.push_back({at-12,at+12});
            for(float radius:{48.F,24.F}) {
                const auto left=static_cast<std::size_t>(std::lower_bound(centre.metres.begin(),centre.metres.end(),at-radius)-centre.metres.begin());
                const auto end=static_cast<std::size_t>(std::upper_bound(centre.metres.begin(),centre.metres.end(),at+radius)-centre.metres.begin());
                if(end<=left)continue;
                const auto right=end-1,work=end-left+4;
                if(!SpendLocal(work))break;
                std::vector<Vector3> proposed(end-left);
                std::vector<float> lateral(end-left);
                bool changed=false;
                for(auto i=left;i<end;++i) {
                    proposed[i-left]=points[i];lateral[i-left]=offsets[i];
                    if(!i||i+1==n||Fixed(i)||recorded[i-1]||recorded[i])continue;
                    const double distance=std::abs(centre.metres[i]-at),inner=.5*radius;
                    const double weight=distance<=inner?1:.5*(1+std::cos(3.141592653589793*(distance-inner)/inner));
                    const float offset=static_cast<float>(offsets[i]*(1-params.Aggression()*weight));
                    if(offset==offsets[i])continue;
                    lateral[i-left]=offset;
                    proposed[i-left]={static_cast<float>(centre.points[i].x+offset*normals[i].x),
                        static_cast<float>(centre.points[i].y+offset*normals[i].y),centre.points[i].z};
                    changed|=!Same(proposed[i-left],points[i]);
                }
                if(!changed)continue;
                auto candidate=[&](std::size_t i){return i>=left&&i<=right?proposed[i-left]:points[i];};
                if(!Better(Score(original,left,right),Score(candidate,left,right)))continue;
                std::vector<std::size_t> uncertified;
                // Even an exact recorded chord needs independent positive
                // coverage in this optional phase, as do both changed joins.
                if(!Validate(candidate,left,right,uncertified,false,nullptr,true))continue;
                for(auto i=left;i<end;++i){points[i]=proposed[i-left];offsets[i]=lateral[i-left];}
                ++local_accepted;break;
            }
        }
        return local_accepted!=before;
    }
public:
    // Keep every safe improvement from the existing coordinate/split search.
    // The reference direction then has a separate bounded ADDITIONAL allowance
    // of 4N nodes / 32 candidates, reusing the same score and evidence verifier.
    bool Refine(const std::vector<double>* coordinate_candidate=nullptr) {
        refinement_attempts=refinement_work=0;local_accepted=0;
        const bool fine=coordinate_candidate&&AcceptFineCandidate(*coordinate_candidate,true);
        return ImproveLocal()||fine;
    }
    std::size_t RefinementAttempts() const{return refinement_attempts;}
    std::size_t RefinementWork() const{return refinement_work;}
    std::size_t LocalAccepted() const{return local_accepted;}
};
} // namespace horse_races::racing_line::detail
