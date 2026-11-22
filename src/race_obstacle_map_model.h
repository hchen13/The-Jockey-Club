#pragma once
#include "race_course.h"
#include "race_obstacle_intervals.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Observations at their actual course cross sections. A local obstacle does
// not imply that every lateral point beyond it is blocked.
namespace horse_races::obstacle_map {
inline constexpr float kStepMetres=3,kUnknown=-1;
inline constexpr int kDetailedVersion=7;
inline constexpr int kAxisEvidenceVersion=8;
inline constexpr int kProbeEvidenceVersion=9;
inline constexpr int kProbeLineageEvidenceVersion=10;
inline constexpr float kMaxConnectionRadius=.5F;
// Game-thread barrier for still-pending passage observations. A collision can
// finish its asynchronous query after a safe sample was taken; that older
// sample must not clear the new impact when its settling delay expires.
// Only physical impacts advance it. It is deliberately not persisted.
inline std::uint64_t physical_impact_epoch=0;
enum Reason : std::uint32_t {
    kNone=0,kGroundUnknown=1u<<0,kStep=1u<<1,kSteep=1u<<2,kWater=1u<<3,
    kChest=1u<<4,kHead=1u<<5,kShin=1u<<6,kLowCapsule=1u<<7,kHeadCapsule=1u<<8,
    kQueryUnknown=1u<<9,kTraffic=1u<<10,kImpact=1u<<11,kLegacy=1u<<12,kPassage=1u<<13
};
struct Row {
    // Kept first for old aggregate initializers. Only the centre-connected
    // interval is described here; outer free islands remain in intervals.
    float left=kUnknown,right=kUnknown;unsigned char confirmed=0;
    std::vector<Interval> intervals,connections,unknown;
    bool detailed=false,connections_detailed=false,legacy=false;
    std::uint32_t reason=kNone;
    float connection_radius=0; // Body radius already verified by the sweep.
    // Independent evidence layers. A survey snapshot must neither erase a
    // successful ride nor promote all its unrelated holes to collision proof.
    std::vector<Interval> passed,impacts,connection_impacts;
    // Authored world obstacles are immutable, ephemeral course data, not evidence.
    std::vector<Interval> authored_sections,authored_links;
    float authored_link_min_normal=1;
    std::vector<Passage> passages;
    // Immutable native geometry certificates, already measured for the
    // row's connection_radius. They are never written as riding evidence.
    std::vector<Passage> native_strips;
    // v8 successful rides certify centre axes. Older v7 strips describe
    // physical footprints and retain their historical interpretation.
    bool passage_axes=false;
    // Live loaded-world probes measure physical room and swept centre axes.
    // Keep them separate from both the native base and ridden centre axes.
    float probe_half_width=0;
    std::vector<Interval> probe_intervals,probe_connections,probe_unknown;
    bool Surveyed() const{return left>=0&&right>=0;}
    bool HasDetailed() const{return detailed;}
    bool HasConnections() const{return connections_detailed||probe_half_width>0;}
    // Physical measured room excludes already-certified actual centre axes.
    std::vector<Interval> MeasuredIntervals() const {
        auto measured=detailed?(probe_intervals.empty()?intervals:Union(intervals,probe_intervals)):
            Surveyed()?std::vector<Interval>{{-right,left}}:std::vector<Interval>{};
        return SubtractAll(std::move(measured),impacts);
    }
    std::vector<Interval> PassedIntervals() const {return SubtractAll(passed,impacts);}
    std::vector<Interval> ClearIntervals(float clearance=0) const {
        if(!std::isfinite(clearance)||clearance<0)return {};
        if(detailed) {
            const auto base=MeasuredIntervals();
            // ConfirmSection removes an older impact before adding its safe
            // axis. A remaining direct overlap is stale and keeps the impact.
            // Do not inflate that subtraction: adjacent impact halos must not
            // revoke the body clearance already proven by the new ride.
            const auto safe_passed=PassedIntervals();
            return SubtractAll(passage_axes?Union(Erode(base,clearance),safe_passed):Erode(Union(base,safe_passed),clearance),authored_sections,clearance);
        }
        return SubtractAll(Erode(MeasuredIntervals(),clearance),authored_sections,clearance);
    }
    // Unknown is not a physical negative. Only a detailed native complement
    // of Clear+Unknown and actual impacts certify blocked space. Live probes
    // add positives; their unsampled complement proves nothing. Resolve in
    // body-centre units, then let effective actual passage axes supersede old
    // negatives exactly as ClearIntervals does, without joining free islands.
    std::vector<Interval> BlockedIntervals(float width,float clearance,bool native,
        const std::vector<Interval>& clear) const {
        if(!std::isfinite(clearance)||clearance<0)return {};
        std::vector<Interval> physical;
        if(native&&detailed&&!legacy&&std::isfinite(width)&&width>0) {
            const auto admitted=Union(Union(intervals,unknown),Union(passed,probe_intervals));
            physical=SubtractAll({{-width,width}},admitted);
        }
        physical=Union(std::move(physical),impacts);
        for(auto& span:physical){span.lo-=clearance;span.hi+=clearance;}
        auto result=SubtractAll(Normalize(std::move(physical)),clear);
        auto fixed=authored_sections;for(auto& span:fixed){span.lo-=clearance;span.hi+=clearance;}
        return Union(std::move(result),fixed);
    }
    std::vector<Interval> BlockedIntervals(float width,float clearance,bool native) const {
        return BlockedIntervals(width,clearance,native,ClearIntervals(clearance));
    }
    std::vector<Interval> ClearConnections(float clearance=0) const {
        if(!HasConnections()||!std::isfinite(clearance)||clearance<0||!std::isfinite(connection_radius)||
            connection_radius<0||connection_radius>kMaxConnectionRadius)return {};
        // The API takes TOTAL physical clearance, just like ClearIntervals.
        // Certified axis positions already include connection_radius of body.
        auto out=Erode(SubtractAll(connections,connection_impacts,connection_radius),std::max(0.0F,clearance-connection_radius));
        if(!probe_connections.empty())out=Union(std::move(out),Erode(SubtractAll(probe_connections,connection_impacts,kMaxConnectionRadius),
            std::max(0.0F,clearance-kMaxConnectionRadius)));
        return SubtractAll(std::move(out),authored_links,clearance/std::max(.01F,authored_link_min_normal));
    }
    void RefreshWidths() {
        if(!detailed)return;left=right=0;
        for(auto v:ClearIntervals())if(v.lo<=0&&v.hi>=0){left=v.hi;right=-v.lo;break;}
    }
    bool operator==(const Row&) const=default;
    void BeginAxisEvidence() {
        if(!passage_axes){passed.clear();passages.clear();passage_axes=true;}
    }
    // A new hit invalidates the older ride's body footprint, never policy reserve.
    void RevokePassage(float lo,float hi,bool section) {
        const float body=passage_axes?kMaxConnectionRadius:0;
        if(section)passed=Subtract(passed,lo-body,hi+body);
        std::erase_if(passages,[&](const Passage& p){
            if(!std::isfinite(p.min_normal)||p.min_normal<.01F||p.min_normal>1.001F)return true;
            const float grow=body/p.min_normal;return Touches(p,lo-grow,hi+grow);});
    }
};
struct Section {Vector3 centre{};float nx=0,ny=0;};
struct Map {
    std::string key,geometry_hash;float length=0,width=0;int version=0,loaded_version=0;
    bool loaded_legacy=false,geometry_mismatch=false;std::uint64_t revision=0;
    bool native_mode=false,native_loaded=false;std::string native_source;
    std::string native_fingerprint;bool evidence_probe_stale=false;
    std::vector<Row> rows;std::vector<unsigned char> suspect;
    // Ephemeral section frames cached from the original recording. Canonical
    // maps use the uniform 3 m grid; expanded races also carry translated s.
    std::vector<float> row_metres;
    std::vector<Section> sections;
    // Script-thread stamps let persistence copy only a bounded number of
    // evidence rows per frame, without lending the worker a mutable map.
    std::vector<std::uint64_t> row_revisions;
    std::uint64_t all_rows_revision=0,evidence_epoch=0;
    static std::string KeyFor(const std::string& station_key,const course::Path& path) {
        char buffer[96];std::snprintf(buffer,sizeof(buffer),"%s-%zu-%.0f",station_key.c_str(),path.points.size(),static_cast<double>(path.Length()));return buffer;
    }
    static std::string GeometryHash(const course::Path& path) {
        std::uint64_t hash=14695981039346656037ULL;
        for(auto p:path.points)for(float v:{p.x,p.y,p.z}){hash^=std::bit_cast<std::uint32_t>(v);hash*=1099511628211ULL;}
        char out[24];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(hash));return out;
    }
    static std::string NativeFingerprint(const std::string& bytes) {
        std::uint64_t hash=14695981039346656037ULL;
        for(const unsigned char value:bytes){hash^=value;hash*=1099511628211ULL;}
        char out[24];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(hash));return out;
    }
    static Vector3 At(const course::Path& path,float along) {
        if(path.points.size()<2)return {};
        along=std::clamp(along,0.0F,path.Length());const auto i=path.Segment(along);
        const float t=(along-path.metres[i])/(path.metres[i+1]-path.metres[i]);const auto a=path.points[i],b=path.points[i+1];
        return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    }
    static Section SectionAt(const course::Path& path,float along) {
        Section out;out.centre=At(path,along);const auto a=At(path,along-2),b=At(path,along+2);
        const float len=std::hypot(b.x-a.x,b.y-a.y);if(len>.01F){out.nx=-(b.y-a.y)/len;out.ny=(b.x-a.x)/len;}return out;
    }
    float Along(std::size_t row) const {
        return row_metres.size()==rows.size()&&row<row_metres.size()?row_metres[row]:static_cast<float>(row)*kStepMetres;
    }
    Section SectionFor(const course::Path& path,std::size_t row) const {
        return sections.size()==rows.size()&&row<sections.size()?sections[row]:SectionAt(path,Along(row));
    }
    bool Bind(const std::string& station_key,const course::Path& path,float survey_width,int survey_version=1) {
        if(path.points.size()<2||!(path.Length()>0)||!(survey_width>0))return false;
        const auto next=KeyFor(station_key,path),hash=GeometryHash(path);
        if(next==key&&geometry_hash==hash&&rows.size()==Rows(path.Length())&&width==survey_width&&version==survey_version)return true;
        key=next;geometry_hash=hash;length=path.Length();width=survey_width;version=survey_version;
        loaded_version=0;loaded_legacy=false;geometry_mismatch=false;native_mode=false;native_loaded=false;native_source.clear();row_metres.clear();sections.clear();
        native_fingerprint.clear();evidence_probe_stale=false;
        rows.assign(Rows(length),Row{});suspect.assign(rows.size(),0);MarkChanged();return true;
    }
    static std::size_t Rows(float length){return static_cast<std::size_t>(length/kStepMetres)+1;}
    std::size_t Index(float along) const {
        if(rows.empty()||!(along>0))return 0;
        if(row_metres.size()==rows.size()) {
            const auto it=std::upper_bound(row_metres.begin(),row_metres.end(),along);
            return std::min(rows.size()-1,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-row_metres.begin()-1)));
        }
        return std::min(rows.size()-1,static_cast<std::size_t>(along/kStepMetres));
    }
    void MarkChanged(){all_rows_revision=++revision;}
    std::uint64_t RowRevision(std::size_t index) const {
        return std::max(all_rows_revision,index<row_revisions.size()?row_revisions[index]:0);
    }
    bool StoreRow(std::size_t index,Row row) {
        if(index>=rows.size()||rows[index]==row)return false;
        rows[index]=std::move(row);
        if(row_revisions.size()!=rows.size())row_revisions.resize(rows.size());
        row_revisions[index]=++revision;return true;
    }
    bool ReplaceRow(std::size_t index,Row row) {
        if(index>=rows.size())return false;
        const auto& old=rows[index];
        row.passed=old.passed;row.passages=old.passages;
        row.passage_axes=old.passage_axes;
        row.probe_half_width=old.probe_half_width;row.probe_intervals=old.probe_intervals;
        row.probe_connections=old.probe_connections;row.probe_unknown=old.probe_unknown;
        row.impacts=old.impacts;row.connection_impacts=old.connection_impacts;
        row.authored_sections=old.authored_sections;row.authored_links=old.authored_links;row.authored_link_min_normal=old.authored_link_min_normal;
        row.reason|=old.reason&(kImpact|kPassage);
        row.unknown=SubtractAll(row.unknown,row.passed);
        row.unknown=SubtractAll(row.unknown,row.impacts);
        row.RefreshWidths();return StoreRow(index,std::move(row));
    }
    bool NeedsSurvey(std::size_t index,float required_half_width=1e9F) const {
        if(index>=rows.size())return false;const auto& row=rows[index];
        if(native_mode&&row.probe_half_width>0) {
            if(row.probe_half_width<std::min(width,required_half_width))return true;
            for(auto v:row.probe_unknown)if(v.hi>-required_half_width+1e-5F&&v.lo<required_half_width-1e-5F)return true;
            return false;
        }
        if(!row.HasDetailed()||row.legacy)return true;
        if(index&&!row.HasConnections())return true;
        for(auto v:row.unknown)if(v.hi>-required_half_width+1e-5F&&v.lo<required_half_width-1e-5F)return true;
        return false;
    }
    bool ObserveProbeRow(std::size_t index,const Row& observed,float half_width) {
        if(index>=rows.size()||!std::isfinite(half_width)||half_width<=0||half_width>width||!observed.detailed)return false;
        auto row=rows[index];
        auto permitted=std::vector<Interval>{{-width,width}};
        if(native_mode&&row.HasDetailed())permitted=Union(Union(row.intervals,row.unknown),Union(row.passed,row.probe_intervals));
        if(!row.detailed){row.intervals=row.ClearIntervals();row.unknown={{-width,width}};row.detailed=true;}
        // A live positive can resolve unknown native space, never revoke a
        // known native obstruction or a newer actual collision overlay.
        row.probe_intervals=Union(std::move(row.probe_intervals),Intersect(observed.intervals,permitted));
        row.probe_connections=Union(std::move(row.probe_connections),Intersect(observed.connections,permitted));
        row.probe_unknown=Union(Subtract(row.probe_unknown,-half_width,half_width),
            Intersect(observed.unknown,{{-half_width,half_width}}));
        row.probe_half_width=std::max(row.probe_half_width,half_width);row.RefreshWidths();
        return StoreRow(index,std::move(row));
    }
    std::size_t Surveyed() const{std::size_t n=0;for(const auto& r:rows)n+=r.Surveyed();return n;}
    struct ConnectionCoverage {
        std::size_t detailed_rows=0,missing_incoming=0,detailed_missing_incoming=0,measured_incoming=0,measured_empty=0,usable_incoming=0;
    };
    ConnectionCoverage CountConnections() const {
        ConnectionCoverage out;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& row=rows[i];if(row.HasDetailed())++out.detailed_rows;
            if(!i)continue;
            if(row.HasConnections()) {
                ++out.measured_incoming;
                // Empty measured links may include unknown terrain; this is
                // evidence availability, not a claim of confirmed blockage.
                if(row.connections.empty()&&row.native_strips.empty())++out.measured_empty;
                if(!ConnectionStrips(i).empty())++out.usable_incoming;
            } else {
                ++out.missing_incoming;
                if(row.HasDetailed())++out.detailed_missing_incoming;
            }
        }
        return out;
    }
    float Coverage() const{return rows.empty()?0:static_cast<float>(Surveyed())/static_cast<float>(rows.size());}
    // Lateral is measured in SectionFor(path, Index(along)),
    // not in a normal interpolated at the caller's arbitrary along value.
    std::vector<Interval> IntervalsAt(float along,float clearance=0) const {
        return rows.empty()?std::vector<Interval>{}:rows[Index(along)].ClearIntervals(clearance);
    }
    bool Allowed(float along,float lateral,float clearance=0) const{return Contains(IntervalsAt(along,clearance),lateral);}
    std::vector<Interval> ConnectionIntervals(std::size_t to_row,float clearance=0) const {
        if(!to_row||to_row>=rows.size())return {};const auto& to=rows[to_row];const auto& from=rows[to_row-1];
        if(to.HasConnections())return to.ClearConnections(clearance);
        if(!from.Surveyed()||!to.Surveyed())return {};
        // Compatibility is explicit. A new unavailable connection is closed,
        // and no unknown row can manufacture a measured connection.
        if(from.HasDetailed()||to.HasDetailed())return {};
        return Intersect(from.ClearIntervals(clearance),to.ClearIntervals(clearance));
    }
    std::vector<Passage> ConnectionStrips(std::size_t row,float clearance=0) const {
        std::vector<Passage> out;if(!row||row>=rows.size()||!std::isfinite(clearance)||clearance<0)return out;
        const auto finish=[&](std::vector<Passage> candidates) {
            for(const auto block:rows[row].authored_links) {
                std::vector<Passage> next;
                const float grow=clearance/std::max(.01F,rows[row].authored_link_min_normal);
                const float lo=block.lo-grow,hi=block.hi+grow;
                for(auto part:candidates) {
                    if(!Touches(part,lo,hi)){next.push_back(part);continue;}
                    auto left=part,right=part;
                    left.from.hi=std::min(left.from.hi,lo);left.to.hi=std::min(left.to.hi,lo);
                    right.from.lo=std::max(right.from.lo,hi);right.to.lo=std::max(right.to.lo,hi);
                    if(left.from.lo<=left.from.hi&&left.to.lo<=left.to.hi)next.push_back(left);
                    if(right.from.lo<=right.from.hi&&right.to.lo<=right.to.hi)next.push_back(right);
                }
                candidates=std::move(next);
            }
            return candidates;
        };
        for(auto span:ConnectionIntervals(row,clearance))out.push_back({span,span});
        for(auto p:rows[row].native_strips) {
            if(!std::isfinite(p.min_normal)||p.min_normal<.01F)continue;
            const float inset=std::max(0.0F,clearance-rows[row].connection_radius)/p.min_normal;
            p.from.lo+=inset;p.from.hi-=inset;p.to.lo+=inset;p.to.hi-=inset;
            if(p.from.lo>p.from.hi||p.to.lo>p.to.hi)continue;
            std::vector<Passage> pieces{p};
            for(const auto impact:rows[row].connection_impacts) {
                std::vector<Passage> remainder;
                const float lo=impact.lo-std::max(clearance,rows[row].connection_radius);
                const float hi=impact.hi+std::max(clearance,rows[row].connection_radius);
                for(auto part:pieces) {
                    if(!Touches(part,lo,hi)){remainder.push_back(part);continue;}
                    auto left=part,right=part;
                    left.from.hi=std::min(left.from.hi,lo);left.to.hi=std::min(left.to.hi,lo);
                    right.from.lo=std::max(right.from.lo,hi);right.to.lo=std::max(right.to.lo,hi);
                    if(left.from.lo<=left.from.hi&&left.to.lo<=left.to.hi)remainder.push_back(left);
                    if(right.from.lo<=right.from.hi&&right.to.lo<=right.to.hi)remainder.push_back(right);
                }
                pieces=std::move(remainder);
            }
            for(auto piece:pieces)AddPassage(out,piece);
        }
        if(rows[row].passage_axes) {
            // Canonical Block/BlockConnection remove touched older axes.
            // Retained actual axes therefore supersede old link-impact halos;
            // ConnectionBlocker resolves that same rule for negative queries.
            for(const auto& p:rows[row].passages)AddPassage(out,p);
            return finish(std::move(out));
        }
        std::vector<Passage> physical;float normal=1;
        const auto append=[&](Passage p) {
            const float inset=clearance/p.min_normal;
            p.from.lo+=inset;p.from.hi-=inset;p.to.lo+=inset;p.to.hi-=inset;
            if(p.from.lo>p.from.hi||p.to.lo>p.to.hi)return;
            for(const auto& old:out)if(old.from==p.from&&old.to==p.to)return;
            out.push_back(p);
        };
        // The recorded strips store physical room, not centre-axis positions;
        // join compatible physical observations BEFORE applying full margin.
        // The per-row normal lower bound is shared by these same two frames.
        for(auto p:rows[row].passages) {
            if(!std::isfinite(p.min_normal)||p.min_normal<.01F)continue;
            normal=std::min(normal,p.min_normal);append(p);AddPassage(physical,p);
        }
        if(physical.empty())return finish(std::move(out));
        // Native connections certify centre axes plus their recorded capsule
        // radius. That already-measured footprint may complete a real ride's
        // neighbouring room; an unmeasured gap never receives a new radius.
        // Keep original native outputs above as well: a conservative bend
        // bound used for the joined evidence cannot erase native clearance.
        if(rows[row].HasConnections())for(auto span:rows[row].ClearConnections()) {
            const float radius=rows[row].connection_radius;
            const Interval occupied{std::max(-width,span.lo-radius),std::min(width,span.hi+radius)};
            if(occupied.lo<=occupied.hi)AddPassage(physical,{occupied,occupied,normal});
        }
        for(auto p:physical)append(p);
        return finish(std::move(out));
    }
    bool ConnectionAllowed(std::size_t to_row,float from_lateral,float to_lateral,float clearance=0) const {
        if(!to_row||to_row>=rows.size()||!Contains(rows[to_row-1].ClearIntervals(clearance),from_lateral)||!Contains(rows[to_row].ClearIntervals(clearance),to_lateral))return false;
        for(auto p:ConnectionStrips(to_row,clearance))if(from_lateral>=p.from.lo-1e-5F&&from_lateral<=p.from.hi+1e-5F&&to_lateral>=p.to.lo-1e-5F&&to_lateral<=p.to.hi+1e-5F)return true;return false;
    }
    bool ConfirmSection(std::size_t index,Interval span) {
        if(index>=rows.size()||!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>=span.hi||span.lo< -width||span.hi>width)return false;
        auto row=rows[index];if(!row.detailed){row.intervals=row.ClearIntervals();row.unknown={{-width,width}};row.detailed=true;}
        row.BeginAxisEvidence();
        row.passed=Union(row.passed,{span});row.impacts=Subtract(row.impacts,span.lo,span.hi);
        row.unknown=Subtract(row.unknown,span.lo,span.hi);row.reason|=kPassage;
        row.RefreshWidths();return StoreRow(index,std::move(row));
    }
    bool ConfirmConnection(std::size_t index,Passage p) {
        if(!index||index>=rows.size()||!std::isfinite(p.min_normal)||p.min_normal<.01F||p.min_normal>1.001F)return false;
        for(auto span:{p.from,p.to})if(!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>=span.hi||span.lo< -width||span.hi>width)return false;
        auto row=rows[index];if(!row.detailed){row.intervals=row.ClearIntervals();row.unknown={{-width,width}};row.detailed=true;}
        row.BeginAxisEvidence();
        AddPassage(row.passages,p);
        const float lo=std::max(p.from.lo,p.to.lo),hi=std::min(p.from.hi,p.to.hi);
        if(lo<hi)row.connection_impacts=Subtract(row.connection_impacts,lo,hi);
        row.reason|=kPassage;row.RefreshWidths();return StoreRow(index,std::move(row));
    }
    bool Block(std::size_t index,float lo,float hi,std::uint32_t why=kImpact) {
        if(index>=rows.size()||!std::isfinite(lo)||!std::isfinite(hi))return false;
        if(lo>hi)std::swap(lo,hi);
        lo=std::max(-width,lo);hi=std::min(width,hi);if(lo>=hi)return false;
        ++physical_impact_epoch;
        auto row=rows[index];
        if(!row.Surveyed()) {
            row.detailed=true;row.unknown={{-width,width}};row.RefreshWidths();
        }
        if(row.HasDetailed()) {
            // The read-only native base remains intact. The negative layer
            // is applied at query time and a later real ride can revoke it.
            row.unknown=Subtract(row.unknown,lo,hi);row.RefreshWidths();
        } else {
            if(lo>=0)row.left=std::min(row.left,lo);else if(hi<=0)row.right=std::min(row.right,-hi);else row.left=row.right=0;row.legacy=true;
        }
        row.impacts=Union(row.impacts,{{lo,hi}});row.connection_impacts=Union(row.connection_impacts,{{lo,hi}});
        row.RevokePassage(lo,hi,true);
        row.reason|=why;row.RefreshWidths();return StoreRow(index,std::move(row));
    }
    bool BlockConnection(std::size_t index,float lo,float hi,std::uint32_t why=kImpact) {
        if(index>=rows.size()||!std::isfinite(lo)||!std::isfinite(hi))return false;
        if(lo>hi)std::swap(lo,hi);
        lo=std::max(-width,lo);hi=std::min(width,hi);if(lo>=hi)return false;
        ++physical_impact_epoch;
        auto row=rows[index];
        if(!row.HasConnections()) {
            // Negative evidence is not an incoming clearance certificate.
            // Preserve known section room, but leave the strip unresolved.
            if(!row.HasDetailed())row.intervals=row.ClearIntervals();
            row.detailed=true;
            if(!(row.reason&kImpact)||row.unknown.empty())row.unknown={{-width,width}};
            row.unknown=Subtract(row.unknown,lo,hi);row.RefreshWidths();
        }
        row.connection_impacts=Union(row.connection_impacts,{{lo,hi}});
        row.RevokePassage(lo,hi,false);
        row.reason|=why;return StoreRow(index,std::move(row));
    }
    Row Narrowest(float from,float to) const {
        Row out{0,0};if(rows.empty())return out;if(from>to)std::swap(from,to);const auto a=Index(from),b=Index(to);out={1e9F,1e9F};
        for(std::size_t r=a;r<=b;++r){const auto& row=rows[r];out.left=std::min(out.left,row.left<0?0.0F:row.left);out.right=std::min(out.right,row.right<0?0.0F:row.right);}return out;
    }
    float Free(float along,float lateral) const {
        if(rows.empty())return 0;const auto& row=rows[Index(along)];const float f=lateral>=0?row.left:row.right;return f<0?0:f;
    }
    bool Isolated(std::size_t r,float margin=5) const {
        if(r==0||r+1>=rows.size())return false;const auto& a=rows[r-1];const auto& b=rows[r];const auto& c=rows[r+1];
        if(!a.Surveyed()||!b.Surveyed()||!c.Surveyed())return false;
        return (a.left-b.left>margin&&c.left-b.left>margin)||(a.right-b.right>margin&&c.right-b.right>margin);
    }
    std::size_t ForgetIsolated(float margin=5) {
        std::vector<std::size_t> forget;
        for(std::size_t r=1;r+1<rows.size();++r)if(!rows[r].HasDetailed()&&Isolated(r,margin)&&rows[r].confirmed==0)forget.push_back(r);
        if(suspect.size()!=rows.size())suspect.assign(rows.size(),0);
        for(auto r:forget){rows[r]={};suspect[r]=1;MarkChanged();}return forget.size();
    }
    std::string SerializeEvidence() const;
    bool ParseEvidence(const std::string& text);
    std::string Serialize() const;
    bool Parse(const std::string& text);

};
} // namespace horse_races::obstacle_map
