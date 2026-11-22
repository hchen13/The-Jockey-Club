#pragma once
#include "race_obstacle_map.h"
#include "data/race_blackwater_obstacles.h"
#include "race_obstacle_storage.h"
#include "race_strip_geometry.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace horse_races::native_corridor {
inline constexpr float kBodyRadius=.5F;
inline constexpr const char* kSource="rdr3-collision-v1";
struct LoadResult {
    bool native_loaded=false,evidence_loaded=false;
    std::filesystem::path native_path,evidence_path;
    std::string error;
    bool evidence_writable=true,evidence_backup_pending=false;
};
inline std::filesystem::path EvidencePath(const obstacle_map::Map& map,const std::filesystem::path& folder) {
    return folder/"evidence"/(map.key+".txt");
}
// Called only while binding a recorded geometry. The native file and pre-v8
// probe cache remain immutable; v9 loaded-world probes have separate provenance.
inline LoadResult Load(obstacle_map::Map& map,const course::Path& path,const std::filesystem::path& folder) {
    LoadResult result;result.native_path=folder/"native"/(map.key+".txt");result.evidence_path=EvidencePath(map,folder);
    map.native_mode=true;map.native_loaded=false;map.native_source.clear();map.native_fingerprint.clear();map.evidence_probe_stale=false;
    const auto fail=[&](const std::string& message){if(!result.error.empty())result.error+="; ";result.error+=message;};
    if(map.key.empty()||map.geometry_hash!=obstacle_map::Map::GeometryHash(path)||
        map.rows.size()!=obstacle_map::Map::Rows(path.Length())||!map.row_metres.empty()) {
        fail("recorded geometry is not bound");result.evidence_writable=false;return result;
    }
    const auto pending=obstacle_storage::BeginLoad(result.evidence_path);
    map.evidence_epoch=pending.epoch;
    map.rows.assign(map.rows.size(),obstacle_map::Row{});map.suspect.assign(map.rows.size(),0);
    map.native_loaded=false;map.native_source.clear();map.loaded_version=0;map.loaded_legacy=false;map.geometry_mismatch=false;map.MarkChanged();
    const auto read=[&](const std::filesystem::path& file,std::string& bytes,bool evidence) {
        std::error_code error;const bool exists=std::filesystem::exists(file,error);
        if(error){fail((evidence?"evidence":"native")+std::string(" path inaccessible"));if(evidence)result.evidence_writable=false;return false;}
        if(!exists){if(!evidence)fail("native base missing");return false;}
        if(evidence)result.evidence_backup_pending=true;
        if(!obstacle_storage::Read(file,bytes)){fail(evidence?"evidence read failed":"native read failed");if(evidence)result.evidence_writable=false;return false;}
        return true;
    };
    std::string bytes;
    if(read(result.native_path,bytes,false)) {
        std::istringstream input(bytes);std::string header,marker,word,source;float radius=0;
        std::getline(input,header);std::getline(input,marker);std::istringstream m(marker);
        const bool identified=(m>>word)&&word=="native"&&(m>>word)&&word=="source"&&(m>>source)&&source==kSource&&
            (m>>word)&&word=="body_radius"&&(m>>radius)&&radius==kBodyRadius&&!(m>>word);
        auto base=map;
        bool valid=identified&&base.Parse(bytes)&&base.loaded_version==obstacle_map::kDetailedVersion&&!base.loaded_legacy;
        for(std::size_t index=0;valid&&index<base.rows.size();++index) {
            auto& row=base.rows[index];
            valid=row.passed.empty()&&row.passages.empty()&&row.impacts.empty()&&row.connection_impacts.empty()&&
                !row.passage_axes&&!(row.reason&(obstacle_map::kImpact|obstacle_map::kPassage))&&
                (!row.HasConnections()||row.connection_radius==kBodyRadius);
            if(!row.native_strips.empty()&&!index)valid=false;
            if(index)for(auto& strip:row.native_strips) {
                const auto from=base.SectionFor(path,index-1),to=base.SectionFor(path,index);
                const strip_geometry::Patch patch{from,to,strip.from,strip.to};
                const auto prepared=strip_geometry::Prepare(patch);
                const float normal=strip_geometry::MinimumNormalLength(from,to);
                valid&=prepared.regular&&strip.min_normal<=normal+1e-5F;
                if(prepared.regular) {
                    // Serialized float rails can lie one ULP beyond a fold.
                    // Retain the certified positive-order SUBSET; never grow
                    // it or reject an otherwise valid whole recorded profile.
                    strip.from=prepared.patch.entry;strip.to=prepared.patch.exit;
                    strip.min_normal=std::min(strip.min_normal,normal);
                }
            }
        }
        if(valid){map=std::move(base);map.native_loaded=true;map.native_source=source;
            map.native_fingerprint=obstacle_map::Map::NativeFingerprint(bytes);result.native_loaded=true;}
        else fail(base.geometry_mismatch?"native geometry mismatch":"native base rejected (marker, geometry or provenance)");
    }
    if(pending.pending&&((pending.pending->restore&&pending.pending->restore(map))||
        (pending.pending->serialized&&map.ParseEvidence(*pending.pending->serialized)))) {
        result.evidence_loaded=true;result.evidence_backup_pending=pending.backup_pending||map.evidence_probe_stale;
        obstacle_storage::Retry(result.evidence_path);
    } else if(read(result.evidence_path,bytes,true)) {
        if(map.ParseEvidence(bytes)){result.evidence_loaded=true;result.evidence_backup_pending=map.evidence_probe_stale;}
        else fail("evidence rejected (axis/probe identity or payload mismatch)");
    }
    // A planner may densify the route. Native certificates stay attached to
    // the exact original section frames, rather than a second accumulation
    // of float distances along that equivalent polyline. Cache once at bind.
    map.sections.resize(map.rows.size());
    for(std::size_t row=0;row<map.rows.size();++row)
        map.sections[row]=obstacle_map::Map::SectionAt(path,map.Along(row));
    const auto shape=map.key.find("-shape");
    if(shape!=std::string::npos&&map.key.substr(0,shape)=="blackwater"&&
       !authored_obstacles::Apply(map,path,authored_obstacles::kBlackwaterPosts))
        fail("authored Blackwater posts not mapped to recorded geometry");
    return result;
}
} // namespace horse_races::native_corridor
