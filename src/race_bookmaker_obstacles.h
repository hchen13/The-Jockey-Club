#pragma once

#include "race_obstacle_profiles.h"
#include "race_obstacle_impact_transfer.h"
#include "race_native_corridor.h"
#include "race_obstacle_storage.h"
#include "race_obstacle_snapshot.h"

namespace horse_races::bookmaking {
// Retain the public name used by bookmaker fixtures and the offline exporter.
using ObstacleProfiles=obstacle_profiles::Bank;

// One quotation owns its evidence until the show ends. A new quotation must
// read the current files, including observations written by a host race while
// the bookmaker was idle. Pending/failed writes transfer to the next owner
// through the shared writer, rather than a stale quotation retrying a map.
struct QuoteObstacleEvidence {
    ObstacleProfiles profiles;
    std::array<native_corridor::LoadResult,3> loaded;
    std::array<std::uint64_t,3> saved_revisions{};
    std::array<obstacle_storage::SnapshotBuilder,3> snapshots;
    std::size_t snapshot_turn=0;
    std::filesystem::path folder;

    bool LoadFresh(const std::string& station,const lap::Plan& plan,
        const std::filesystem::path& source,float width,int version) {
        if(!profiles.station.empty())Release([](std::size_t,bool){});
        *this={};folder=source;
        if(!profiles.Bind(station,plan,width,version))return false;
        for(std::size_t profile=0;profile<profiles.maps.size();++profile) {
            auto& map=profiles.maps[profile];
            loaded[profile]=native_corridor::Load(map,profiles.paths[profile],folder);
            saved_revisions[profile]=map.revision;
        }
        profiles.LoadSeams(folder/"native");
        return true;
    }
    template<class Report> bool Save(Report report) {
        bool complete=true;
        for(std::size_t profile=0;profile<profiles.maps.size();++profile) {
            const auto& map=profiles.maps[profile];
            if(map.key.empty() || !map.Surveyed())continue;
            auto& status=loaded[profile];
            if(!status.evidence_writable){complete=false;report(profile,false);continue;}
            const auto target=native_corridor::EvidencePath(map,folder);
            obstacle_storage::Retry(target);
            if(obstacle_storage::Committed(target,map.evidence_epoch,map.revision))saved_revisions[profile]=map.revision;
            if(map.revision==saved_revisions[profile])continue;
            snapshots[profile].Request(map,"bookmaker periodic");report(profile,true);
        }
        return complete;
    }
    void Pump(double budget_ms=.1) {
        const auto profile=snapshot_turn++%profiles.maps.size();const auto& map=profiles.maps[profile];
        if(auto snapshot=snapshots[profile].Tick(map,budget_ms))
            obstacle_storage::Submit(native_corridor::EvidencePath(map,folder),std::move(snapshot),
                loaded[profile].evidence_backup_pending);
    }
    template<class Report> bool Release(Report report) {
        bool complete=true;
        for(std::size_t profile=0;profile<profiles.maps.size();++profile) {
            auto& map=profiles.maps[profile];
            if(map.key.empty())continue;
            const auto target=native_corridor::EvidencePath(map,folder);
            if(obstacle_storage::Committed(target,map.evidence_epoch,map.revision))saved_revisions[profile]=map.revision;
            if(map.revision==saved_revisions[profile]){obstacle_storage::Retry(target);continue;}
            const bool queued=loaded[profile].evidence_writable&&snapshots[profile].Retire(target,map,
                loaded[profile].evidence_backup_pending,"bookmaker released");
            complete&=queued;report(profile,queued);
        }
        // The writer owns retired observations. This quotation has no mutable
        // bank left that could submit over a later host race's evidence.
        *this={};return complete;
    }
};
} // namespace horse_races::bookmaking
