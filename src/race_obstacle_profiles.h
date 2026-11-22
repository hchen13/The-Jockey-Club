#pragma once

#include "race_obstacle_map.h"
#include "race_obstacle_seams.h"
#include "race_segmented_circuit.h"
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <utility>

namespace horse_races::obstacle_profiles {
// Persistent observations belong to a recorded shape, never to a random lap
// draw. The race gets exact translated copies of those measured sections.
struct Bank {
    std::string station;
    std::array<course::Path,3> paths;
    std::array<obstacle_map::Map,3> maps;
    std::size_t last_refresh_rows=0;

    static std::string ShapeKey(const std::string& station,int profile,const course::Path& path) {
        // Keep the existing bookmaker identity, including every coordinate.
        // Node count and rounded length alone cannot identify a recording.
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto p:path.points)for(const float coordinate:{p.x,p.y,p.z}) {
            hash^=std::bit_cast<std::uint32_t>(coordinate);hash*=1099511628211ULL;
        }
        return station+"-shape"+std::to_string(profile)+"-"+std::to_string(hash);
    }
    bool Bind(const std::string& name,const lap::Plan& plan,float width,int version) {
        if(name.empty() || !plan.course.Ready() || plan.Total()<1)return false;
        auto candidate=*this;
        bool changed=station!=name;
        for(int profile=0;profile<3;++profile) {
            const auto& path=plan.profiles[profile];
            const auto old=candidate.maps[profile].key;
            const auto old_width=candidate.maps[profile].width;
            const auto old_version=candidate.maps[profile].version;
            if(!candidate.maps[profile].Bind(ShapeKey(name,profile,path),path,width,version))return false;
            changed|=old!=candidate.maps[profile].key || old_width!=width || old_version!=version;
            candidate.paths[profile]=path;
        }
        if(changed) {
            candidate.binding_={};candidate.seams_={};candidate.seam_impacts_={};
            candidate.seam_dirty_={};candidate.seams_loaded_=false;
        }
        candidate.station=name;*this=std::move(candidate);return true;
    }
    // Called after canonical native maps load, once for each bound geometry.
    // Random lap draws reuse the four ordered pair proofs without file I/O.
    std::size_t LoadSeams(const std::filesystem::path& native_folder) {
        if(!seams_loaded_) {
            for(int from:{0,1})for(int to:{1,2})
                seams_[from][to]=obstacle_seams::Load(native_folder,maps[from],paths[from],maps[to],paths[to]);
            seams_loaded_=true;
        }
        std::size_t loaded=0;for(const auto& row:seams_)for(const auto& link:row)loaded+=link.loaded;
        return loaded;
    }
    static std::pair<int,float> Local(const lap::Plan& plan,float along) {
        const auto last=std::upper_bound(plan.offsets.begin(),plan.offsets.end(),along);
        const int lap=std::clamp(static_cast<int>(last-plan.offsets.begin())-1,0,plan.Total()-1);
        return {plan.Profile(lap),std::max(0.0F,along-plan.offsets[lap])};
    }
    bool Matches(const lap::Plan& plan) const {
        if(station.empty() || !plan.course.Ready() || plan.Total()<1)return false;
        for(int profile=0;profile<3;++profile)
            if(!lap::SameNodes(paths[profile].points,plan.profiles[profile].points))return false;
        return true;
    }
    // Narrow legacy side widths do not measure either the outer corridor or
    // connected passages. Keep their source file, but remeasure those rows.
    static std::size_t DiscardLegacyClearance(obstacle_map::Map& map) {
        std::size_t discarded=0;
        for(std::size_t row=0;row<map.rows.size();++row)
            if(map.rows[row].Surveyed() && !map.rows[row].HasDetailed()) {
                map.rows[row]={};if(row<map.suspect.size())map.suspect[row]=0;++discarded;
            }
        if(discarded)map.MarkChanged();return discarded;
    }
    bool Expand(const lap::Plan& plan,obstacle_map::Map& expanded) {
        if(!Matches(plan) || !expanded.Bind(station,plan.course.path,maps[0].width,maps[0].version))return false;
        expanded.rows.clear();expanded.row_metres.clear();expanded.sections.clear();
        binding_={};binding_.offsets=plan.offsets;
        for(int profile=0;profile<3;++profile)binding_.destinations[profile].resize(maps[profile].rows.size());
        for(int lap=0;lap<plan.Total();++lap) {
            const int profile=plan.Profile(lap);
            const auto& source=maps[profile];
            for(std::size_t row=0;row<source.rows.size();++row) {
                const float local=source.Along(row);
                if(lap+1<plan.Total() && local>=paths[profile].Length()-.0001F)break;
                const float along=plan.offsets[lap]+local;
                if(!expanded.row_metres.empty() && along<=expanded.row_metres.back())continue;
                const Copy copy{profile,lap,row,lap?plan.Profile(lap-1):-1};
                binding_.destinations[profile][row].push_back(expanded.rows.size());
                expanded.rows.push_back(ExpandedRow(copy));
                expanded.row_metres.push_back(along);
                expanded.sections.push_back(source.SectionFor(paths[profile],row));
                binding_.copies.push_back(copy);
                if(!row&&lap)binding_.seam_rows.push_back(expanded.rows.size()-1);
            }
        }
        expanded.suspect.assign(expanded.rows.size(),0);expanded.MarkChanged();
        expanded.native_mode=false;expanded.native_loaded=true;expanded.native_source=maps[0].native_source;
        for(int lap=0;lap<plan.Total();++lap) {
            expanded.native_mode|=maps[plan.Profile(lap)].native_mode;
            expanded.native_loaded&=maps[plan.Profile(lap)].native_loaded;
        }
        binding_.key=expanded.key;binding_.geometry=expanded.geometry_hash;
        for(int profile=0;profile<3;++profile)
            binding_.revisions[profile]=binding_.captured_revisions[profile]=maps[profile].revision;
        return true;
    }
    // Pull a sanctioned map mutation (impact or successful horse passage)
    // from ONE qualified lap. Other regular copies still contain the prior
    // observation, so accepting an unbounded range could undo new evidence.
    std::size_t Capture(const lap::Plan& plan,const obstacle_map::Map& expanded,float from,float to) {
        if(!BoundTo(plan,expanded) || !std::isfinite(from) || !std::isfinite(to) || from<0 || to<from || to>expanded.length)return 0;
        const auto begin=std::upper_bound(plan.offsets.begin(),plan.offsets.end(),from);
        const int lap=std::clamp(static_cast<int>(begin-plan.offsets.begin())-1,0,plan.Total()-1);
        if(lap+1<plan.Total() && to>=plan.offsets[lap+1])return 0;
        // Direct canonical mutations must be published before an expanded
        // rider can teach another observation. Refuse a stale source instead
        // of copying its older rows over newer survey or physical evidence.
        for(int profile=0;profile<3;++profile)
            if(maps[profile].revision!=binding_.revisions[profile])return 0;
        for(const auto& row:seam_dirty_)for(const bool dirty:row)if(dirty)return 0;
        std::size_t changed=0;
        for(std::size_t row=expanded.Index(from);row<=expanded.Index(to) && row<binding_.copies.size();++row) {
            const auto copy=binding_.copies[row];
            if(copy.lap!=lap || expanded.Along(row)<from || expanded.Along(row)>to)continue;
            auto& target=maps[copy.profile];
            if(copy.row!=0 || copy.lap==0) {
                if(Assign(target,copy.row,expanded.rows[row])) {
                    binding_.captured_rows[copy.profile].push_back(copy.row);++changed;
                }
                continue;
            }
            auto& impacts=seam_impacts_[copy.previous_profile][copy.profile];
            const bool incoming_changed=impacts!=expanded.rows[row].connection_impacts;
            if(incoming_changed) {
                impacts=expanded.rows[row].connection_impacts;
                seam_dirty_[copy.previous_profile][copy.profile]=true;
            }
            if(expanded.rows[row]==ExpandedRow(copy)){changed+=incoming_changed;continue;}
            auto observation=expanded.rows[row];
            // The bridge belongs to this profile PAIR, never to row zero's
            // unrelated incoming connection in the standalone recording.
            if(copy.row==0 && copy.lap>0) {
                observation.connections=target.rows[copy.row].connections;
                observation.connections_detailed=target.rows[copy.row].connections_detailed;
                observation.connection_radius=target.rows[copy.row].connection_radius;
                observation.passages=target.rows[copy.row].passages;
                observation.native_strips=target.rows[copy.row].native_strips;
                observation.probe_connections=target.rows[copy.row].probe_connections;
                observation.connection_impacts=target.rows[copy.row].connection_impacts;
                auto section=observation;section.reason=target.rows[copy.row].reason;
                if(section==target.rows[copy.row])observation.reason=section.reason;
            }
            // The map API has already resolved conflicting evidence. Copy the
            // full row without applying ReplaceRow's impact-retention again.
            if(Assign(target,copy.row,observation)) {
                binding_.captured_rows[copy.profile].push_back(copy.row);++changed;
            } else changed+=incoming_changed;
        }
        for(int profile=0;profile<3;++profile)binding_.captured_revisions[profile]=maps[profile].revision;
        return changed;
    }
    // Refresh only profiles whose canonical evidence changed. No resampling,
    // new world queries, or replacement of the frozen race's path is needed.
    bool NoteCanonicalRows(int profile,const std::vector<std::size_t>& rows,std::uint64_t before) {
        if(profile<0||profile>=3||before!=binding_.captured_revisions[profile]||
            maps[profile].revision-before!=rows.size())return false;
        for(auto row:rows)if(row>=maps[profile].rows.size())return false;
        auto& changed=binding_.captured_rows[profile];changed.insert(changed.end(),rows.begin(),rows.end());
        binding_.captured_revisions[profile]=maps[profile].revision;return true;
    }
    std::size_t Refresh(const lap::Plan& plan,obstacle_map::Map& expanded) {
        last_refresh_rows=0;
        if(!BoundTo(plan,expanded))return 0;
        std::size_t updated=0;bool changed=false;
        const auto publish=[&](int profile,std::size_t row) {
            ++last_refresh_rows;
            for(const auto index:binding_.destinations[profile][row]) {
                const auto copy=binding_.copies[index];
                if(copy.row==0 && copy.lap>0) {
                    if(Assign(expanded,index,ExpandedRow(copy)))++updated;
                } else if(Assign(expanded,index,maps[profile].rows[row]))++updated;
            }
        };
        for(int profile=0;profile<3;++profile) {
            if(binding_.revisions[profile]==maps[profile].revision)continue;
            changed=true;
            if(!binding_.captured_rows[profile].empty() &&
                binding_.captured_revisions[profile]==maps[profile].revision) {
                // Every revision since publication came from this Capture;
                // translate only its changed source rows into the race copies.
                for(const auto row:binding_.captured_rows[profile])publish(profile,row);
            } else {
                // A survey or another direct canonical writer may have changed
                // any row before OR after Capture. Compare the whole profile.
                for(std::size_t row=0;row<maps[profile].rows.size();++row)publish(profile,row);
            }
            binding_.captured_rows[profile].clear();
            binding_.revisions[profile]=binding_.captured_revisions[profile]=maps[profile].revision;
        }
        for(const auto& row:seam_dirty_)for(const bool dirty:row)changed|=dirty;
        // Either endpoint's physical evidence can restrict a bridge. A source
        // last-row mutation must also refresh a successor in another profile.
        if(changed)for(const auto index:binding_.seam_rows)
            if(Assign(expanded,index,ExpandedRow(binding_.copies[index])))++updated;
        seam_dirty_={};
        return updated;
    }

private:
    struct Copy {int profile=0,lap=0;std::size_t row=0;int previous_profile=-1;};
    std::array<std::array<obstacle_seams::Certificate,3>,3> seams_;
    // Actual bridge impacts are kept separately from canonical row-zero data.
    // They live only with this geometry bank, never in a standalone shape file.
    std::array<std::array<std::vector<obstacle_map::Interval>,3>,3> seam_impacts_;
    std::array<std::array<bool,3>,3> seam_dirty_{};
    bool seams_loaded_=false;
    struct Binding {
        std::string key,geometry;
        std::vector<float> offsets;
        std::vector<Copy> copies;
        std::vector<std::size_t> seam_rows;
        std::array<std::vector<std::vector<std::size_t>>,3> destinations;
        std::array<std::vector<std::size_t>,3> captured_rows;
        std::array<std::uint64_t,3> revisions{},captured_revisions{};
    } binding_;
    bool BoundTo(const lap::Plan& plan,const obstacle_map::Map& expanded) const {
        return !binding_.key.empty() && binding_.key==expanded.key && binding_.geometry==expanded.geometry_hash &&
            binding_.offsets==plan.offsets && binding_.copies.size()==expanded.rows.size();
    }
    obstacle_map::Row ExpandedRow(Copy copy) const {
        auto observation=maps[copy.profile].rows[copy.row];
        if(copy.row==0 && copy.lap>0) {
            observation.connections.clear();observation.connections_detailed=true;
            observation.passages.clear();observation.connection_impacts.clear();
            observation.native_strips.clear();
            observation.probe_connections.clear();
            observation.connection_radius=native_corridor::kBodyRadius;
            observation.connection_impacts=seam_impacts_[copy.previous_profile][copy.profile];
            const auto& link=seams_[copy.previous_profile][copy.profile];
            if(link.loaded&&maps[copy.previous_profile].native_loaded&&maps[copy.profile].native_loaded) {
                const auto from=maps[copy.previous_profile].rows[link.from_row].ClearIntervals(native_corridor::kBodyRadius);
                const auto to=observation.ClearIntervals(native_corridor::kBodyRadius);
                for(const auto& strip:link.strips)
                    for(const auto a:obstacle_map::Intersect({strip.from},from))
                        for(const auto b:obstacle_map::Intersect({strip.to},to))
                            obstacle_map::AddPassage(observation.native_strips,{a,b,strip.min_normal});
            }
        }
        return observation;
    }
    static bool Assign(obstacle_map::Map& target,std::size_t row,const obstacle_map::Row& observation) {
        if(target.rows[row]==observation)return false;
        return target.StoreRow(row,observation);
    }
};
} // namespace horse_races::obstacle_profiles
