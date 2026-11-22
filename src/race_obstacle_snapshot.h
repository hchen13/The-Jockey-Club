#pragma once
#include "race_obstacle_map.h"
#include "race_obstacle_storage.h"

namespace horse_races::obstacle_storage {
inline obstacle_map::Row EvidenceRow(const obstacle_map::Row& source) {
    obstacle_map::Row row;row.passage_axes=source.passage_axes;
    if(source.passage_axes){row.passed=source.passed;row.passages=source.passages;}
    row.impacts=source.impacts;row.connection_impacts=source.connection_impacts;
    row.probe_half_width=source.probe_half_width;row.probe_intervals=source.probe_intervals;
    row.probe_connections=source.probe_connections;row.probe_unknown=source.probe_unknown;
    return row;
}
inline bool RestoreEvidence(obstacle_map::Map& map,const obstacle_map::Map& source) {
    if(map.key!=source.key||map.geometry_hash!=source.geometry_hash||map.width!=source.width||
        map.length!=source.length||map.rows.size()!=source.rows.size())return false;
    const bool use_probes=map.native_fingerprint==source.native_fingerprint;
    const bool had_probes=std::any_of(source.rows.begin(),source.rows.end(),[](const auto& row){return row.probe_half_width>0;});
    for(std::size_t index=0;index<map.rows.size();++index) {
        auto& row=map.rows[index];const auto evidence=EvidenceRow(source.rows[index]);
        const auto permitted=obstacle_map::Union(obstacle_map::Union(row.intervals,row.unknown),evidence.passed);
        row.passed=evidence.passed;row.passages=evidence.passages;row.passage_axes=true;
        row.impacts=evidence.impacts;row.connection_impacts=evidence.connection_impacts;
        row.probe_half_width=evidence.probe_half_width;row.probe_intervals=evidence.probe_intervals;
        row.probe_connections=evidence.probe_connections;row.probe_unknown=evidence.probe_unknown;
        if(!use_probes){row.probe_half_width=0;row.probe_intervals.clear();row.probe_connections.clear();row.probe_unknown.clear();}
        if(map.native_mode&&row.HasDetailed()) {
            row.probe_intervals=obstacle_map::Intersect(row.probe_intervals,permitted);
            row.probe_connections=obstacle_map::Intersect(row.probe_connections,permitted);
        }
        row.reason&=~(obstacle_map::kImpact|obstacle_map::kPassage);
        if(row.probe_half_width>0||!row.passed.empty()||!row.passages.empty()||!row.impacts.empty()||!row.connection_impacts.empty()) {
            if(!row.detailed){row.intervals=row.ClearIntervals();row.unknown={{-map.width,map.width}};}
            row.detailed=true;
            if(!row.passed.empty()||!row.passages.empty())row.reason|=obstacle_map::kPassage;
            if(!row.impacts.empty()||!row.connection_impacts.empty())row.reason|=obstacle_map::kImpact;
            row.unknown=obstacle_map::SubtractAll(obstacle_map::SubtractAll(row.unknown,row.passed),row.impacts);
            row.RefreshWidths();
        }
    }
    map.evidence_probe_stale=had_probes&&!use_probes;map.MarkChanged();return true;
}
inline std::shared_ptr<const Snapshot> MakeSnapshot(std::shared_ptr<const obstacle_map::Map> map,const std::string& why) {
    auto result=std::make_shared<Snapshot>();
    result->key=map->key;result->why=why;result->revision=map->revision;result->epoch=map->evidence_epoch;result->rows=map->rows.size();
    result->serialize=[map]{return map->SerializeEvidence();};
    result->restore=[map](obstacle_map::Map& target){return RestoreEvidence(target,*map);};
    return result;
}
// A staging pass copies only persistence overlays, at most 64 rows and 0.1 ms
// per frame. Row stamps repair changes made during the pass before publication.
// The writer receives one coherent revision and never reads the live map.
struct SnapshotBuilder {
    std::shared_ptr<obstacle_map::Map> staging;
    std::vector<std::uint64_t> copied;
    std::size_t cursor=0;
    std::uint64_t submitted=0;
    bool requested=false;
    std::string why;
    void Request(const obstacle_map::Map& map,const char* reason) {
        if(map.revision!=submitted){requested=true;why=reason;}
    }
    std::shared_ptr<const Snapshot> Tick(const obstacle_map::Map& map,double budget_ms=.1,std::size_t maximum_rows=64) {
        if(!requested||map.key.empty()||map.rows.empty()||!(budget_ms>0)||!maximum_rows)return {};
        const auto start=std::chrono::steady_clock::now();
        if(!staging||staging->key!=map.key||staging->geometry_hash!=map.geometry_hash||
            staging->evidence_epoch!=map.evidence_epoch||staging->width!=map.width||staging->length!=map.length||
            staging->native_fingerprint!=map.native_fingerprint) {
            staging=std::make_shared<obstacle_map::Map>();copied.clear();cursor=0;
            staging->key=map.key;staging->geometry_hash=map.geometry_hash;staging->width=map.width;
            staging->length=map.length;staging->version=map.version;staging->evidence_epoch=map.evidence_epoch;
            staging->native_fingerprint=map.native_fingerprint;
            staging->rows.reserve(map.rows.size());copied.reserve(map.rows.size());
        }
        std::size_t rows=0;
        while(cursor<map.rows.size()&&rows<maximum_rows&&
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<budget_ms) {
            const auto index=cursor++;const auto revision=map.RowRevision(index);
            if(index==staging->rows.size()){staging->rows.emplace_back();copied.push_back(0);}
            if(!copied[index]||copied[index]!=revision) {
                staging->rows[index]=EvidenceRow(map.rows[index]);copied[index]=revision;++rows;
            }
        }
        if(cursor<map.rows.size())return {};
        // Checking integer stamps does no geometry work or heap allocation.
        // A moving horse usually changes only a handful of already copied rows.
        for(std::size_t index=0;index<copied.size();++index)
            if(copied[index]!=map.RowRevision(index)){cursor=index;return {};}
        staging->revision=map.revision;submitted=map.revision;requested=false;
        auto result=MakeSnapshot(std::move(staging),why);copied.clear();cursor=0;return result;
    }
    bool Retire(const std::filesystem::path& target,obstacle_map::Map& map,bool backup,const char* reason) {
        // Moving the released geometry does not copy its native rows. Dropping
        // partial staging still destroys its overlay rows on this caller; the
        // offline benchmark measures that release cost separately from I/O.
        // Final observations survive even if staging was not yet complete.
        auto owned=std::make_shared<obstacle_map::Map>(std::move(map));
        if(!Submit(target,MakeSnapshot(owned,reason),backup)) {
            // An older owner cannot publish. Keep both its live observations
            // and unfinished staging so the caller can refuse the handoff.
            map=std::move(*owned);return false;
        }
        staging.reset();copied.clear();requested=false;cursor=0;return true;
    }
};
} // namespace horse_races::obstacle_storage
