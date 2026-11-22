#pragma once
#include "race_line_pins.h"
#include "data/arthur_recorded_reference.h"

namespace horse_races::recorded_reference {
inline const Fragment* Registered(std::string_view station) {
    for(const auto* fragment:{&data::arthur_original})if(fragment->station==station)return fragment;
    return nullptr;
}
inline const Fragment* Find(std::string_view station,const course::Path& canonical) {
    const auto* fragment=Registered(station);
    return fragment&&fragment->canonical_geometry==obstacle_map::Map::GeometryHash(canonical)?fragment:nullptr;
}
inline bool Same(Vector3 a,Vector3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
inline float MapAlong(float at,const std::vector<float>& from,const std::vector<float>& to) {
    if(at<=from.front())return to.front();if(at>=from.back())return to.back();
    const auto j=static_cast<std::size_t>(std::upper_bound(from.begin(),from.end(),at)-from.begin());
    const float t=(at-from[j-1])/(from[j]-from[j-1]);return to[j-1]+t*(to[j]-to[j-1]);
}

// Build-local only. It owns the temporary reparameterized evidence view and
// never exposes that Map to persistence. It cannot move: Params borrows its
// vectors/map, while callers retain the canonical path throughout the build.
class Prepared {
    const course::Path* canonical=nullptr;
    const course::Path* base=nullptr;
    const Fragment* fragment=nullptr;
    course::Path work;
    racing_line::Params policy;
    obstacle_map::Map evidence;
    std::vector<float> progress,fixed,path_starts;
    std::vector<std::size_t> core;
    std::vector<line_pins::Stretch> projected_pins;
    const std::vector<line_pins::Stretch>* pins=nullptr;
    std::string status;
    std::size_t refused=0;
    bool Fail(const std::string& why){status=why;return false;}
    bool MakeGrid() {
        const auto& c=*canonical;const auto& f=*fragment;
        if(f.points.size()<3||!(f.from<f.to)||f.from<0||f.to>c.Length()||policy.fixed_offsets)
            return Fail("invalid recorded fragment or incompatible external fixed grid");
        std::size_t first=c.points.size(),last=c.points.size();
        for(std::size_t i=0;i<c.points.size();++i) {
            if(c.metres[i]<=f.from&&Same(c.points[i],f.points.front()))first=i;
            if(last==c.points.size()&&c.metres[i]>=f.to&&Same(c.points[i],f.points.back()))last=i;
        }
        if(first>=last||last>=c.points.size())return Fail("recorded fragment lacks exact canonical boundary anchors");
        std::vector<float> dense_progress;
        for(std::size_t i=0;i+1<c.points.size();++i) {
            const auto a=c.points[i],b=c.points[i+1];const float length=destination::Distance(a,b);
            const int parts=length>8?static_cast<int>(std::ceil(length/8)):1;
            for(int k=0;k<parts;++k) {
                const float t=static_cast<float>(k)/parts;
                const Vector3 expected=k?Vector3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}:a;
                if(dense_progress.size()>=work.points.size()||!Same(work.points[dense_progress.size()],expected))
                    return Fail("default working reference is not the canonical dense geometry");
                dense_progress.push_back(c.metres[i]+(c.metres[i+1]-c.metres[i])*t);
            }
        }
        dense_progress.push_back(c.Length());
        if(dense_progress.size()!=work.points.size())return Fail("dense reference mapping differs from its geometry");
        std::vector<Vector3> points;points.reserve(work.points.size()+f.points.size());
        auto append=[&](Vector3 p,float along) {
            if(!std::isfinite(along)||(!progress.empty()&&along<=progress.back()))return false;
            points.push_back(p);progress.push_back(along);return true;
        };
        for(std::size_t i=0;i<work.points.size();++i)if(dense_progress[i]<c.metres[first])
            if(!append(work.points[i],dense_progress[i]))return Fail("nonmonotone recorded approach");
        for(std::size_t j=0;j<f.points.size();++j) {
            const auto projected=course::Project(c,f.points[j],c.metres[first]-1,c.metres[last]+1);
            if(!projected.valid||projected.distance>policy.Usable()-.01F)return Fail("recorded fragment leaves canonical corridor");
            const float along=j==0?c.metres[first]:j+1==f.points.size()?c.metres[last]:projected.along;
            if(!append(f.points[j],along))return Fail("recorded fragment does not progress along canonical route");
        }
        for(std::size_t i=0;i<work.points.size();++i)if(dense_progress[i]>c.metres[last])
            if(!append(work.points[i],dense_progress[i]))return Fail("nonmonotone recorded exit");
        if(!work.Build(points))return Fail("recorded working grid is invalid");
        base=&work;fixed.assign(work.points.size(),NAN);
        for(std::size_t i=0;i+1<progress.size();++i)if(progress[i+1]>=f.from&&progress[i]<=f.to)
            fixed[i]=fixed[i+1]=0;
        for(std::size_t i=0;i<fixed.size();++i)if(std::isfinite(fixed[i]))core.push_back(i);
        if(core.size()<2)return Fail("recorded fragment has no fixed core chords");
        policy.fixed_offsets=&fixed;return true;
    }
    bool BindEvidence() {
        if(!policy.obstacles)return true;
        const auto& raw=*policy.obstacles;
        if(raw.geometry_hash!=obstacle_map::Map::GeometryHash(*canonical))
            return Fail("recorded fragment evidence belongs to another canonical route");
        evidence=raw;evidence.row_metres.resize(raw.rows.size());evidence.sections.resize(raw.rows.size());
        float previous=-1;
        for(std::size_t r=0;r<raw.rows.size();++r) {
            const float along=raw.Along(r);
            if(!std::isfinite(along)||along<0||along>canonical->Length()+.001F||along<=previous)
                return Fail("recorded fragment evidence has invalid canonical progress");
            previous=along;evidence.sections[r]=raw.SectionFor(*canonical,r);
            evidence.row_metres[r]=MapAlong(along,progress,work.metres);
            if(r&&evidence.row_metres[r]<=evidence.row_metres[r-1])return Fail("recorded evidence view does not progress");
        }
        policy.obstacles=&evidence;return true;
    }
    static line_pins::Stretch Trim(const line_pins::Stretch& source,float from,float to) {
        auto out=source;out.from=from;out.to=to;out.pins.clear();out.pins.push_back({from,source.Offset(from)});
        for(auto pin:source.pins)if(pin.along>from&&pin.along<to)out.pins.push_back(pin);
        out.pins.push_back({to,source.Offset(to)});return out;
    }
    bool BindPins(const line_pins::File& file) {
        const auto projected=line_pins::Project(work,file,policy.half_width);refused=projected.refused.size();
        if(projected.required_refused)return Fail("required canonical centreline cannot bind recorded working grid");
        const float from=work.metres[core.front()],to=work.metres[core.back()];
        for(const auto& stretch:projected.stretches) {
            if(stretch.required_centerline) {
                for(std::size_t i=0;i<work.points.size();++i)if(line_pins::OwnsNode(work,i,stretch)) {
                    const auto original=obstacle_map::Map::At(*canonical,progress[i]);
                    if(destination::Distance(original,work.points[i])>.001F||std::abs(original.z-work.points[i].z)>.001F)
                        return Fail("original captured path conflicts with an existing mandatory canonical centreline");
                }
                projected_pins.push_back(stretch);continue;
            }
            // Preserve every non-overlapping optional piece. Its original
            // interpolation remains a constraint, never clearance evidence.
            if(stretch.to<from||stretch.from>to){projected_pins.push_back(stretch);continue;}
            if(stretch.from<from)projected_pins.push_back(Trim(stretch,stretch.from,std::nextafter(from,-INFINITY)));
            if(stretch.to>to)projected_pins.push_back(Trim(stretch,std::nextafter(to,INFINITY),stretch.to));
        }
        std::sort(projected_pins.begin(),projected_pins.end(),[](const auto& a,const auto& b){return a.from<b.from;});
        pins=&projected_pins;return true;
    }
public:
    Prepared()=default;
    Prepared(const Prepared&)=delete;Prepared& operator=(const Prepared&)=delete;
    Prepared(Prepared&&)=delete;Prepared& operator=(Prepared&&)=delete;
    bool Prepare(const course::Path& route,const course::Path& default_reference,const racing_line::Params& params,std::string_view station,
        const std::vector<line_pins::Stretch>& legacy,const line_pins::File& file) {
        canonical=&route;policy=params;pins=&legacy;base=&default_reference;status="not registered";
        fragment=Find(station,route);
        if(!fragment){if(Registered(station))status="canonical geometry does not match captured fragment";return true;}
        if(!file.station.empty()&&file.station!=station)return Fail("recorded pins belong to another station");
        work=default_reference;
        if(!MakeGrid()||!BindEvidence()||!BindPins(file))return false;
        if(params.obstacle_path_starts) {
            for(float at:*params.obstacle_path_starts) {
                if(!std::isfinite(at))return Fail("invalid canonical profile boundary");
                path_starts.push_back(MapAlong(at,progress,work.metres));
            }
            policy.obstacle_path_starts=&path_starts;
        }
        status="original recorded core prepared";return true;
    }
    const course::Path& Path() const{return *base;}
    const racing_line::Params& Policy() const{return policy;}
    const std::vector<line_pins::Stretch>& Pins() const{return *pins;}
    const Fragment* Source() const{return fragment;}
    bool Active() const{return fragment!=nullptr;}
    const std::string& Status() const{return status;}
    std::size_t RefusedPins() const{return refused;}
    const std::vector<float>& CanonicalMetres() const{return progress;}
    const std::vector<std::size_t>& CoreNodes() const{return core;}
    // Run after all optional terrain work and old required-pin checks, while
    // offsets/reference still belong to the work grid. Publish canonical
    // progress only after every original core chord and canonical join passes.
    bool Finish(racing_line::Result& out) {
        if(!fragment)return true;
        const auto n=work.points.size();
        auto fail=[&](const std::string& why){out={};return Fail(why);};
        if(!out.verified||out.path.points.size()!=n||out.reference!=work.metres||out.offsets.size()!=n)
            return fail("captured line does not match its working reference");
        for(auto i:core)if(!Same(out.path.points[i],work.points[i])||out.offsets[i]!=0)
            return fail("original recorded core changed after optimization or overlay");
        if(!line_pins::Matches(work,policy,*pins,out,true))return fail("existing mandatory centreline changed");
        const float window=std::max(60.0F,3*policy.Usable()+30),limit=policy.Usable()-.01F;
        std::vector<float> canonical_offsets(n);float previous=-1;
        for(std::size_t i=0;i<n;++i) {
            const auto at=racing_line::detail::ProjectNear(*canonical,out.path.points[i],progress[i],window);
            if(!at.valid||at.distance>limit||at.along<=previous)return fail("captured line violates canonical corridor or order");
            const auto a=canonical->points[at.segment],b=canonical->points[at.segment+1],q=out.path.points[i];
            canonical_offsets[i]=(b.x-a.x)*(q.y-a.y)-(b.y-a.y)*(q.x-a.x)<0?-at.distance:at.distance;
            if(!i){previous=at.along;continue;}
            const auto p=out.path.points[i-1];const int samples=std::max(1,static_cast<int>(std::ceil(destination::Distance(p,q)/policy.verify_step)));
            for(int k=1;k<samples;++k) {
                const float t=static_cast<float>(k)/samples;
                const Vector3 sample{p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),p.z+t*(q.z-p.z)};
                const auto projected=racing_line::detail::ProjectNear(*canonical,sample,progress[i-1]+t*(progress[i]-progress[i-1]),window);
                if(!projected.valid||projected.distance>limit||projected.along<=previous)
                    return fail("captured join leaves canonical corridor or reverses progress");
                previous=projected.along;
            }
            if(at.along<=previous)return fail("captured join reverses canonical progress at its end");
            previous=at.along;
        }
        out.reference=progress;out.canonical_offsets=std::move(canonical_offsets);out.recorded_reference_applied=true;
        out.max_offset=0;for(float at:out.canonical_offsets)out.max_offset=std::max(out.max_offset,std::abs(at));
        status="original recorded core applied";return true;
    }
};
}
