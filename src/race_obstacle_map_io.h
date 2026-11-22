#pragma once
#include "race_obstacle_map_model.h"

namespace horse_races::obstacle_map {
// The overlay stores ridden axes and independent loaded-world probe room.
// Native geometry and pre-v8 probe caches never enter this payload.
inline std::string Map::SerializeEvidence() const {
    std::ostringstream o;o<<std::setprecision(9);
    const bool probes=std::any_of(rows.begin(),rows.end(),[](const Row& row){return row.probe_half_width>0;});
    o<<"evidence "<<key<<" step "<<kStepMetres<<" width "<<width<<" version "<<(probes?kProbeLineageEvidenceVersion:kAxisEvidenceVersion)<<" length "<<length<<" rows "<<rows.size()<<" geometry "<<geometry_hash;
    if(probes)o<<" native "<<(native_fingerprint.empty()?"none":native_fingerprint);
    o<<'\n';
    const auto spans=[&](const std::vector<Interval>& v){o<<' '<<v.size();for(auto p:v)o<<' '<<p.lo<<' '<<p.hi;};
    for(std::size_t r=0;r<rows.size();++r) {
        const auto& row=rows[r];
        if(row.probe_half_width<=0&&row.impacts.empty()&&row.connection_impacts.empty()&&(!row.passage_axes||(row.passed.empty()&&row.passages.empty())))continue;
        o<<r<<" passed";spans(row.passage_axes?row.passed:std::vector<Interval>{});
        o<<" impacts";spans(row.impacts);o<<" link_impacts";spans(row.connection_impacts);
        o<<" passages "<<(row.passage_axes?row.passages.size():0);
        if(row.passage_axes)for(auto p:row.passages)o<<' '<<p.from.lo<<' '<<p.from.hi<<' '<<p.to.lo<<' '<<p.to.hi<<' '<<p.min_normal;
        if(probes){o<<" probe "<<row.probe_half_width;spans(row.probe_intervals);spans(row.probe_connections);spans(row.probe_unknown);}
        o<<'\n';
    }
    return o.str();
}
inline bool Map::ParseEvidence(const std::string& text) {
    std::istringstream input(text);std::string header,word,file_key,file_geometry,file_native;
    std::getline(input,header);std::istringstream h(header);
    float step=0,file_width=0,file_length=0;int format=0;std::size_t count=0;
    if(!(h>>word)||word!="evidence"||!(h>>file_key>>word)||word!="step"||!(h>>step>>word)||word!="width"||
        !(h>>file_width>>word)||word!="version"||!(h>>format>>word)||word!="length"||!(h>>file_length>>word)||
        word!="rows"||!(h>>count>>word)||word!="geometry"||!(h>>file_geometry))return false;
    if(format==kProbeLineageEvidenceVersion) {
        if(!(h>>word)||word!="native"||!(h>>file_native))return false;
        if(file_native!="none"&&(file_native.size()!=16||!std::all_of(file_native.begin(),file_native.end(),
            [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})))return false;
    }
    if(h>>word)return false;
    if(file_key!=key||file_geometry!=geometry_hash||step!=kStepMetres||file_width!=width||
        (format!=kAxisEvidenceVersion&&format!=kProbeEvidenceVersion&&format!=kProbeLineageEvidenceVersion)||
        !std::isfinite(file_length)||std::abs(file_length-length)>.02F||count!=rows.size())return false;
    const bool use_probes=format==kProbeLineageEvidenceVersion&&file_native==(native_fingerprint.empty()?"none":native_fingerprint);
    auto parsed=rows;std::vector<bool> seen(rows.size());
    for(auto& row:parsed) {
        row.passed.clear();row.passages.clear();row.impacts.clear();row.connection_impacts.clear();
        row.probe_half_width=0;row.probe_intervals.clear();row.probe_connections.clear();row.probe_unknown.clear();
        row.passage_axes=true;row.reason&=~(kImpact|kPassage);
    }
    const auto valid=[&](Interval p){return std::isfinite(p.lo)&&std::isfinite(p.hi)&&p.lo<=p.hi&&p.lo>=-width&&p.hi<=width;};
    const auto spans=[&](std::istringstream& in,std::vector<Interval>& values) {
        std::size_t n=0;if(!(in>>n)||n>256)return false;
        for(std::size_t i=0;i<n;++i){Interval p;if(!(in>>p.lo>>p.hi)||!valid(p))return false;values.push_back(p);}
        values=Normalize(std::move(values));return true;
    };
    std::string line;
    while(std::getline(input,line)) {
        if(line.empty())continue;std::istringstream in(line);std::size_t r=0,n=0;
        if(!(in>>r)||r>=parsed.size()||seen[r])return false;seen[r]=true;auto& row=parsed[r];
        if(!(in>>word)||word!="passed"||!spans(in,row.passed)||!(in>>word)||word!="impacts"||!spans(in,row.impacts)||
            !(in>>word)||word!="link_impacts"||!spans(in,row.connection_impacts)||!(in>>word)||word!="passages"||!(in>>n)||n>64)return false;
        for(std::size_t i=0;i<n;++i) {
            Passage p;if(!(in>>p.from.lo>>p.from.hi>>p.to.lo>>p.to.hi>>p.min_normal)||!valid(p.from)||!valid(p.to)||
                !std::isfinite(p.min_normal)||p.min_normal<.01F||p.min_normal>1.001F||!r)return false;
            AddPassage(row.passages,p);
        }
        if(format>=kProbeEvidenceVersion) {
            if(!(in>>word)||word!="probe"||!(in>>row.probe_half_width)||!std::isfinite(row.probe_half_width)||
                row.probe_half_width<0||row.probe_half_width>width||!spans(in,row.probe_intervals)||
                !spans(in,row.probe_connections)||!spans(in,row.probe_unknown))return false;
            if(row.probe_half_width==0&&(!row.probe_intervals.empty()||!row.probe_connections.empty()||!row.probe_unknown.empty()))return false;
            if(!use_probes) {
                row.probe_half_width=0;row.probe_intervals.clear();row.probe_connections.clear();row.probe_unknown.clear();
            }
            if(native_mode&&row.HasDetailed()) {
                const auto permitted=Union(Union(row.intervals,row.unknown),row.passed);
                row.probe_intervals=Intersect(row.probe_intervals,permitted);
                row.probe_connections=Intersect(row.probe_connections,permitted);
            }
        }
        if(in>>word)return false;
        if(row.probe_half_width>0||!row.passed.empty()||!row.passages.empty()||!row.impacts.empty()||!row.connection_impacts.empty()) {
            if(!row.detailed){row.intervals=row.ClearIntervals();row.unknown={{-width,width}};}
            row.detailed=true;
            if(!row.passed.empty()||!row.passages.empty())row.reason|=kPassage;
            if(!row.impacts.empty()||!row.connection_impacts.empty())row.reason|=kImpact;
            row.unknown=SubtractAll(SubtractAll(row.unknown,row.passed),row.impacts);row.RefreshWidths();
        }
    }
    rows=std::move(parsed);evidence_probe_stale=format>=kProbeEvidenceVersion&&!use_probes;MarkChanged();return true;
}
inline std::string Map::Serialize() const {
    std::ostringstream o;o<<std::setprecision(9);
    const bool axes=std::any_of(rows.begin(),rows.end(),[](const Row& row){return row.passage_axes;});
    const bool probes=std::any_of(rows.begin(),rows.end(),[](const Row& row){return row.probe_half_width>0;});
    const int format=probes?kProbeEvidenceVersion:axes?kAxisEvidenceVersion:std::max(version,kDetailedVersion);
    o<<"course "<<key<<" step "<<kStepMetres<<" width "<<width<<" version "<<format<<" length "<<length<<" rows "<<rows.size()<<" geometry "<<geometry_hash<<'\n';
    const auto spans=[&](const std::vector<Interval>& v){o<<' '<<v.size();for(auto p:v)o<<' '<<p.lo<<' '<<p.hi;};
    for(std::size_t r=0;r<rows.size();++r)if(rows[r].Surveyed()) {
        const auto& row=rows[r];const int confirmed=std::max<int>(row.confirmed,(r<suspect.size()&&suspect[r]&&Isolated(r))?1:0);
        o<<r<<' '<<row.left<<' '<<row.right<<' '<<confirmed<<" d "<<row.detailed<<' '<<row.connections_detailed<<' '<<(row.legacy||!row.detailed)<<' '<<row.reason;
        spans(row.intervals);spans(row.connections);spans(row.unknown);o<<" radius "<<row.connection_radius;
        o<<" passed";spans(row.passed);o<<" impacts";spans(row.impacts);o<<" link_impacts";spans(row.connection_impacts);
        o<<" passages "<<row.passages.size();for(auto p:row.passages)o<<' '<<p.from.lo<<' '<<p.from.hi<<' '<<p.to.lo<<' '<<p.to.hi<<' '<<p.min_normal;
        if(!row.native_strips.empty()) {
            o<<" native_strips "<<row.native_strips.size();
            for(auto p:row.native_strips)o<<' '<<p.from.lo<<' '<<p.from.hi<<' '<<p.to.lo<<' '<<p.to.hi<<' '<<p.min_normal;
        }
        if(format>=kAxisEvidenceVersion)o<<" axes "<<row.passage_axes;
        if(probes){o<<" probe "<<row.probe_half_width;spans(row.probe_intervals);spans(row.probe_connections);spans(row.probe_unknown);}
        o<<'\n';
    }
    return o.str();
}
inline bool Map::Parse(const std::string& text) {
    std::istringstream in(text);std::string header;std::getline(in,header);std::istringstream h(header);
    std::string word,file_key,file_geometry;float step=0,file_width=0,file_length=0;std::size_t count=0;int file_version=0;
    if(!(h>>word)||word!="course"||!(h>>file_key>>word)||word!="step"||!(h>>step>>word)||word!="width"||!(h>>file_width>>word)||word!="version"||!(h>>file_version>>word)||word!="length"||!(h>>file_length>>word)||word!="rows"||!(h>>count))return false;
    if(file_key!=key||step!=kStepMetres||!std::isfinite(file_width)||file_width<width||!std::isfinite(file_length)||file_length<=0||
        file_version<1||file_version>kProbeEvidenceVersion||count!=rows.size())return false;
    while(h>>word){if(word=="geometry"){if(!(h>>file_geometry))return false;}else return false;}
    if(file_version>=6&&file_geometry.empty())return false;
    if(file_version>=6&&std::abs(file_length-length)>.02F)return false;
    if(!file_geometry.empty()&&file_geometry!=geometry_hash){geometry_mismatch=true;return false;}
    std::vector<Row> parsed(rows.size());bool legacy_file=file_version<kDetailedVersion;std::string line;
    const auto read_spans=[&](std::istringstream& s,std::vector<Interval>& v){
        std::size_t n=0;if(!(s>>n)||n>256)return false;
        for(std::size_t k=0;k<n;++k){Interval p;if(!(s>>p.lo>>p.hi)||!std::isfinite(p.lo)||!std::isfinite(p.hi)||p.lo>p.hi||p.lo<-file_width-.01F||p.hi>file_width+.01F)return false;v.push_back(p);}
        v=Normalize(std::move(v));return true;
    };
    while(std::getline(in,line)) {
        if(line.empty()||line.rfind("hazard ",0)==0)continue;std::istringstream s(line);std::size_t r=0;Row row;int confirmed=0;
        if(!(s>>r>>row.left>>row.right))continue;
        if(r>=parsed.size()||!std::isfinite(row.left)||!std::isfinite(row.right)||row.left<0||row.right<0||row.left>file_width+.01F||row.right>file_width+.01F)continue;
        if(!(s>>confirmed)){s.clear();confirmed=0;}row.confirmed=static_cast<unsigned char>(std::clamp(confirmed,0,255));
        if(s>>word) {
            int detailed=0,connected=0,legacy=0;
            if(word!="d"||!(s>>detailed>>connected>>legacy>>row.reason)||!read_spans(s,row.intervals)||!read_spans(s,row.connections)||!read_spans(s,row.unknown))return false;
            if(detailed<0||detailed>1||connected<0||connected>1||legacy<0||legacy>1||(connected&&!detailed))return false;
            row.detailed=detailed!=0;row.connections_detailed=connected!=0;row.legacy=legacy!=0||!row.detailed;if(row.detailed)row.RefreshWidths();
            // Earlier v6 writers have no radius field: zero preserves the
            // conservative physical-interval interpretation of those files.
            if(s>>word) {
                if(word!="radius"||!(s>>row.connection_radius)||!std::isfinite(row.connection_radius)||
                    row.connection_radius<0||row.connection_radius>kMaxConnectionRadius)return false;
                if(s>>word) {
                    if(file_version<7||word!="passed"||!read_spans(s,row.passed)||!(s>>word)||word!="impacts"||!read_spans(s,row.impacts)||
                        !(s>>word)||word!="link_impacts"||!read_spans(s,row.connection_impacts)||!(s>>word)||word!="passages")return false;
                    std::size_t n=0;if(!(s>>n)||n>64)return false;
                    for(std::size_t k=0;k<n;++k) {
                        Passage p;if(!(s>>p.from.lo>>p.from.hi>>p.to.lo>>p.to.hi>>p.min_normal)||
                            !std::isfinite(p.min_normal)||p.min_normal<.01F||p.min_normal>1.001F)return false;
                        for(auto span:{p.from,p.to})if(!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>span.hi||span.lo< -file_width-.01F||span.hi>file_width+.01F)return false;
                        row.passages.push_back(p);
                    }
                    bool trailing=static_cast<bool>(s>>word);
                    if(trailing&&word=="native_strips") {
                        std::size_t count_native=0;if(!(s>>count_native)||count_native>64||!r||!row.HasConnections())return false;
                        for(std::size_t k=0;k<count_native;++k) {
                            Passage p;if(!(s>>p.from.lo>>p.from.hi>>p.to.lo>>p.to.hi>>p.min_normal)||
                                !std::isfinite(p.min_normal)||p.min_normal<.01F||p.min_normal>1.001F)return false;
                            for(auto span:{p.from,p.to})if(!std::isfinite(span.lo)||!std::isfinite(span.hi)||span.lo>span.hi||
                                span.lo< -file_width-.01F||span.hi>file_width+.01F)return false;
                            row.native_strips.push_back(p);
                        }
                        trailing=static_cast<bool>(s>>word);
                    }
                    if(file_version>=kAxisEvidenceVersion) {
                        int axes=0;if(!trailing||word!="axes"||!(s>>axes)||axes<0||axes>1)return false;
                        row.passage_axes=axes!=0;
                        trailing=static_cast<bool>(s>>word);
                    }
                    if(file_version>=kProbeEvidenceVersion) {
                        if(!trailing||word!="probe"||!(s>>row.probe_half_width)||!std::isfinite(row.probe_half_width)||
                            row.probe_half_width<0||row.probe_half_width>file_width||!read_spans(s,row.probe_intervals)||
                            !read_spans(s,row.probe_connections)||!read_spans(s,row.probe_unknown))return false;
                        if(row.probe_half_width==0&&(!row.probe_intervals.empty()||!row.probe_connections.empty()||!row.probe_unknown.empty()))return false;
                        trailing=static_cast<bool>(s>>word);
                    }
                    if(trailing)return false;
                } else if(file_version>=7)return false;
            } else if(file_version>=7)return false;
        } else {row.legacy=true;row.reason|=kLegacy;}
        if(file_version<kDetailedVersion){
            row.legacy=true;row.reason|=kLegacy;
            // v6 lacks per-span impact provenance. Keep its conservative
            // veto locally until real passage contradicts it; never assign
            // these old holes to any differently recorded profile.
            if(row.reason&kImpact) {
                row.impacts=SubtractAll({{-width,width}},Union(row.ClearIntervals(),row.unknown));
                row.connection_impacts=SubtractAll({{-width,width}},Union(row.connections,row.unknown));
            }
        }
        row.RefreshWidths();legacy_file|=row.legacy;parsed[r]=std::move(row);
    }
    rows=std::move(parsed);row_metres.clear();sections.clear();
    loaded_version=file_version;loaded_legacy=legacy_file;geometry_mismatch=false;MarkChanged();return true;
}
} // namespace horse_races::obstacle_map
