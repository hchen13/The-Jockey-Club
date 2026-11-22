// The scene survey's natives: pin every known model near the player, list
// the pools, probe the camp's map files, write the file. See scene_survey.h.
//
// A map entity is pinned in two steps, as the camp scripts do it:
// PIN_CLOSEST_MAP_ENTITY hands back a pin that the engine fills in on a
// later frame (IS_MAP_ENTITY_PINNED), and only then _GET_PINNED_MAP_ENTITY
// is an entity whose pose can be read. Pins are granted one after another,
// so only a few are kept in flight and each is given ten seconds.
#include "scene_survey.h"
#include "runtime_paths.h"
#include "kernel/files.h"
#include "scene_survey_models.h"
#include "data/scene_survey_hashes.inc"
#include "scene_survey_imaps.h"
#include "scene_survey_runtime.h"
#include <main.h>
#include "game_natives.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace horse_races::scene_survey {
namespace {
constexpr int kPinFlags=15;          // What the camp scripts pass.
constexpr int kAttemptsPerFrame=48;
constexpr int kMaxPending=6;
constexpr int kPinPatienceFrames=600;
constexpr std::size_t kQueryRing=8;   // The player plus eight points 7 m out: a second chair is found too.
// The map-entity probe: GET_CLOSEST_OBJECT_OF_TYPE for every object model the
// game knows, from every query point, this many calls a frame (nine points
// and 20471 models take about four seconds; the game keeps running).
constexpr int kProbePerFrame=800;
constexpr float kProbeRadius=10.0F;
// The Horseshoe Overlook camp's map files, as camp_horseshoeoverlook.ysc
// names them (hashes; a name where one is known). Which of them is active
// while the player stands in the camp says which file holds what.
struct Ipl { int hash; const char* note; };
constexpr std::array<Ipl,25> kCampIpls{{
    {970924250,"hso_p_camp_02"},{-1166561064,"dutch tent lights A"},{172011504,"dutch tent lights B"},{-1365193577,"dutch tent lights C"},
    {163126540,"lights (-116,-36)"},{-1611076340,"camp arrangement (func_900)"},{-1920340119,"lights (-122,-22)"},{44502487,""},
    {825179479,""},{-1437554707,""},{1157066259,""},{1261237250,""},{1102643191,""},{1475953931,""},{-1788578071,""},
    {300868838,""},{1351589798,""},{-1962893335,""},{-316909020,""},{1070723367,""},{-109425099,""},
    {2108368013,"camp 2 arrangement"},{-412827149,"camp 3 arrangement"},{-1395712024,"camp 5 arrangement"},{-1162254606,"camp 6 arrangement"}}};
struct Pin { int handle=0; const char* model=nullptr; int waited=0; };
struct Outcome { int issued=0,granted=0,timeouts=0; };
struct Job {
    bool running=false;
    bool probe_map_entities=false;
    std::string directory;
    float radius=60;
    LogFn log=nullptr;
    Point player;
    Ped ped=0;
    std::vector<Point> points;
    std::size_t cursor=0;               // Over points.size()*kModels.size().
    std::vector<Pin> pending;
    std::vector<Record> records;
    std::map<std::string,Outcome> outcomes;
    std::vector<std::string> notes;     // Comment lines for the file: map files, pin outcomes.
    int pinned=0,pins=0,timeouts=0,frames=0;
    std::size_t probe_cursor=0;         // Over points.size()*kAllModelHashes.size().
    int probed=0,probe_frames=0;
    bool pools_read=false;
    int objects=0,peds=0,vehicles=0;    // What the pools held.
};
Job g_job;
void Log(const std::string& s){if(g_job.log)g_job.log("scene survey: "+s);}
Record Read(const char* kind,int handle,Point player) {
    Record r;r.kind=kind;r.handle=handle;
    r.model=static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(handle));
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(handle,true,false);
    r.x=p.x;r.y=p.y;r.z=p.z;
    r.heading=ENTITY::GET_ENTITY_HEADING(handle);
    const Vector3 rot=ENTITY::GET_ENTITY_ROTATION(handle,2);
    r.pitch=rot.x;r.roll=rot.y;r.yaw=rot.z;
    r.distance=std::hypot(p.x-player.x,p.y-player.y,p.z-player.z);
    if(ENTITY::IS_ENTITY_ATTACHED(handle))r.extra="attached";
    return r;
}
std::string Stamp() {
    SYSTEMTIME now{};GetLocalTime(&now);
    char b[64];std::snprintf(b,sizeof b,"%04d-%02d-%02d %02d:%02d:%02d",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
    return b;
}
// Every map file the shipped scripts ever request, asked for its sphere:
// the ones whose sphere reaches within kSweepMetres of the player are
// written with their state, so a camp piece's map file can be found by
// where it is instead of by a guessed name (Arthur's wagon, 2026-09-15).
constexpr float kSweepMetres=120.0F;
void SweepImaps(std::vector<std::string>& notes,Point player,LogFn log) {
    int close_by=0,active_near=0;
    for(int hash:kAllScriptImaps) {
        const Hash h=static_cast<Hash>(hash);
        Vector3 centre{};float radius=0;
        if(!STREAMING::_GET_IPL_BOUNDING_SPHERE(h,&centre,&radius))continue;
        const float reach=std::hypot(centre.x-player.x,centre.y-player.y)-radius;
        if(reach>kSweepMetres)continue;
        const bool on=STREAMING::IS_IPL_ACTIVE_HASH(h);
        char b[200];
        std::snprintf(b,sizeof b,"# imap %d active=%d centre=%.2f,%.2f,%.2f radius=%.1f distance=%.1f",hash,on?1:0,centre.x,centre.y,centre.z,radius,std::max(0.0F,reach));
        notes.emplace_back(b);++close_by;if(on)++active_near;
    }
    if(log)log("scene survey: "+std::to_string(close_by)+" of "+std::to_string(kAllScriptImaps.size())+" script map files reach within "+
        std::to_string(static_cast<int>(kSweepMetres))+" m, "+std::to_string(active_near)+" active; see the # imap lines");
}
void ProbeIpls() {
    std::string active;
    for(const auto& ipl:kCampIpls) {
        const Hash h=static_cast<Hash>(ipl.hash);
        const bool on=STREAMING::IS_IPL_ACTIVE_HASH(h);
        Vector3 centre{};float radius=0;
        const bool sphere=STREAMING::_GET_IPL_BOUNDING_SPHERE(h,&centre,&radius);
        char b[256];
        std::snprintf(b,sizeof b,"# ipl %d active=%d centre=%.2f,%.2f,%.2f radius=%.1f %s",ipl.hash,on?1:0,
            sphere?centre.x:0.0F,sphere?centre.y:0.0F,sphere?centre.z:0.0F,sphere?radius:0.0F,ipl.note);
        g_job.notes.emplace_back(b);
        if(on)active+=(active.empty()?"":", ")+std::to_string(ipl.hash);
    }
    Log("map files active here: "+(active.empty()?std::string("none of the known ones"):active));
    SweepImaps(g_job.notes,g_job.player,g_job.log);
}
// The pools within `radius` of `centre`, added to `records` (each instance
// once); the counts are what was added. Shared by the one-shot survey and
// the walking recording.
struct PoolCounts { int objects=0,peds=0,vehicles=0; };
// An entity is one record however often it is met: a ped that walks, or
// the lantern in its hand, was written at every step on 2026-09-15 (341 ped
// records for 17 peds).
bool Known(const std::vector<Record>& records,int handle) {
    for(const auto& have:records)if(have.handle==handle)return true;
    return false;
}
PoolCounts CollectPools(std::vector<Record>& records,std::vector<std::string>& notes,Point centre,float radius,Ped self) {
    // The whole pool, not its first 2048 entries: the camp's object pool ran
    // past that, and which 2048 came back differed from one run to the next
    // (the cot and the nightstand were in one survey and not the next).
    static std::vector<int> handles(16384);
    PoolCounts n;
    const int object_count=worldGetAllObjects(handles.data(),static_cast<int>(handles.size()));
    if(object_count>=static_cast<int>(handles.size()))notes.emplace_back("# WARNING: the object pool is larger than "+std::to_string(handles.size())+"; some objects were not seen");
    for(int i=0;i<object_count && i<static_cast<int>(handles.size());++i) {
        const int h=handles[static_cast<std::size_t>(i)];
        if(!h || Known(records,h) || !ENTITY::DOES_ENTITY_EXIST(h))continue;
        Record r=Read("object",h,centre);
        if(r.distance<=radius && Add(records,r))++n.objects;
    }
    std::vector<int> ped_handles;
    {
        constexpr int kMax=200;
        std::array<UINT64,1+2*kMax> list{};list[0]=kMax;
        const int found=PED::GET_PED_NEARBY_PEDS(self,reinterpret_cast<Any*>(list.data()),-1,-1);
        for(int stride:{1,2}) {
            std::vector<int> got;
            for(int i=0;i<found && i<kMax;++i) {
                const int h=static_cast<int>(list[static_cast<std::size_t>(1+i*stride)]);
                if(h && ENTITY::DOES_ENTITY_EXIST(h))got.push_back(h);
            }
            if(got.size()>ped_handles.size()) {
                ped_handles=got;
                const std::string note="# peds: neighbour list stride "+std::to_string(stride)+", "+std::to_string(found)+" listed";
                if(std::find(notes.begin(),notes.end(),note)==notes.end())notes.emplace_back(note);
            }
        }
        if(ped_handles.empty()) {
            const int ped_count=worldGetAllPeds(handles.data(),static_cast<int>(handles.size()));
            for(int i=0;i<ped_count && i<static_cast<int>(handles.size());++i)if(handles[static_cast<std::size_t>(i)])ped_handles.push_back(handles[static_cast<std::size_t>(i)]);
        }
    }
    for(const int h:ped_handles) {
        if(!h || h==self || Known(records,h) || !ENTITY::DOES_ENTITY_EXIST(h))continue;
        Record r=Read("ped",h,centre);
        if(r.distance>radius)continue;
        r.extra+=(r.extra.empty()?"":" ")+std::string(PED::IS_PED_USING_ANY_SCENARIO(h)?"scenario":"no-scenario");
        if(PED::IS_PED_ON_MOUNT(h))r.extra+=" mounted";
        char b[160];
        std::snprintf(b,sizeof b," human=%d type=%d rel=%u mission=%d owner=%d scen=%u",
            PED::IS_PED_HUMAN(h)?1:0,PED::GET_PED_TYPE(h),static_cast<unsigned>(PED::GET_PED_RELATIONSHIP_GROUP_HASH(h)),
            ENTITY::IS_ENTITY_A_MISSION_ENTITY(h)?1:0,static_cast<int>(PLAYER::_GET_PLAYER_OWNER_OF_MOUNT(h)),
            static_cast<unsigned>(TASK::_GET_SCENARIO_POINT_TYPE_PED_IS_USING(h)));
        r.extra+=b;
        std::string flags;
        for(int flag=0;flag<600;++flag)if(PED::GET_PED_CONFIG_FLAG(h,flag,true))flags+=(flags.empty()?"":",")+std::to_string(flag);
        r.extra+=" flags=["+flags+"]";
        if(Add(records,r))++n.peds;
    }
    const int vehicle_count=worldGetAllVehicles(handles.data(),static_cast<int>(handles.size()));
    for(int i=0;i<vehicle_count && i<static_cast<int>(handles.size());++i) {
        const int h=handles[static_cast<std::size_t>(i)];
        if(!h || Known(records,h) || !ENTITY::DOES_ENTITY_EXIST(h))continue;
        Record r=Read("vehicle",h,centre);
        if(r.distance<=radius && Add(records,r))++n.vehicles;
    }
    // How big the pools were, the largest seen, once per file: a count at
    // the buffer's size means the buffer, not the world, decided what was seen.
    // (The last sample of the 22:08 recording read 0 objects; the note used
    // to say only that.)
    static int largest_objects=0,largest_vehicles=0;
    if(records.size()<=1){largest_objects=0;largest_vehicles=0;} // A fresh file.
    largest_objects=std::max(largest_objects,object_count);largest_vehicles=std::max(largest_vehicles,vehicle_count);
    std::erase_if(notes,[](const std::string& s){return s.rfind("# pools:",0)==0;});
    notes.emplace_back("# pools: objects="+std::to_string(largest_objects)+" (last "+std::to_string(object_count)+") vehicles="+std::to_string(largest_vehicles)+" buffer="+std::to_string(handles.size()));
    return n;
}
// Map entities by model, a batch of models a frame: the engine's
// closest-object-of-type query from each query point over every object model
// the game knows (data/scene_survey_hashes.inc) answers with map entities the pools
// never list - a tent's floor, a desk under floating books, a wagon's wheel -
// with their model and pose, as "mapobject" lines. The pools are read first,
// so a script object of the camp's own is never taken for a map entity; one
// instance per model per point. Names go to the hashes offline
// (analysis/reference/object_list.lua). True once the probe is complete.
bool ProbeStep() {
    const std::size_t total=g_job.points.size()*kAllModelHashes.size();
    if(g_job.probe_cursor>=total)return true;
    ++g_job.probe_frames;
    for(int n=0;n<kProbePerFrame && g_job.probe_cursor<total;++n,++g_job.probe_cursor) {
        const Point& point=g_job.points[g_job.probe_cursor/kAllModelHashes.size()];
        const Hash model=static_cast<Hash>(kAllModelHashes[g_job.probe_cursor%kAllModelHashes.size()]);
        const Object o=OBJECT::GET_CLOSEST_OBJECT_OF_TYPE(point.x,point.y,point.z,kProbeRadius,model,false,false,false);
        if(!o || !ENTITY::DOES_ENTITY_EXIST(o) || Known(g_job.records,o))continue;
        Record r=Read("mapobject",o,g_job.player);
        if(r.distance<=g_job.radius && Add(g_job.records,r))++g_job.probed;
    }
    return g_job.probe_cursor>=total;
}
void ReadPools() {
    const PoolCounts n=CollectPools(g_job.records,g_job.notes,g_job.player,g_job.radius,g_job.ped);
    g_job.objects=n.objects;g_job.peds=n.peds;g_job.vehicles=n.vehicles;
}
#include "scene_survey_storage.inl"
}
void Begin(const std::string& directory,float radius,LogFn log,bool pin) {
    if(g_job.running){Log("already running");return;}
    g_job=Job{};g_job.log=log;g_job.directory=runtime_paths::Resolve(directory).Diagnostics().string();g_job.radius=radius;
    if(!files::CanCreateDirectory(g_job.directory)){Log("survey refused: unsafe output directory");return;}
    runtime_paths::EnsureDirectory(g_job.directory);
    if(!SurveyAdmission(g_job.directory,2)){Log("survey storage is full or unsafe; no existing measurements removed");return;}
    g_job.probe_map_entities=pin;
    g_job.ped=PLAYER::PLAYER_PED_ID();
    if(!g_job.ped || !ENTITY::DOES_ENTITY_EXIST(g_job.ped)){Log("no player");return;}
    const Vector3 at=ENTITY::GET_ENTITY_COORDS(g_job.ped,true,false);
    g_job.player={at.x,at.y,at.z};
    g_job.points=QueryPoints(g_job.player,7.0F,kQueryRing);
    if(!pin)g_job.cursor=g_job.points.size()*kModels.size(); // Nothing to pin: the pools and map files are read on the next frame.
    Record me=Read("player",g_job.ped,g_job.player);me.distance=0;g_job.records.push_back(me);
    g_job.running=true;
    Log("started at "+std::to_string(at.x)+","+std::to_string(at.y)+","+std::to_string(at.z)+": "+(pin?"pinning "+std::to_string(kModels.size())+
        " models from "+std::to_string(g_job.points.size())+" points, "+std::to_string(kMaxPending)+" pins in flight; then ":"")+
        (pin?"the pools, then map entities by model ("+std::to_string(kAllModelHashes.size())+" models from "+
             std::to_string(g_job.points.size())+" points, "+std::to_string(kProbePerFrame)+" a frame)":
             "the existing entity pools only (non-intrusive survey)"));
    ProbeIpls();
}
bool Running(){return g_job.running;}
void AimTick();
void Tick() {
    AimTick();
    if(!g_job.running)return;
    ++g_job.frames;
    // Granted pins are read and released; stale ones are given up on.
    for(auto it=g_job.pending.begin();it!=g_job.pending.end();) {
        Pin& pin=*it;
        if(ENTITY::IS_MAP_ENTITY_PINNED(pin.handle)) {
            const Entity e=ENTITY::_GET_PINNED_MAP_ENTITY(pin.handle);
            if(e && ENTITY::DOES_ENTITY_EXIST(e)) {
                ++g_job.pins;++g_job.outcomes[pin.model].granted;
                Record r=Read("pinned",e,g_job.player);
                if(r.distance<=g_job.radius && Add(g_job.records,r))++g_job.pinned;
            }
            ENTITY::_UNPIN_MAP_ENTITY(pin.handle);it=g_job.pending.erase(it);continue;
        }
        if(++pin.waited>kPinPatienceFrames){++g_job.timeouts;++g_job.outcomes[pin.model].timeouts;ENTITY::_UNPIN_MAP_ENTITY(pin.handle);it=g_job.pending.erase(it);continue;}
        ++it;
    }
    // New pins, a batch of attempts a frame, a few in flight at a time.
    const std::size_t total=g_job.points.size()*kModels.size();
    for(int attempts=0;attempts<kAttemptsPerFrame && g_job.cursor<total && g_job.pending.size()<static_cast<std::size_t>(kMaxPending);++g_job.cursor,++attempts) {
        const Point& point=g_job.points[g_job.cursor/kModels.size()];
        const char* name=kModels[g_job.cursor%kModels.size()];
        const Hash model=MISC::GET_HASH_KEY(name);
        if(!model)continue;
        const int handle=static_cast<int>(ENTITY::PIN_CLOSEST_MAP_ENTITY(model,point.x,point.y,point.z,kPinFlags));
        if(!handle)continue;
        ++g_job.outcomes[name].issued;
        g_job.pending.push_back({handle,name,0});
    }
    if(g_job.cursor<total || !g_job.pending.empty())return;
    if(!g_job.pools_read){ReadPools();g_job.pools_read=true;}
    if(g_job.probe_map_entities && !ProbeStep())return;
    WriteSurvey();
    g_job.running=false;
}
// ---- the thing the player is aiming at --------------------------------------
// A ray down the middle of the screen, and everything standing along it -
// map geometry included, which no pool lists and no closest-object query
// returns (a tent's floor, the standing board a shelf is made of). The first
// thing hit is often the wall the third-person camera sits behind, so the ray
// is carried on past each hit in turn and every entity it passes through is
// written down in the order it was met.
namespace {
constexpr int kAimSegments=8;       // How many things deep the ray reports.
constexpr float kAimReach=30.0F;
struct AimJob {
    bool running=false;
    std::string directory;
    LogFn log=nullptr;
    Point eye,from,at;              // Camera, this segment's start, the far end.
    Ped ped=0;
    ScrHandle probe=0;
    int segment=0,waited=0;
    std::vector<Record> found;
};
AimJob g_aim;
void AimLog(const std::string& s){if(g_aim.log)g_aim.log("aim probe: "+s);}
void AimCast() {
    g_aim.probe=SHAPETEST::START_SHAPE_TEST_LOS_PROBE(g_aim.from.x,g_aim.from.y,g_aim.from.z,
        g_aim.at.x,g_aim.at.y,g_aim.at.z,511,g_aim.ped,7);
    g_aim.waited=0;
}
void AimFinish() {
    g_aim.running=false;
    const std::string stamp=Stamp();
    char head[220];std::snprintf(head,sizeof head,"# aimed from %.3f,%.3f,%.3f at %s: %d along the ray",
        static_cast<double>(g_aim.eye.x),static_cast<double>(g_aim.eye.y),static_cast<double>(g_aim.eye.z),
        stamp.c_str(),static_cast<int>(g_aim.found.size()));
    std::string text=std::string(head)+"\n";
    for(const auto& r:g_aim.found)text+=Line(r)+"\n";
    // This optional developer diagnostic is also logged below. Never let its
    // append-only side file grow indefinitely, and preserve existing evidence
    // if the cap has already been reached or the path is not an ordinary file.
    constexpr std::uint64_t kAimLimit=4ull*1024*1024;
    const auto path=g_aim.directory+"\\TheJockeyClub-aim.txt";
    const HANDLE out=CreateFileA(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    bool saved=false;
    if(out!=INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(GetFileInformationByHandle(out,&info) &&
           !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
           info.nNumberOfLinks==1 && GetFileSizeEx(out,&size) && size.QuadPart>=0 &&
           static_cast<std::uint64_t>(size.QuadPart)<kAimLimit && text.size()+1<=kAimLimit-static_cast<std::uint64_t>(size.QuadPart)) {
            text+='\n';DWORD written=0;
            saved=WriteFile(out,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size();
        }
        CloseHandle(out);
    }
    AimLog(!saved?"could not append the capped diagnostic file; results follow in the log":
        g_aim.found.empty()?"the ray met nothing in 30 m":
        "wrote "+std::to_string(g_aim.found.size())+" along the ray to TheJockeyClub-aim.txt");
    for(const auto& r:g_aim.found)AimLog("  "+Line(r));
}
} // namespace
void ProbeAim(const std::string& directory,LogFn log) {
    if(g_aim.running){if(log)log("aim probe: one is already out");return;}
    g_aim=AimJob{};g_aim.directory=runtime_paths::Resolve(directory).Diagnostics().string();runtime_paths::EnsureDirectory(g_aim.directory);g_aim.log=log;
    const Vector3 eye=CAM::GET_GAMEPLAY_CAM_COORD();
    const Vector3 rot=CAM::GET_GAMEPLAY_CAM_ROT(2);
    if(!std::isfinite(eye.x) || !std::isfinite(rot.z)){AimLog("no gameplay camera");return;}
    constexpr float kRadians=0.01745329252F;
    const float pitch=rot.x*kRadians,yaw=rot.z*kRadians,flat=std::cos(pitch);
    g_aim.eye={eye.x,eye.y,eye.z};
    g_aim.from=g_aim.eye;
    g_aim.at={eye.x-std::sin(yaw)*flat*kAimReach,eye.y+std::cos(yaw)*flat*kAimReach,eye.z+std::sin(pitch)*kAimReach};
    g_aim.ped=PLAYER::PLAYER_PED_ID();
    g_aim.running=true;
    AimCast();
    if(!g_aim.probe){g_aim.running=false;AimLog("the ray could not be cast");}
}
void AimTick() {
    if(!g_aim.running)return;
    BOOL hit=false;Vector3 end{},normal{};Entity entity=0;
    const int state=SHAPETEST::GET_SHAPE_TEST_RESULT(g_aim.probe,&hit,&end,&normal,&entity);
    if(state==1 && ++g_aim.waited<120)return;
    if(state==1){AimLog("no answer in 120 frames");AimFinish();return;}
    if(!hit || !std::isfinite(end.x)){AimFinish();return;}
    if(entity && ENTITY::DOES_ENTITY_EXIST(entity) && entity!=g_aim.ped && !Known(g_aim.found,entity)) {
        Record r=Read("aim",entity,g_aim.eye);
        r.extra+=(r.extra.empty()?"":" ")+("kind="+std::to_string(ENTITY::GET_ENTITY_TYPE(entity)));
        char where[80];std::snprintf(where,sizeof where," met=%.3f,%.3f,%.3f",
            static_cast<double>(end.x),static_cast<double>(end.y),static_cast<double>(end.z));
        r.extra+=where;
        g_aim.found.push_back(r);
    }
    // Carry on from just past what was hit, so the wall the camera sits behind
    // does not end the ray, and report what stands behind it as well.
    if(++g_aim.segment>=kAimSegments){AimFinish();return;}
    const float dx=g_aim.at.x-g_aim.eye.x,dy=g_aim.at.y-g_aim.eye.y,dz=g_aim.at.z-g_aim.eye.z;
    const float length=std::hypot(dx,dy,dz);
    if(length<=0.01F){AimFinish();return;}
    constexpr float kStep=0.10F;
    g_aim.from={end.x+dx/length*kStep,end.y+dy/length*kStep,end.z+dz/length*kStep};
    if(std::hypot(g_aim.from.x-g_aim.eye.x,g_aim.from.y-g_aim.eye.y,g_aim.from.z-g_aim.eye.z)>=kAimReach){AimFinish();return;}
    AimCast();
    if(!g_aim.probe)AimFinish();
}
// ---- the walking recording ------------------------------------------------
namespace {
struct RecordingJob {
    bool running=false;
    std::string directory;
    float radius=20;
    LogFn log=nullptr;
    Ped ped=0;
    Point start;
    std::vector<Record> records;
    std::vector<std::string> notes;
    int frames=0,samples=0;
};
RecordingJob g_rec;
void RecLog(const std::string& s){if(g_rec.log)g_rec.log("scene recording: "+s);}
}
void BeginRecording(const std::string& directory,float radius,LogFn log) {
    if(g_rec.running){RecLog("already running");return;}
    g_rec=RecordingJob{};g_rec.log=log;g_rec.directory=runtime_paths::Resolve(directory).Diagnostics().string();g_rec.radius=radius;
    if(!files::CanCreateDirectory(g_rec.directory)){RecLog("survey refused: unsafe output directory");return;}
    runtime_paths::EnsureDirectory(g_rec.directory);
    if(!SurveyAdmission(g_rec.directory)){RecLog("survey storage is full or unsafe; no existing measurements removed");return;}
    g_rec.ped=PLAYER::PLAYER_PED_ID();
    if(!g_rec.ped || !ENTITY::DOES_ENTITY_EXIST(g_rec.ped)){RecLog("no player");return;}
    const Vector3 at=ENTITY::GET_ENTITY_COORDS(g_rec.ped,true,false);
    g_rec.start={at.x,at.y,at.z};
    Record me=Read("player",g_rec.ped,g_rec.start);me.distance=0;g_rec.records.push_back(me);
    std::string active;
    for(const auto& ipl:kCampIpls) {
        const Hash h=static_cast<Hash>(ipl.hash);
        const bool on=STREAMING::IS_IPL_ACTIVE_HASH(h);
        Vector3 centre{};float r=0;
        const bool sphere=STREAMING::_GET_IPL_BOUNDING_SPHERE(h,&centre,&r);
        char b[256];
        std::snprintf(b,sizeof b,"# ipl %d active=%d centre=%.2f,%.2f,%.2f radius=%.1f %s",ipl.hash,on?1:0,
            sphere?centre.x:0.0F,sphere?centre.y:0.0F,sphere?centre.z:0.0F,sphere?r:0.0F,ipl.note);
        g_rec.notes.emplace_back(b);
        if(on)active+=(active.empty()?"":", ")+std::to_string(ipl.hash);
    }
    SweepImaps(g_rec.notes,g_rec.start,g_rec.log);
    g_rec.running=true;
    RecLog("started at "+std::to_string(at.x)+","+std::to_string(at.y)+","+std::to_string(at.z)+", "+std::to_string(static_cast<int>(radius))+
        " m round the player every half second; map files active: "+(active.empty()?std::string("none of the known ones"):active));
}
bool Recording(){return g_rec.running;}
void RecordTick() {
    if(!g_rec.running)return;
    if(++g_rec.frames%30!=1)return; // Twice a second at 60 fps; the first frame at once.
    if(!g_rec.ped || !ENTITY::DOES_ENTITY_EXIST(g_rec.ped)){RecLog("player lost; stopping");EndRecording();return;}
    const Vector3 at=ENTITY::GET_ENTITY_COORDS(g_rec.ped,true,false);
    CollectPools(g_rec.records,g_rec.notes,{at.x,at.y,at.z},g_rec.radius,g_rec.ped);
    ++g_rec.samples;
}
void EndRecording() {
    if(!g_rec.running)return;
    g_rec.running=false;
    // Distances in the file are from where the recording began, so the
    // converter's tent-relative maths has one origin.
    for(auto& r:g_rec.records)r.distance=std::hypot(r.x-g_rec.start.x,r.y-g_rec.start.y,r.z-g_rec.start.z);
    int objects=0,peds=0,vehicles=0;
    for(const auto& r:g_rec.records){if(r.kind=="object")++objects;else if(r.kind=="ped")++peds;else if(r.kind=="vehicle")++vehicles;}
    const std::string stamp=Stamp();
    std::string text=Format(stamp,g_rec.records);
    text+="# recording: "+std::to_string(g_rec.samples)+" samples, "+std::to_string(static_cast<int>(g_rec.radius))+" m round the player, distances from the start\n";
    for(const auto& note:g_rec.notes)text+=note+"\n";
    std::string file_stamp=stamp;for(auto& c:file_stamp)if(c==':' || c==' ')c='-';
    const std::string path=g_rec.directory+"\\TheJockeyClub-record-"+file_stamp+"-"+std::to_string(GetTickCount64())+".txt";
    if(!SurveyAdmission(g_rec.directory,1,text.size())||!files::WriteNew(path,text)){
        RecLog("survey capacity reached or output unavailable; existing measurements preserved");return;}
    RecLog("wrote "+path+": "+std::to_string(objects)+" objects, "+std::to_string(peds)+" peds, "+std::to_string(vehicles)+" vehicles over "+
        std::to_string(g_rec.samples)+" samples");
}
} // namespace horse_races::scene_survey
