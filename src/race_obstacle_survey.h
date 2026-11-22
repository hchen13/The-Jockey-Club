#pragma once
#include "race_obstacle_survey_probe.h"

namespace horse_races::obstacle_survey {
struct Capsule {
    Vector3 from{},to{};ScrHandle handle=0;int cell=0,issued_at=0,load_phase=0;bool head=false,boundary=false,resolved=false;
    Sample support_from,support_to;
    std::shared_ptr<QueryResult> query;
};
struct PendingRow {
    std::size_t row=0;int born=0,remaining=0;std::array<Cell,kCells> cells{};
    std::vector<Capsule> capsules;bool centre_valid=false,query_failed=false;int half=20;
    float along=0;
};
struct Ray {Vector3 from{},to{};bool cross=false;std::uint32_t why=0;Sample support_from,support_to;};
enum class Phase {CentreLoad,GroundLoad,GroundProbe,GroundWater,Cell,Edges,RefineLoad,RefineProbe,RefineWater,Rays,Capsules,Finish};
struct Work {
    bool active=false,query_failed=false;std::size_t row=0;Phase phase=Phase::CentreLoad;
    obstacle_map::Section previous,current;
    std::array<std::array<Sample,kNodes>,3> ground{};
    std::array<Cell,kCells> cells{};
    int slot=0,order=0,cell=0,edge=0,refine=0,ray=0,half=20;
    std::vector<Edge> edges;std::vector<Ray> rays;std::vector<Capsule> capsules;
    float along=0;
};
struct Evidence {
    std::size_t row=0;float along=0,lateral=0;const char* kind="";std::uint32_t reason=0;
    Vector3 from{},to{},hit{},normal{},end_normal{};int status=2;Entity entity=0;
};
struct Survey {
    int probes=0,rows=0,frames=0,capsules=0,capsules_hit=0,capsules_refused=0,capsules_dropped=0;
    int steps=0,stalled=0,unknown_cells=0,budget_overruns=0;
    int outside_cells=0,query_rows_deferred=0,ground_query_failures=0,ground_misses=0,ray_query_failures=0,capsule_query_failures=0;
    int supporting_contacts=0,transient_contacts=0;
    int pending_capsules=0,peak_pending_capsules=0,refused_tick=-1;
    double ms_total=0,ms_max=0,max_step_ms=0;
    std::vector<PendingRow> in_flight;
    std::vector<Evidence> evidence;
    std::vector<std::size_t> changed_rows;
    Work work;
    std::vector<int> next_attempt;
    std::vector<std::uint64_t> last_selected;
    std::uint64_t selections=0;
    std::size_t scan_cursor=0,best_row=0,resolve_row=0,resolve_cap=0,issue_row=0,issue_cap=0;
    float best_distance=INFINITY;bool have_best=false;
    int ticks=0,turn=0,idle_until=0;float required_width=20;std::string bound_key,bound_geometry;
    Vector3 idle_anchor{};std::uint64_t idle_revision=0;
    bool resolve_done=false;int issue_remaining=0,poll_remaining=0;
    void Record(Evidence value){
        // Bounded examples, not one log per probe; no extra game query.
        if(evidence.size()>=24)return;int count=0;
        for(const auto& old:evidence)if(old.reason==value.reason&&std::string_view(old.kind)==value.kind)++count;
        if(count<2)evidence.push_back(value);
    }
    std::string QueryStats() const {
        return " unknown_cells="+std::to_string(unknown_cells)+" unrequested_cells="+std::to_string(outside_cells)+
            " deferred_rows="+std::to_string(query_rows_deferred)+" ground_query_failures="+std::to_string(ground_query_failures)+
            " ground_misses="+std::to_string(ground_misses)+" ray_query_failures="+std::to_string(ray_query_failures)+
            " capsule_query_failures="+std::to_string(capsule_query_failures)+" unconsumed_capsules="+std::to_string(pending_capsules)+
            " supporting_contacts="+std::to_string(supporting_contacts)+" transient_contacts="+std::to_string(transient_contacts)+
            " peak_unconsumed_capsules="+std::to_string(peak_pending_capsules)+" shared_live_queries="+std::to_string(QueryQueue::Total())+
            " max_step_ms="+std::to_string(max_step_ms)+" max_tick_ms="+std::to_string(ms_max)+
            " budget_overruns="+std::to_string(budget_overruns);
    }
    std::vector<std::string> TakeEvidence(){
        std::vector<std::string> out;out.reserve(evidence.size());
        for(const auto& e:evidence){
            std::ostringstream s;s<<std::fixed<<std::setprecision(3);
            const auto vec=[&](Vector3 v){s<<'('<<v.x<<','<<v.y<<','<<v.z<<')';};
            s<<"survey evidence row="<<e.row<<" s="<<e.along<<" lateral="<<e.lateral<<" kind="<<e.kind<<" reason="<<e.reason<<" status="<<e.status<<" entity="<<e.entity<<" from=";vec(e.from);
            s<<" to=";vec(e.to);s<<" hit=";vec(e.hit);s<<" normal=";vec(e.normal);s<<" end_normal=";vec(e.end_normal);out.push_back(s.str());
        }
        evidence.clear();return out;
    }
    void Reset(){*this=Survey{};}
    static int Node(int order){return order==0?40:40+((order&1)?(order+1)/2:-order/2);}
    static float Lateral(int node){return -kSurveyHalfWidth+kGridPitch*static_cast<float>(node);}
    Vector3 Position(int slot,int node) const {
        const float d=Lateral(node);
        const Vector3 a{work.previous.centre.x+work.previous.nx*d,work.previous.centre.y+work.previous.ny*d,work.previous.centre.z};
        const Vector3 b{work.current.centre.x+work.current.nx*d,work.current.centre.y+work.current.ny*d,work.current.centre.z};
        if(slot==0)return a;if(slot==2)return b;return {(a.x+b.x)*.5F,(a.y+b.y)*.5F,(a.z+b.z)*.5F};
    }
    float GroundSeed(int slot,int node) const {
        // A missed point is still unknown. Retain the nearest measured inner
        // height so one miss cannot reset an uphill trace below the terrain.
        if(node!=40) {
            const int inward=node<40?1:-1;
            for(int neighbour=node+inward;;neighbour+=inward) {
                if(work.ground[slot][neighbour].valid)return work.ground[slot][neighbour].at.z+1.5F;
                if(neighbour==40)break;
            }
        }
        return Position(slot,node).z;
    }
    void NextGround() {
        if(++work.order>work.half*4){work.order=0;++work.slot;}
        work.phase=work.slot>=3?Phase::Cell:Phase::GroundLoad;
    }
    bool Loaded(Vector3 p){return ENTITY::HAS_COLLISION_LOADED_AROUND_POSITION(p.x,p.y,p.z);}
    Sample ReadGround(Vector3 p,float seed) {
        ++probes;const auto hit=Line({p.x,p.y,seed+kGroundProbeAboveRiding},{p.x,p.y,seed-kProbeBelow},kWorld|kObjects);
        Sample out;out.at=p;
        if(hit.status!=2){++ground_query_failures;work.query_failed=true;if(hit.pending)QueryQueue::Track(hit.pending);}
        else if(!hit.hit)++ground_misses;
        const float lateral=(p.x-work.current.centre.x)*work.current.nx+(p.y-work.current.centre.y)*work.current.ny;
        if(hit.status!=2||!hit.hit)Record({work.row,work.along,lateral,"ground",hit.status==2?obstacle_map::kGroundUnknown:obstacle_map::kQueryUnknown,
            {p.x,p.y,seed+kGroundProbeAboveRiding},{p.x,p.y,seed-kProbeBelow},hit.at,hit.normal,{},hit.status,hit.entity});
        if(hit.status==2&&hit.hit&&Finite(hit.at)&&UnitNormal(hit.normal)) {
            const bool centre=work.phase==Phase::GroundProbe&&Node(work.order)==40;
            if(!centre||Position(work.slot,40).z-hit.at.z<=kMaximumRecordedFloorDrop) {
                out.at=hit.at;out.normal=hit.normal;out.valid=true;
            }
        }
        return out;
    }
    bool InFlight(std::size_t row) const {for(const auto& p:in_flight)if(p.row==row)return true;return false;}
    // Exactly one candidate per step; no sort or unbudgeted full-map scan.
    bool SelectStep(obstacle_map::Map& map,const course::Path& path,const std::vector<Vector3>& anchors) {
        if(work.active||in_flight.size()>=kPendingRows||map.rows.empty())return false;
        if(last_selected.size()!=map.rows.size())last_selected.assign(map.rows.size(),0);
        if(ticks<idle_until&&map.revision==idle_revision&&std::hypot(anchors.front().x-idle_anchor.x,anchors.front().y-idle_anchor.y)<3)return false;
        if(scan_cursor>=map.rows.size())scan_cursor=0;
        const std::size_t r=scan_cursor++;
        if(map.NeedsSurvey(r,required_width)&&ticks>=next_attempt[r]&&!InFlight(r)) {
            const auto c=map.SectionFor(path,r).centre;float d=INFINITY;
            for(const auto a:anchors)d=std::min(d,std::hypot(c.x-a.x,c.y-a.y));
            if(d<=kRangeMetres) {
                // Continue measured strips before scattering isolated updates.
                // A two-row bonus cannot outrank a much nearer new position.
                const bool needs_incoming=r&&(map.rows[r-1].HasDetailed()||InFlight(r-1));
                const float score=d-(needs_incoming?2*obstacle_map::kStepMetres:0);
                // Every fourth acquisition serves the least recently selected
                // eligible row. Nearby migration seams therefore cannot starve
                // another unresolved link on repeated moving passes.
                const bool fair=selections%4==3;
                if(!have_best||(fair&&last_selected[r]<last_selected[best_row])||
                   ((!fair||last_selected[r]==last_selected[best_row])&&score<best_distance)) {
                    best_distance=score;best_row=r;have_best=true;
                }
            }
        }
        if(scan_cursor==map.rows.size()) {
            scan_cursor=0;
            if(have_best) {
                last_selected[best_row]=++selections;
                work=Work{};work.active=true;work.row=best_row;
                work.along=map.Along(best_row);
                work.current=map.SectionFor(path,best_row);work.previous=best_row?map.SectionFor(path,best_row-1):work.current;
                work.slot=best_row?0:2;work.half=static_cast<int>(required_width);work.cell=20-work.half;
                for(int i=0;i<kCells;++i)if(i<20-work.half||i>=20+work.half) {
                    work.cells[i].open=work.cells[i].link=false;work.cells[i].unknown=true;
                }
            } else {
                idle_until=ticks+60;idle_anchor=anchors.front();idle_revision=map.revision;
                best_distance=INFINITY;return false;
            }
            have_best=false;best_distance=INFINITY;
        }
        return true;
    }
    void AddEdge(Sample a,Sample b,bool cross,bool ray) {
        Edge e;e.a=a;e.b=b;e.cross=cross;e.ray=ray;work.edges.push_back(e);
    }
    void PrepareCell() {
        work.edges.clear();work.rays.clear();work.edge=work.ray=0;
        const int n=work.cell*2;
        for(int i=0;i<2;++i)AddEdge(work.ground[2][n+i],work.ground[2][n+i+1],true,true);
        if(work.row) {
            for(int slot=0;slot<2;++slot)for(int i=0;i<2;++i)AddEdge(work.ground[slot][n+i],work.ground[slot][n+i+1],false,false);
            for(int i=0;i<3;++i)for(int slot=0;slot<2;++slot)AddEdge(work.ground[slot][n+i],work.ground[slot+1][n+i],false,i==0);
        }
    }
    void ApplyEdge(Verdict verdict,Edge& edge) {
        auto& cell=work.cells[work.cell];cell.reason|=edge.reason;
        if(verdict==Verdict::Blocked||verdict==Verdict::Unknown)Record({work.row,work.along,-kSurveyHalfWidth+work.cell,"terrain",edge.reason,
            edge.a.at,edge.b.at,{},edge.a.normal,edge.b.normal});
        if(verdict==Verdict::Blocked){cell.link=false;if(edge.cross)cell.open=false;}
        if(verdict==Verdict::Unknown){cell.link_unknown=true;if(edge.cross)cell.unknown=true;}
        if(verdict==Verdict::Clear&&edge.ray) {
            const auto add=[&](const Sample& a,const Sample& b){
                if(edge.cross) {
                    for(float h:{kChestMetres,kHeadMetres})work.rays.push_back({Above(a,h),Above(b,h),true,h==kChestMetres?obstacle_map::kChest:obstacle_map::kHead,a,b});
                } else work.rays.push_back({Above(a,kShinMetres),Above(b,kShinMetres),false,obstacle_map::kShin,a,b});
            };
            if(edge.refined){Sample a=edge.a;for(int i=0;i<4;++i){const auto& b=i<3?edge.inside[i]:edge.b;add(a,b);a=b;}}
            else add(edge.a,edge.b);
        }
    }
    void QueueCellCapsules() {
        auto& cell=work.cells[work.cell];if(!work.row||!cell.open||cell.unknown||!cell.link||cell.link_unknown)return;
        const int count=work.cell==20+work.half-1?3:2;
        for(int i=0;i<count;++i) {
            const int n=work.cell*2+i;const auto a=work.ground[0][n],m=work.ground[1][n],b=work.ground[2][n];
            const bool split=std::abs(m.at.z-(a.at.z+b.at.z)*.5F)>kGroundFollowTolerance;
            for(float h:{kCapsuleCentre,kHeadCapsuleCentre}) {
                const auto queue=[&](const Sample& from,const Sample& to){Capsule c;c.cell=work.cell;c.boundary=i==0;c.head=h==kHeadCapsuleCentre;c.from=Above(from,h);c.to=Above(to,h);c.support_from=from;c.support_to=to;work.capsules.push_back(c);};
                if(split){queue(a,m);queue(m,b);}else queue(a,b);
            }
        }
    }
    bool WorkStep(obstacle_map::Map& map) {
        if(!work.active)return false;
        // A refused/invalid query is no observation of the world. Preserve the
        // map and retry the row, instead of publishing artificial narrow gaps.
        if(work.query_failed){next_attempt[work.row]=ticks+30;work.active=false;++query_rows_deferred;return true;}
        const int node=Node(work.order);auto& sample=work.ground[std::min(work.slot,2)][node];
        switch(work.phase) {
        case Phase::CentreLoad:
            if(!Loaded(work.current.centre)){next_attempt[work.row]=ticks+kSweepPatienceFrames;work.active=false;++stalled;}
            else work.phase=Phase::GroundLoad;
            break;
        case Phase::GroundLoad:
            sample.at=Position(work.slot,node);
            if((node!=40&&!work.ground[work.slot][40].valid)||!Loaded(sample.at)){NextGround();++ground_misses;}
            else work.phase=Phase::GroundProbe;
            break;
        case Phase::GroundProbe:
            if(QueryQueue::Full())return false;
            sample=ReadGround(Position(work.slot,node),GroundSeed(work.slot,node));
            if(sample.valid)work.phase=Phase::GroundWater;else NextGround();
            break;
        case Phase::GroundWater:
            sample.water=UnderWater(sample.at.x,sample.at.y,sample.at.z);NextGround();break;
        case Phase::Cell:
            if(work.cell>=20+work.half){work.phase=Phase::Finish;break;}PrepareCell();work.phase=Phase::Edges;break;
        case Phase::Edges: {
            if(work.edge>=static_cast<int>(work.edges.size())){work.phase=Phase::Rays;break;}
            auto& e=work.edges[work.edge];const auto v=EdgeVerdict(e);
            if(v==Verdict::Refine){work.refine=0;work.phase=Phase::RefineLoad;}
            else {ApplyEdge(v,e);++work.edge;}
            break;
        }
        case Phase::RefineLoad: {
            auto& e=work.edges[work.edge];auto& p=e.inside[work.refine];p=Blend(e.a,e.b,(work.refine+1)*.25F);
            if(Loaded(p.at))work.phase=Phase::RefineProbe;
            else if(++work.refine==3){e.refined=true;work.phase=Phase::Edges;}
            break;
        }
        case Phase::RefineProbe: {
            if(QueryQueue::Full())return false;
            auto& e=work.edges[work.edge];auto& p=e.inside[work.refine];p=ReadGround(p.at,p.at.z+1.5F);
            if(p.valid)work.phase=Phase::RefineWater;
            else if(++work.refine==3){e.refined=true;work.phase=Phase::Edges;}else work.phase=Phase::RefineLoad;
            break;
        }
        case Phase::RefineWater: {
            auto& e=work.edges[work.edge];auto& p=e.inside[work.refine];p.water=UnderWater(p.at.x,p.at.y,p.at.z);
            if(++work.refine==3){e.refined=true;work.phase=Phase::Edges;}else work.phase=Phase::RefineLoad;
            break;
        }
        case Phase::Rays: {
            if(work.ray>=static_cast<int>(work.rays.size())){work.phase=Phase::Capsules;break;}
            if(QueryQueue::Full())return false;
            const auto& q=work.rays[work.ray++];auto& cell=work.cells[work.cell];
            if((q.cross&&(!cell.open||cell.unknown))||(!q.cross&&(!cell.link||cell.link_unknown)))break;
            ++probes;const auto p=Line(q.from,q.to,kProbeMask);
            const auto contact=p.hit?ClassifyContact(p.at,p.normal,q.support_from,q.support_to,0):Contact::Unknown;
            const auto why=p.status!=2?obstacle_map::kQueryUnknown:(p.traffic?obstacle_map::kTraffic:
                (contact==Contact::Unknown?obstacle_map::kQueryUnknown:(contact==Contact::Supporting?obstacle_map::kGroundUnknown:q.why)));
            if(p.status!=2||p.hit||p.traffic)Record({work.row,work.along,-kSurveyHalfWidth+work.cell,"ray",why,
                q.from,q.to,p.at,p.normal,{},p.status,p.entity});
            if(p.status!=2){++ray_query_failures;work.query_failed=true;if(p.pending)QueryQueue::Track(p.pending);}
            else if(p.traffic||(p.hit&&contact!=Contact::Blocked)) {
                cell.link_unknown=true;if(q.cross)cell.unknown=true;cell.reason|=why;
                if(p.traffic)++transient_contacts;else if(contact==Contact::Supporting)++supporting_contacts;
                if(!q.cross&&work.cell>0){work.cells[work.cell-1].link_unknown=true;work.cells[work.cell-1].reason|=why;}
            }
            else if(p.hit) {
                cell.link=false;if(q.cross)cell.open=false;cell.reason|=q.why;
                // A boundary ray touches both adjacent cells. Keep the local
                // exclusion, not a first-hit wall through all outer cells.
                if(!q.cross&&work.cell>0){work.cells[work.cell-1].link=false;work.cells[work.cell-1].reason|=q.why;}
            }break;
        }
        case Phase::Capsules:QueueCellCapsules();++work.cell;work.phase=Phase::Cell;break;
        case Phase::Finish: {
            PendingRow pending;pending.row=work.row;pending.born=ticks;pending.cells=work.cells;
            pending.capsules=std::move(work.capsules);pending.remaining=static_cast<int>(pending.capsules.size());
            pending.centre_valid=work.ground[2][40].valid;
            pending.half=work.half;
            pending.along=work.along;
            in_flight.push_back(std::move(pending));work.active=false;break;
        }
        }
        return true;
    }
    void UnknownCapsule(PendingRow& p,Capsule& c) {
        Record({p.row,p.along,-kSurveyHalfWidth+c.cell,"capsule",obstacle_map::kQueryUnknown,c.from,c.to,{},{},{},1});
        if(c.handle){c.handle=0;c.query.reset();--pending_capsules;}
        c.resolved=true;--p.remaining;++capsules_dropped;p.query_failed=true;
    }
    bool DrainStep() {
        if(poll_remaining<=0)return false;--poll_remaining;return QueryQueue::PollOne();
    }
    bool IssueStep(int& issued) {
        if(in_flight.empty()||issued>=kCapsulesPerFrame||issue_remaining<=0)return false;
        if(refused_tick==ticks||QueryQueue::Full()||!QueryQueue::CanIssue())return false;
        issue_row%=in_flight.size();auto& p=in_flight[issue_row];
        if(issue_cap>=p.capsules.size()){issue_cap=0;issue_row=(issue_row+1)%in_flight.size();return true;}
        auto& c=p.capsules[issue_cap++];--issue_remaining;if(c.resolved||c.handle)return true;
        // Never issue a sweep for a cell a later boundary ray already blocked.
        if(p.query_failed||!p.cells[c.cell].link||p.cells[c.cell].link_unknown){c.resolved=true;--p.remaining;return true;}
        if(c.load_phase<2) {
            --issue_cap;++issue_remaining;
            if(!Loaded(c.load_phase?c.to:c.from))UnknownCapsule(p,c);else ++c.load_phase;
            return true;
        }
        ++issued;QueryQueue::Issued();c.handle=StartCapsuleProbe(c.from,c.to);
        if(c.handle){c.query=QueryQueue::Track(c.handle);++poll_remaining;c.issued_at=ticks;++capsules;++pending_capsules;peak_pending_capsules=std::max(peak_pending_capsules,pending_capsules);}
        else {++capsules_refused;--issue_cap;refused_tick=ticks;
            Record({p.row,p.along,-kSurveyHalfWidth+c.cell,"capsule",obstacle_map::kQueryUnknown,c.from,c.to,{},{},{},0});}return true;
    }
    int Publish(obstacle_map::Map& map,PendingRow& p) {
        if(p.query_failed){next_attempt[p.row]=ticks+30;++query_rows_deferred;return 0;}
        obstacle_map::Row row;row.detailed=true;row.connections_detailed=true;row.connection_radius=kCapsuleRadius;
        for(int i=0;i<kCells;++i) {
            const auto c=p.cells[i];const obstacle_map::Interval span{-kSurveyHalfWidth+static_cast<float>(i),-kSurveyHalfWidth+static_cast<float>(i+1)};
            if(c.open&&!c.unknown)row.intervals.push_back(span);
            if(p.row&&c.open&&!c.unknown&&c.link&&!c.link_unknown)row.connections.push_back(span);
            if(c.unknown||c.link_unknown){row.unknown.push_back(span);if(i>=20-p.half&&i<20+p.half)++unknown_cells;else ++outside_cells;}
            row.reason|=c.reason;
        }
        row.intervals=obstacle_map::Normalize(std::move(row.intervals));row.connections=obstacle_map::Normalize(std::move(row.connections));
        row.unknown=obstacle_map::Normalize(std::move(row.unknown));row.RefreshWidths();
        if(!p.centre_valid&&row.intervals.empty()){next_attempt[p.row]=ticks+kSweepPatienceFrames;++stalled;return 0;}
        // ReplaceRow merges the CURRENT explicit impact/passage overlays.
        // Intersecting with the old row's whole clear set would fossilize
        // unrelated probe holes whenever any one local impact was learned.
        next_attempt[p.row]=ticks+kSweepPatienceFrames;
        const bool changed=map.native_mode?map.ObserveProbeRow(p.row,row,static_cast<float>(p.half)):
            map.ReplaceRow(p.row,std::move(row));
        if(changed){++rows;changed_rows.push_back(p.row);return 1;}return 0;
    }
    bool ResolveStep(obstacle_map::Map& map,int& committed) {
        if(in_flight.empty()||resolve_done)return false;
        if(resolve_row>=in_flight.size()){resolve_row=resolve_cap=0;resolve_done=true;return false;}auto& p=in_flight[resolve_row];
        if(p.remaining==0) {
            committed+=Publish(map,p);in_flight.erase(in_flight.begin()+static_cast<std::ptrdiff_t>(resolve_row));
            resolve_cap=0;issue_cap=0;return true;
        }
        if(resolve_cap>=p.capsules.size()){resolve_cap=0;++resolve_row;return true;}
        auto& c=p.capsules[resolve_cap++];if(c.resolved||!c.handle)return true;
        const auto result=*c.query;const int status=result.status;const auto hit=result.hit;
        const auto end=result.at,normal=result.normal;const auto entity=result.entity;
        if(status==1){if(ticks-c.issued_at>kSweepPatienceFrames)UnknownCapsule(p,c);return true;}
        c.resolved=true;c.handle=0;c.query.reset();--p.remaining;--pending_capsules;
        const bool traffic=status==2&&hit&&Transient(entity);
        const auto contact=status==2&&hit&&!traffic?ClassifyContact(end,normal,c.support_from,c.support_to,kCapsuleRadius):Contact::Unknown;
        const auto why=status!=2?obstacle_map::kQueryUnknown:(traffic?obstacle_map::kTraffic:
            (contact==Contact::Supporting?obstacle_map::kGroundUnknown:(contact==Contact::Unknown?obstacle_map::kQueryUnknown:
                (c.head?obstacle_map::kHeadCapsule:obstacle_map::kLowCapsule))));
        if(status!=2||hit)Record({p.row,p.along,-kSurveyHalfWidth+c.cell,"capsule",why,
            c.from,c.to,end,normal,{},status,entity});
        if(status!=2){++capsule_query_failures;p.query_failed=true;}
        else if(hit) {
            if(traffic||contact!=Contact::Blocked) {
                p.cells[c.cell].link_unknown=true;p.cells[c.cell].reason|=why;
                if(traffic)++transient_contacts;else if(contact==Contact::Supporting)++supporting_contacts;
                if(c.boundary&&c.cell>0){p.cells[c.cell-1].link_unknown=true;p.cells[c.cell-1].reason|=why;}
            }
            else {
                ++capsules_hit;const auto why=c.head?obstacle_map::kHeadCapsule:obstacle_map::kLowCapsule;
                p.cells[c.cell].link=false;p.cells[c.cell].reason|=why;
                if(c.boundary&&c.cell>0){p.cells[c.cell-1].link=false;p.cells[c.cell-1].reason|=why;}
            }
        }
        return true;
    }
    int Tick(obstacle_map::Map& map,const course::Path& path,const std::vector<Vector3>& anchors,double budget_ms=kFrameBudgetMs,float half_width=kSurveyHalfWidth) {
        using clock=std::chrono::steady_clock;const auto started=clock::now();
        changed_rows.clear();
        const QueryQueue::Charge charge{started};
        const auto spent=[&]{return std::chrono::duration<double,std::milli>(clock::now()-started).count();};
        if(map.rows.empty()||path.points.size()<2||anchors.empty()||!(budget_ms>0))return 0;
        const double budget=QueryQueue::RemainingBudget(budget_ms);if(!(budget>0))return 0;
        if(bound_key!=map.key||bound_geometry!=map.geometry_hash||next_attempt.size()!=map.rows.size()) {
            Reset();bound_key=map.key;bound_geometry=map.geometry_hash;next_attempt.assign(map.rows.size(),0);last_selected.assign(map.rows.size(),0);
        }
        const float requested=std::isfinite(half_width)&&half_width>0?std::clamp(std::ceil(half_width),1.0F,kSurveyHalfWidth):kSurveyHalfWidth;
        if(requested!=required_width){idle_until=0;scan_cursor=0;have_best=false;best_distance=INFINITY;}
        required_width=requested;
        ++ticks;resolve_done=false;issue_remaining=0;poll_remaining=static_cast<int>(QueryQueue::Total());
        for(const auto& p:in_flight)issue_remaining+=static_cast<int>(p.capsules.size());
        int issued=0,committed=0,performed=0,idle=0;double predicted=0;
        while(spent()+std::min(predicted,budget*.5)<budget) {
            const auto step_start=clock::now();bool did=false;
            switch(turn++%5) {
            case 0:did=DrainStep();break;
            case 1:did=ResolveStep(map,committed);break;
            case 2:did=IssueStep(issued);break;
            case 3:did=WorkStep(map);break;
            case 4:did=SelectStep(map,path,anchors);break;
            }
            if(did) {
                predicted=std::chrono::duration<double,std::milli>(clock::now()-step_start).count();
                max_step_ms=std::max(max_step_ms,predicted);++performed;++steps;idle=0;
            } else if(++idle>=5)break;
        }
        const double elapsed=spent();if(performed){++frames;ms_total+=elapsed;ms_max=std::max(ms_max,elapsed);if(elapsed>budget)++budget_overruns;}
        else ++stalled;return committed;
    }
    int TickNearPlayer(obstacle_map::Map& map,const course::Path& path,Ped player,float half_width) {
        const auto started=std::chrono::steady_clock::now();
        if(!(QueryQueue::RemainingBudget(kFrameBudgetMs)>0)||!player||!ENTITY::DOES_ENTITY_EXIST(player))return 0;
        const auto anchor=ENTITY::GET_ENTITY_COORDS(player,true,false);
        const bool loaded=destination::Finite(anchor)&&Loaded(anchor);
        QueryQueue::Debit(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
        // Only the player's current loaded region supplies an anchor.
        // Every floor point and body sweep also checks current world loading.
        return loaded?Tick(map,path,{anchor},kOnlineBudgetMs,half_width):0;
    }
};
} // namespace horse_races::obstacle_survey
