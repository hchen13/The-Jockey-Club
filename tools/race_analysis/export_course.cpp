// Offline only: use the production route expansion and obstacle interpretation.
// No game process, natives, save writes or reconstructed racing-line solver.
#include "../../src/race_route_config.h"
#include "../../src/race_obstacle_map.h"
#include "../../src/race_racing_line.h"
#include "../../src/race_obstacle_profiles.h"
#include "../../src/race_native_corridor.h"
#include "../../src/race_line_pins.h"
#include "../../src/race_recorded_reference.h"
#include "../../src/race_terrain_line.h"
#include "bounded_output.h"
#ifdef TJC_ANALYSIS_CANDIDATES
#include "candidate_solver.h"
#endif
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace horse_races;
namespace {
std::string Read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+p.string());
    return {std::istreambuf_iterator<char>{f},{}};
}
void String(std::ostream& o,const std::string& s) {
    o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c=='\n')o<<"\\n";
        else if(c=='\r')o<<"\\r";else if(c=='\t')o<<"\\t";else if(c<32)o<<' ';else o<<c;}o<<'"';
}
void Spans(std::ostream& o,const std::vector<obstacle_map::Interval>& spans) {
    o<<'[';for(std::size_t i=0;i<spans.size();++i){if(i)o<<',';o<<'['<<spans[i].lo<<','<<spans[i].hi<<']';}o<<']';
}
void Strips(std::ostream& o,const std::vector<obstacle_gap_view::Strip>& strips,float width) {
    o<<'[';bool first=true;
    for(auto strip:strips) {
        strip.from.lo=std::max(strip.from.lo,-width);strip.from.hi=std::min(strip.from.hi,width);
        strip.to.lo=std::max(strip.to.lo,-width);strip.to.hi=std::min(strip.to.hi,width);
        if(strip.from.lo>strip.from.hi||strip.to.lo>strip.to.hi)continue;
        if(!first)o<<',';first=false;
        o<<'['<<strip.from.lo<<','<<strip.from.hi<<','<<strip.to.lo<<','<<strip.to.hi<<']';
    }
    o<<']';
}
void Path(std::ostream& o,const course::Path& p) {
    o<<'[';for(std::size_t i=0;i<p.points.size();++i){if(i)o<<',';
        o<<'['<<p.metres[i]<<','<<p.points[i].x<<','<<p.points[i].y<<','<<p.points[i].z<<']';}o<<']';
}
void RecordedReference(std::ostream& o,const recorded_reference::Prepared& prepared,const racing_line::Result& line) {
    o<<",\"recorded_reference\":{\"active\":"<<line.recorded_reference_applied<<",\"status\":";String(o,prepared.Status());
    if(line.recorded_reference_applied) {
        const auto& source=*prepared.Source();const auto& core=prepared.CoreNodes();const auto& along=prepared.CanonicalMetres();
        o<<",\"source\":";String(o,std::string(source.source));o<<",\"source_sha256\":";String(o,std::string(source.source_sha256));
        o<<",\"canonical_geometry_hash\":";String(o,std::string(source.canonical_geometry));
        o<<",\"core_from_metres\":"<<source.from<<",\"core_to_metres\":"<<source.to
         <<",\"fixed_from_metres\":"<<along[core.front()]<<",\"fixed_to_metres\":"<<along[core.back()]
         <<",\"source_first_sample\":"<<source.source_first_sample<<",\"source_point_count\":"<<source.points.size()
         <<",\"fixed_nodes\":"<<core.size()<<",\"core_points\":[";
        for(std::size_t k=0;k<core.size();++k){if(k)o<<',';const auto i=core[k];const auto q=prepared.Path().points[i];o<<'['<<along[i]<<','<<q.x<<','<<q.y<<','<<q.z<<']';}
        o<<']';
    }
    o<<'}';
    if(line.recorded_reference_applied) {
        o<<",\"reference_path\":[";
        for(std::size_t i=0;i<line.reference.size();++i){if(i)o<<',';const auto q=prepared.Path().points[i];o<<'['<<line.reference[i]<<','<<q.x<<','<<q.y<<','<<q.z<<']';}
        o<<"],\"work_offsets\":[";for(std::size_t i=0;i<line.offsets.size();++i){if(i)o<<',';o<<line.offsets[i];}o<<']';
    }
}
}
int main(int argc,char** argv) {
    try {
        bool live=false;std::string shape_folder,native_folder,pins_file;float caution=-1;
#ifdef TJC_ANALYSIS_CANDIDATES
        analysis_candidates::Optimizer optimizer;
#endif
        std::vector<std::string> arguments{argv[0]};
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--live")live=true;
#ifdef TJC_ANALYSIS_CANDIDATES
            else if(option=="--algorithm"||option=="--seed"||option=="--constraints") {
                if(++i>=argc)throw std::runtime_error("missing value for "+option);
                if(option=="--algorithm")optimizer.kind=argv[i];
                else if(option=="--seed")optimizer.seed_file=argv[i];
                else optimizer.constraints_file=argv[i];
            }
#endif
            else if(option=="--profiles"||option=="--shapes"||option=="--native-profiles"||option=="--pins"||option=="--terrain-caution") {
                if(++i>=argc)throw std::runtime_error("missing value for "+option);
                if(option=="--profiles"||option=="--shapes")shape_folder=argv[i];
                else if(option=="--native-profiles")native_folder=argv[i];
                else if(option=="--pins")pins_file=argv[i];else caution=std::stof(argv[i]);
            } else arguments.push_back(option);
        }
        std::vector<char*> pointers;for(auto& value:arguments)pointers.push_back(value.data());
        argc=static_cast<int>(pointers.size());argv=pointers.data();
#ifdef TJC_ANALYSIS_CANDIDATES
        optimizer.Validate();
#endif
        if(!std::isfinite(caution)||caution< -1||caution>1)throw std::runtime_error("invalid terrain caution variant");
        if(argc!=9 && argc!=13)throw std::runtime_error("usage: race_analysis_export route.ini survey.txt|- station total_laps half_width margin body_clearance output.json [aggression raw_speed raw_acceleration lateral]");
        const int total=std::stoi(argv[4]);const float half=std::stof(argv[5]),margin=std::stof(argv[6]),body=std::stof(argv[7]);
        if(!std::isfinite(half)||!std::isfinite(margin)||!std::isfinite(body)||half<=0||margin<0||body<0)
            throw std::runtime_error("invalid clearance parameters");
        if(live&&!tracking_margin::Valid(margin))throw std::runtime_error("current analysis tracking margin must be within the production 0..5 metre range");
        std::string error;const auto route=route_config::Parse(Read(argv[1]),error);
        if(!route)throw std::runtime_error("route parse: "+error);
        lap::Plan plan;course::Path path;std::vector<course::Path> profiles;std::vector<float> offsets;std::vector<int> order;
        std::vector<std::string> names;
        if(route->circuit) {
            const bool single=route->circuit->single_loop||route->circuit->single_trip;
            if(total<1||!plan.Build(*route->circuit,single?0:total-2)||plan.Total()!=total)
                throw std::runtime_error("invalid lap expansion");
            path=plan.course.path;offsets=plan.offsets;
            for(int i=0;i<3;++i)profiles.push_back(plan.profiles[i]);
            names={"first","regular","final"};for(int i=0;i<plan.Total();++i)order.push_back(plan.Profile(i));
        } else {
            if(total!=1)throw std::runtime_error("a recorded destination course has one run");
            if(!path.Build(route->course_points))throw std::runtime_error("no recorded course: free-route races need an explicit recorded reference");
            profiles={path};offsets={0};order={0};names={"course"};
        }
        obstacle_map::Map map;map.Bind(argv[3],path,20,obstacle_map::kDetailedVersion);
        bool has_survey=std::string(argv[2])!="-";
        if(live&&has_survey)throw std::runtime_error("live analysis requires profile-bank evidence; expanded snapshots are historical only");
        if(has_survey&&!map.Parse(Read(argv[2])))throw std::runtime_error("survey key/geometry differs from supplied route and lap count");
        std::vector<std::string> survey_files,native_files,native_seam_files;
        if(has_survey)survey_files.push_back(argv[2]);
        obstacle_profiles::Bank shapes;
        std::array<bool,3> profiles_loaded{};
        std::array<native_corridor::LoadResult,3> native_status;
        const bool use_profiles=route->circuit&&(live||!shape_folder.empty()||!native_folder.empty());
        if(use_profiles) {
            if(!shapes.Bind(argv[3],plan,20,obstacle_map::kDetailedVersion))throw std::runtime_error("cannot bind shape maps");
            for(std::size_t profile=0;profile<shapes.maps.size();++profile) {
                auto& source=shapes.maps[profile];
                if(!native_folder.empty()||live) {
                    if(!native_folder.empty())native_status[profile]=native_corridor::Load(source,shapes.paths[profile],native_folder);
                    const auto& status=native_status[profile];
                    profiles_loaded[profile]=status.native_loaded||status.evidence_loaded;
                    if(!native_folder.empty()&&std::filesystem::exists(status.native_path))native_files.push_back(status.native_path.string());
                    if(!native_folder.empty()&&std::filesystem::exists(status.evidence_path))survey_files.push_back(status.evidence_path.string());
                } else {
                    // Explicit historical profile input only; never current live evidence.
                    const auto file=std::filesystem::path(shape_folder)/(source.key+".txt");
                    if(std::filesystem::exists(file)) {
                        if(!source.Parse(Read(file)))throw std::runtime_error("shape cache geometry mismatch: "+file.string());
                        obstacle_profiles::Bank::DiscardLegacyClearance(source);
                        source.ForgetIsolated();profiles_loaded[profile]=true;survey_files.push_back(file.string());
                    }
                }
            }
            if(!native_folder.empty()) {
                const auto native=std::filesystem::path(native_folder)/"native";shapes.LoadSeams(native);
                for(int from:{0,1})for(int to:{1,2}) {
                    const auto file=native/"seams"/(obstacle_seams::Key(shapes.maps[from],shapes.maps[to])+".txt");
                    if(std::filesystem::exists(file))native_seam_files.push_back(file.string());
                }
            }
            if(!shapes.Expand(plan,map))throw std::runtime_error("cannot expand original shape measurements");
            has_survey=map.Surveyed()>0;
        }
        const bool use_destination=!route->circuit&&(live||!native_folder.empty());
        if(use_destination) {
            if(!native_folder.empty())native_status[0]=native_corridor::Load(map,path,native_folder);
            const auto& status=native_status[0];profiles_loaded[0]=status.native_loaded||status.evidence_loaded;
            if(!native_folder.empty()&&std::filesystem::exists(status.native_path))native_files.push_back(status.native_path.string());
            if(!native_folder.empty()&&std::filesystem::exists(status.evidence_path))survey_files.push_back(status.evidence_path.string());
            has_survey=map.Surveyed()>0;
        }
        racing_line::Result line;
        recorded_reference::Prepared prepared_reference;
        bool pins_applied=false,required_pins_applied=false;std::string pins_status;std::size_t pins_refused=0;
        course::Path dense;
        racing_line::Params params;
        params.half_width=half;params.margin=margin;params.obstacle_clearance=body;
        std::size_t forgotten=0;
        int raw_speed=0,raw_acceleration=0;
        if(argc==13) {
            if(!has_survey&&!live)throw std::runtime_error("offline replan requires an explicit survey snapshot");
            params.aggression=std::stof(argv[9]);
            raw_speed=std::stoi(argv[10]);raw_acceleration=std::stoi(argv[11]);
            if(raw_speed<0||raw_speed>9||raw_acceleration<0||raw_acceleration>9||
                !std::isfinite(params.aggression)||params.aggression<0||params.aggression>1)
                throw std::runtime_error("invalid planning attributes/aggression");
            params.dynamics=race_pace::ForAttributes(raw_speed+1,raw_acceleration+1);
            params.dynamics.lateral=std::stof(argv[12]);
            params.closed=route->circuit&&route->circuit->single_loop;
            params.obstacles=&map;
            params.obstacle_path_starts=&offsets;
            if(!use_profiles)forgotten=map.ForgetIsolated(); // Profiles were cleaned before expansion.
            std::vector<line_pins::Stretch> stretches;line_pins::File pin_source;
            if(!pins_file.empty()&&std::filesystem::exists(pins_file)) {
                std::string why;
                if(!pin_source.Parse(Read(pins_file),&why))throw std::runtime_error("invalid pins: "+why);
                const auto projected=line_pins::Project(path,pin_source,half);
                if(projected.required_refused)throw std::runtime_error("required recorded centreline projection refused");
                stretches=projected.stretches;pins_refused=projected.refused.size();
            }
            if(!racing_line::Densify(path,8.0F,dense)||
               !prepared_reference.Prepare(path,dense,params,argv[3],stretches,pin_source))
                throw std::runtime_error("recorded reference refused: "+prepared_reference.Status());
            params=prepared_reference.Policy();const auto& planning_path=prepared_reference.Path();
            const auto& planning_pins=prepared_reference.Pins();pins_refused+=prepared_reference.RefusedPins();
            line_pins::Applied applied;
            const auto solve=[&] {
#ifdef TJC_ANALYSIS_CANDIDATES
                if(optimizer.kind!="current")return line_pins::Build(planning_path,params,planning_pins,line,&applied,optimizer);
#endif
                return line_pins::Build(planning_path,params,planning_pins,line,&applied);
            };
            if(!solve())
                throw std::runtime_error("production racing-line solver rejected the course/parameters");
            pins_applied=applied.ok&&!planning_pins.empty()&&(!prepared_reference.Active()||line.authored_constraints);
            if(!planning_pins.empty())pins_status=applied.why;
            if(caution>=.5F) {
                terrain::Profile terrain;
                // Match runtime: a refused optional overlay retains the
                // computed line; it cannot override current obstacle evidence.
                if(terrain.Build(planning_path))terrain::FollowRecorded(planning_path,params,terrain,line);
            }
            if(pins_applied&&!line_pins::Matches(planning_path,params,planning_pins,line)) {
                pins_applied=false;line.authored_constraints=false;
                pins_status="final geometry no longer follows all authored nodes";
            }
            if(!line_pins::Matches(planning_path,params,planning_pins,line,true))
                throw std::runtime_error("required recorded centreline changed after terrain overlay");
            required_pins_applied=line_pins::Required(planning_pins);
            if(!prepared_reference.Finish(line))throw std::runtime_error("recorded reference refused: "+prepared_reference.Status());
        }
        obstacle_gap_view::View estimated;
        std::vector<racing_line::detail::SectionBound> selected_rows;
        if(argc==13) {
            const auto& planning_path=prepared_reference.Path();
            const auto& starts=params.obstacle_path_starts?*params.obstacle_path_starts:offsets;
            estimated=obstacle_gap_view::Build(*params.obstacles,planning_path,starts);
            std::vector<racing_line::detail::SectionBound> sections;
            std::vector<unsigned char> recorded;bool unconstrained=false;
            racing_line::detail::ObstacleSections(planning_path,estimated.map,params.ProofClearance(),params.Usable()-.01F,
                sections,selected_rows,recorded,unconstrained,&estimated);
        }
        analysis_output::Buffer output;std::ostream o(&output);
        o<<std::setprecision(9)<<std::boolalpha;
        o<<"{\"schema\":\"race-analysis.course.v1\",\"survey_available\":"<<has_survey<<",\"cache_key\":";String(o,map.key);
        o<<",\"geometry_hash\":";String(o,map.geometry_hash);o<<",\"length\":"<<path.Length()<<",\"node_count\":"<<path.points.size();
        o<<",\"survey_step_metres\":"<<obstacle_map::kStepMetres<<",\"runtime_parts\":"<<order.size()
         <<",\"single_loop\":"<<(route->circuit&&route->circuit->single_loop)<<",\"survey_files\":[";
        for(std::size_t i=0;i<survey_files.size();++i){if(i)o<<',';String(o,survey_files[i]);}o<<']';
        o<<",\"native_files\":[";for(std::size_t i=0;i<native_files.size();++i){if(i)o<<',';String(o,native_files[i]);}o<<']';
        o<<",\"native_seam_files\":[";for(std::size_t i=0;i<native_seam_files.size();++i){if(i)o<<',';String(o,native_seam_files[i]);}o<<']';
        o<<",\"survey_source\":";String(o,use_profiles?(live||!native_folder.empty()?"native-profile-bank":"historical-profile-bank"):use_destination?"native-profile-bank":has_survey?"historical-expanded-snapshot":"none");
        o<<",\"profile_caches\":[";
        for(std::size_t i=0;i<(use_profiles?shapes.maps.size():use_destination?1:0);++i) {
            if(i)o<<',';const auto& source=use_profiles?shapes.maps[i]:map;
            const auto physical_rows=std::count_if(source.rows.begin(),source.rows.end(),[](const auto& row){return !row.passed.empty()||!row.passages.empty()||!row.impacts.empty()||!row.connection_impacts.empty();});
            o<<"{\"id\":";String(o,names[i]);o<<",\"key\":";String(o,source.key);
            o<<",\"geometry_hash\":";String(o,source.geometry_hash);
            o<<",\"length\":"<<source.length<<",\"row_count\":"<<source.rows.size()
             <<",\"version\":"<<source.version<<",\"loaded\":"<<profiles_loaded[i]
             <<",\"measured_rows\":"<<source.Surveyed()
             <<",\"native_loaded\":"<<native_status[i].native_loaded
             <<",\"evidence_loaded\":"<<native_status[i].evidence_loaded
             <<",\"physical_rows\":"<<physical_rows
             <<",\"native_source\":";String(o,source.native_source);
            o<<",\"load_error\":";String(o,native_status[i].error);o<<'}';
        }
        o<<']';
        o<<",\"parameters\":{\"half_width\":"<<half<<",\"margin\":"<<margin<<",\"obstacle_clearance\":"<<body
         <<",\"keep\":"<<params.ObstacleReserve()<<",\"proof_clearance\":"<<params.ProofClearance()
         <<",\"usable_half_width\":"<<params.Usable()<<"},\"path\":";Path(o,path);
        o<<",\"lap_offsets\":[";for(std::size_t i=0;i<offsets.size();++i){if(i)o<<',';o<<offsets[i];}o<<','<<path.Length()<<']';
        if(argc==13) {
            o<<",\"planning\":{\"kind\":\"offline-replan\",\"aggression\":"<<params.aggression
             <<",\"raw_speed\":"<<raw_speed<<",\"raw_acceleration\":"<<raw_acceleration
             <<",\"lateral\":"<<params.dynamics.lateral<<",\"discarded_isolated_rows\":"<<forgotten
             <<",\"recorded_segments\":"<<line.recorded_segments
             <<",\"recorded_branch_segments\":"<<line.recorded_branch_segments
             <<",\"recorded_projection_segments\":"<<line.recorded_projection_segments
             <<",\"verification_rounds\":"<<line.verification_rounds
             <<",\"total_segments\":"<<line.path.points.size()-1
             <<",\"recorded_fallback\":"<<line.recorded_fallback<<",\"verified\":"<<line.verified
             <<",\"pins_requested\":"<<!pins_file.empty()<<",\"pinned_nodes\":"<<line.pinned_nodes<<",\"terrain_caution\":"<<caution
             <<",\"pins_applied\":"<<pins_applied<<",\"required_pins_applied\":"<<required_pins_applied<<",\"pins_refused\":"<<pins_refused<<",\"pins_status\":";String(o,pins_status);
            o<<",\"pins_strategy\":";String(o,pins_applied?(params.Aggression()>0?"joint-constraints":"recorded-overlay"):"none");
            o<<",\"recorded_mask_scope\":\"uncertified planner chords; authored fixed nodes and optional terrain overlay reported separately\""
             <<",\"obstacle_verified\":"<<line.obstacle_verified<<",\"max_offset\":"<<line.max_offset
             <<",\"estimated_rows\":"<<line.estimated_rows<<",\"estimated_links\":"<<line.estimated_links
             <<",\"recorded_chords\":[";
            for(std::size_t i=0;i<line.recorded_chords.size();++i){if(i)o<<',';o<<static_cast<int>(line.recorded_chords[i]);}
            o<<']'
             <<",\"peak_node_angle_deg\":"<<racing_line::detail::PeakNodeAngleDegrees(line.path)
             <<",\"optimizer\":";String(o,line.optimizer);
            o<<",\"seam_curvature_sweeps\":"<<line.seam_curvature_sweeps
             <<",\"refinement_candidates\":"<<line.refinement_candidates
             <<",\"refinement_nodes\":"<<line.refinement_nodes
             <<",\"reference_refinements\":"<<line.reference_refinements
             <<",\"curvature_sweeps\":"<<line.curvature_sweeps
             <<",\"accepted_passes\":"<<line.accepted_passes<<",\"linear_solves\":"<<line.iterations
             <<",\"objective_before\":"<<line.objective_before<<",\"objective_after\":"<<line.objective_after
             <<",\"objective_final\":"<<line.objective_final
             <<",\"milliseconds\":"<<line.milliseconds<<",\"points\":[";
            for(std::size_t i=0;i<line.path.points.size();++i) {
                if(i)o<<',';const auto& p=line.path.points[i];
                const auto& lateral=line.canonical_offsets.size()==line.offsets.size()?line.canonical_offsets:line.offsets;
                o<<'['<<line.reference[i]<<','<<p.x<<','<<p.y<<','<<p.z<<','<<lateral[i]<<']';
            }
            o<<']';RecordedReference(o,prepared_reference,line);o<<'}';
        }
        o<<",\"profiles\":[";
        bool first_profile=true;
        for(std::size_t pi=0;pi<profiles.size();++pi) {
            if(std::find(order.begin(),order.end(),static_cast<int>(pi))==order.end())continue;
            if(!first_profile)o<<',';first_profile=false;o<<"{\"id\":";String(o,names[pi]);o<<",\"length\":"<<profiles[pi].Length()<<",\"path\":";Path(o,profiles[pi]);
            o<<",\"fixed_obstacles\":[";
            if(use_profiles&&!native_folder.empty()&&std::string_view(argv[3])=="blackwater") {
                bool first_obstacle=true;
                for(const auto& post:authored_obstacles::kBlackwaterPosts) {
                    if(!first_obstacle)o<<',';first_obstacle=false;
                    o<<"{\"x\":"<<post.centre.x<<",\"y\":"<<post.centre.y<<",\"z\":"<<post.centre.z<<",\"radius\":"<<post.radius<<'}';
                }
            }
            o<<']';
            o<<",\"observations\":[";bool first_lap=true;
            for(std::size_t li=0;li<order.size();++li) {
                if(order[li]!=pi)continue;if(!first_lap)o<<',';first_lap=false;
                const float start=offsets[li],end=li+1<offsets.size()?offsets[li+1]:path.Length();
                o<<"{\"lap\":"<<li+1<<",\"global_start\":"<<start<<",\"global_end\":"<<end<<",\"rows\":[";bool first_row=true;
                for(std::size_t ri=0;ri<map.rows.size();++ri) {
                    const float s=map.Along(ri);if(s<start||(s>=end&&li+1<order.size()))continue;
                    if(!first_row)o<<',';first_row=false;const auto& row=map.rows[ri];
                    const auto frame=map.SectionFor(path,ri);
                    o<<"{\"index\":"<<ri<<",\"s\":"<<s-start<<",\"global_s\":"<<s
                     <<",\"x\":"<<frame.centre.x<<",\"y\":"<<frame.centre.y<<",\"z\":"<<frame.centre.z
                     <<",\"nx\":"<<frame.nx<<",\"ny\":"<<frame.ny<<",\"legacy\":"<<row.legacy<<",\"detailed\":"<<row.detailed
                     <<",\"reason\":"<<row.reason<<",\"walk\":";
                    // Section lateral coordinates are physical certificate coordinates;
                    // legal width is enforced in world geometry by the production planner/view mask.
                    Spans(o,row.ClearIntervals(params.ProofClearance()));
                    o<<",\"link\":";Spans(o,map.ConnectionIntervals(ri,params.ProofClearance()));
                    o<<",\"raw\":";Spans(o,row.ClearIntervals());o<<",\"unknown\":";Spans(o,row.unknown);
                    o<<",\"raw_connections\":";Spans(o,row.connections);o<<",\"connection_radius\":"<<row.connection_radius;
                    o<<",\"passed\":";Spans(o,row.passed);
                    o<<",\"fixed_links\":";Spans(o,row.authored_links);
                    o<<",\"probe_width\":"<<row.probe_half_width<<",\"probe_raw\":";Spans(o,row.probe_intervals);
                    o<<",\"probe_connections\":";Spans(o,row.probe_connections);
                    o<<",\"probe_unknown\":";Spans(o,row.probe_unknown);
                    o<<",\"passage_strips\":";Strips(o,obstacle_gap_view::MeasuredStrips(map,ri),INFINITY);
                    // Current native display exposes every certified physical branch.
                    // The planner's selected branch remains in usable/usable_strips.
                    if(map.native_mode) {
                        o<<",\"available\":";Spans(o,row.ClearIntervals(params.ProofClearance()));
                        o<<",\"available_strips\":";Strips(o,obstacle_gap_view::MeasuredStrips(map,ri,params.ProofClearance()),INFINITY);
                    }
                    if(argc==13) {
                        o<<",\"estimate_walk\":";Spans(o,estimated.estimated_rows[ri]?
                            estimated.map.rows[ri].ClearIntervals(params.ProofClearance()):std::vector<obstacle_map::Interval>{});
                        o<<",\"estimate_link\":";Spans(o,estimated.estimated_links[ri]?
                            estimated.map.rows[ri].ClearIntervals(params.ProofClearance()):std::vector<obstacle_map::Interval>{});
                        o<<",\"estimate_strips\":";Strips(o,estimated.estimated_links[ri]?
                            obstacle_gap_view::EffectiveStrips(estimated.map,&estimated,ri,params.ProofClearance()):std::vector<obstacle_gap_view::Strip>{},INFINITY);
                        racing_line::detail::SectionBound unavailable;unavailable.recorded=true;unavailable.incoming_recorded=true;
                        const auto& selected=ri<selected_rows.size()?selected_rows[ri]:unavailable;
                        o<<",\"usable\":";Spans(o,selected.recorded?std::vector<obstacle_map::Interval>{}:
                            std::vector<obstacle_map::Interval>{{static_cast<float>(selected.lo),static_cast<float>(selected.hi)}});
                        std::vector<obstacle_gap_view::Strip> usable;
                        if(ri<selected_rows.size()&&ri&&!selected.incoming_recorded) {
                            usable.push_back({{static_cast<float>(selected.incoming_from_lo),static_cast<float>(selected.incoming_from_hi)},
                                {static_cast<float>(selected.incoming_lo),static_cast<float>(selected.incoming_hi)}});
                        }
                        o<<",\"usable_strips\":";Strips(o,usable,INFINITY);
                        o<<",\"estimated\":"<<static_cast<bool>(estimated.estimated_rows[ri]||estimated.estimated_links[ri]||
                            (ri&&estimated.estimated_rows[ri-1]));
                    } else {
                        // The survey view consumes the same measured strips as
                        // the runtime planner, including actual local passages.
                        o<<",\"usable\":";Spans(o,row.ClearIntervals(params.ProofClearance()));
                        o<<",\"usable_strips\":";Strips(o,obstacle_gap_view::MeasuredStrips(map,ri,params.ProofClearance()),INFINITY);
                        o<<",\"estimated\":false";
                    }
                    o<<'}';
                }
                o<<"]}";
            }
            o<<"]}";
        }
        o<<"]}\n";if(!o)throw std::runtime_error("analysis output exceeds 32 MiB capacity; previous output preserved");
        analysis_output::Publish(argv[8],output.Bytes());
        std::cout<<map.key<<" geometry="<<map.geometry_hash<<" length="<<path.Length()<<'\n';return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
