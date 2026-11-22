// Synthetic script-cell layouts only. These tests do not read a game process,
// use ScriptHook, or establish that the layout was verified in a live save.
#include "../src/native_horse_race_conditions.h"
#include "../src/native_horse_race_admission.h"
#include "../src/native_horse_race_reasons.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace horse_races::native_encounter;
void Check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct Api {
    bool supported=true;
    std::map<int,std::uint64_t> cells;
    mutable std::vector<int> reads;
    bool Supported() const{return supported;}
    bool ReadGlobal(int index,std::uint64_t& cell) const {
        reads.push_back(index);const auto found=cells.find(index);
        if(found==cells.end())return false;
        cell=found->second;return true;
    }
    void Put(int index,int value){cells[index]=static_cast<std::uint32_t>(value);}
};
constexpr int kActive=1310750+13321,kMissionMap=1835011,kMissionRecords=12106;
constexpr std::uint32_t kAllowed=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)|(1u<<9)|(1u<<10)|(1u<<11);
int DirectoryRow(int region){return 2*region+5;}
int CompletionCell(int region){return kCounts+1+DirectoryRow(region)*2+1;}
int MissionId(int mission){return mission+100;}
void SetMission(Api& api,int mission,int state) {
    api.Put(kMissionMap,256);api.Put(kMissionRecords,771);
    api.Put(kMissionMap+1+mission*74+1,MissionId(mission));
    api.Put(kMissionRecords+1+MissionId(mission)*7+1,state);
}
Api Ready() {
    Api api;
    api.Put(40+1095,0);api.Put(40+1095+1,7);
    for(int slot=0;slot<7;++slot)api.Put(1509+slot*436,3);
    api.Put(kEvents,120);api.Put(kEventState+4,0);api.Put(kEventState+5,0);
    api.Put(kEventState+6,0);api.Put(kEventState+7,1);
    api.Put(1327479+4,0);api.Put(1897952+41,0);
    api.Put(kUsed,29);api.Put(kUsed+1+8,0);
    api.Put(kHistory,180);
    for(int row=0;row<180;++row)api.Put(kHistory+1+row*6,0);
    for(const auto [row,key]:{std::pair{7,27},std::pair{69,147},std::pair{149,267}}) {
        api.Put(kHistory+1+row*6,key);api.Put(kHistory+1+row*6+1,0);api.Put(kHistory+1+row*6+2,0);
    }
    api.Put(1310750,120);api.Put(1572887+12,-1);
    api.Put(kConfig,1073745953);api.Put(kConfig+1,1);
    api.Put(kConfig+38,8192);api.Put(kConfig+39,-1);api.Put(kConfig+48,0);
    for(int offset:{4,5,6,7})api.Put(kConfig+offset,-1);
    api.Put(kConfig+34,static_cast<int>(kAllowed));api.Put(kConfig+64,45);
    for(int region=0;region<45;++region)api.Put(kConfig+64+1+region,100);
    api.Put(kActive,4);for(int slot=0;slot<4;++slot)api.Put(kActive+1+slot*9,-1);
    api.Put(1935630+32,0);api.Put(1935630+33,0);api.Put(1935630+30,0);api.Put(1894899,0);
    api.Put(kStarts,45);api.Put(kEnds,45);api.Put(kDirectory,64);api.Put(kCounts,64);
    for(int row=0;row<64;++row) {
        api.Put(kDirectory+1+row,0);
        api.Put(kCounts+1+row*2,777); // A counter's other field must not be read.
        api.Put(kCounts+1+row*2+1,0);
    }
    for(int region=0;region<45;++region){api.Put(kStarts+1+region,0);api.Put(kEnds+1+region,-1);}
    for(int region=0;region<17;++region) {
        const int row=DirectoryRow(region);
        api.Put(kStarts+1+region,row);api.Put(kEnds+1+region,row);api.Put(kDirectory+1+row,27);
    }
    for(int mission:{26,28,56,60})SetMission(api,mission,0);
    return api;
}
void ReadCellsAndArrayHeaders() {
    Check(kConfig==1313748 && kEventState==17763 && kHistory==18507 && kUsed==17516 &&
        kDirectory==1326873 && kStarts==1327374 && kEnds==1327420 && kCounts==19709,"global base arithmetic changed");
    Api api;Reader<Api> r{api};api.cells[10]=0xabcdef01ffffffffULL;
    Check(r.Int(10)==-1,"VM integers must use the signed low 32 bits");
    api.cells[10]=0xabcdef0100000000ULL;
    Check(r.Int(10)==-1 && api.reads==std::vector<int>{10},"one reader did not memoize its first successful cell sample");
    Check(Reader<Api>{api}.Int(10)==0,"fresh reader did not use low word or inherited another snapshot's cache");
    api.reads.clear();Check(!r.Int(-1) && api.reads.empty(),"negative index reached memory API");
    for(int raw:{0,1,2,-1}) {
        api.Put(10,raw);const auto value=Reader<Api>{api}.Bool(10);
        Check(raw<0 || raw>1?!value.has_value():value==static_cast<bool>(raw),"malformed BOOL was converted to No");
    }
    api.Put(100,3);api.Put(100+1+2*7+1,42);api.Put(100+2*7+1,99);
    api.reads.clear();Check(r.Element(100,2,7,1,3)==42 && api.reads==std::vector<int>{100,116},
        "array header +1 or record stride/offset was omitted");
    for(int index:{-1,3,4})Check(!r.Element(100,index,7,1,3),"out-of-range array element read");
    Check(!r.Element(100,0,0,0,3) && !r.Element(100,0,7,-1,3) && !r.Element(100,0,7,7,3),"invalid stride/offset read");
    for(int count:{-1,0,4}){api.Put(100,count);Check(!Reader<Api>{api}.Element(100,0,7,1,3),"malformed array length admitted");}
    api.cells.erase(100);Check(!Reader<Api>{api}.Length(100,3),"missing array header became an empty known array");
    api.reads.clear();const Reader<Api> missing{api};Check(!missing.Int(500),"missing cell unexpectedly readable");
    api.Put(500,1);Check(!missing.Int(500) && api.reads==std::vector<int>{500},
        "a failed cell read was retried into permission inside the same snapshot");
    Check(Reader<Api>{api}.Int(500)==1,"a fresh snapshot could not recover a formerly missing cell");
}
void PrimaryHorseAndBonding() {
    for(int slot=0;slot<7;++slot)for(int rank=0;rank<=4;++rank) {
        auto api=Ready();api.Put(1135,slot);api.Put(1509+slot*436,rank);
        const auto globals=api.cells;const auto s=Reader<Api>{api}.Capture();
        Check(s.bonding==Truth{rank>=3} && api.cells==globals,"primary slot or raw bonding level used displayed +1");
        Check(std::count(api.reads.begin(),api.reads.end(),1509+slot*436)==1,"wrong primary horse record read");
        for(int other=0;other<7;++other)if(other!=slot)
            Check(std::find(api.reads.begin(),api.reads.end(),1509+other*436)==api.reads.end(),"read a non-primary horse rank");
    }
    for(int slot:{-1,7,8}){auto api=Ready();api.Put(1135,slot);Check(!Reader<Api>{api}.Capture().bonding,"invalid primary slot became No/Yes");}
    for(int rank:{-1,5,100}){auto api=Ready();api.Put(1509,rank);Check(!Reader<Api>{api}.Capture().bonding,"malformed bonding rank became No/Yes");}
    for(int missing:{1135,1136,1509}){auto api=Ready();api.cells.erase(missing);Check(!Reader<Api>{api}.Capture().bonding,"primary horse read hole became No");}
    auto api=Ready();api.Put(1136,6);Check(!Reader<Api>{api}.Capture().bonding,"primary array must have seven entries");
}
void PlayerPredicatesAndCooldowns() {
    for(int hour=0;hour<24;++hour)Check(Daytime(hour)==Truth{hour>=6&&hour<20},"daytime is exactly 06:00 inclusive to 20:00 exclusive");
    for(int hour:{-1,24,99})Check(!Daytime(hour).has_value(),"unknown hour became night/No");
    // The caller supplies raw handling(attribute 4) and speed(attribute 5).
    // Native attribute-ID selection belongs to the runtime integration test.
    for(int handling=0;handling<=9;++handling)for(int speed=0;speed<=9;++speed)
        Check(Attributes(handling,speed)==Truth{handling>=2||speed>=2},"handling OR speed threshold changed or used displayed +1");
    Check(Attributes(1,1)==false && Attributes(2,0)==true && Attributes(0,2)==true,"raw threshold is two, not one or both attributes");
    for(int invalid:{-1,10,100})Check(!Attributes(invalid,2).has_value() && !Attributes(2,invalid).has_value(),"unknown attribute became a known result");
    for(int raw=-89;raw<=0;++raw)Check(CooldownSteps(raw)==0,"native signed negative cooldown tail was rejected");
    for(const auto [raw,steps]:{std::pair{1,1},std::pair{89,1},std::pair{90,1},std::pair{91,2},
        std::pair{179,2},std::pair{180,2},std::pair{181,3},std::pair{9000,100},std::pair{2147483647,23860930}})
        Check(CooldownSteps(raw)==steps,"race cooldown must ceil(raw/90) without overflow, not report seconds");
    for(int raw:{-90,-91,std::numeric_limits<int>::min()})Check(!CooldownSteps(raw),"invalid negative cooldown became zero");
    for(int raw:{-89,-1,0,1,90,91}) {
        auto api=Ready();api.cells[kEventState+4]=0xdecafbad00000000ULL|static_cast<std::uint32_t>(raw);
        Check(Reader<Api>{api}.Capture().cooldown_steps==CooldownSteps(raw),"snapshot did not decode signed cooldown cell");
    }
    Check(SharedSeconds(0,0)==0 && SharedSeconds(999,1000)==0 && SharedSeconds(1001,1000)==1 &&
        SharedSeconds(2000,1000)==1 && SharedSeconds(2001,1000)==2 && SharedSeconds(2147483647,0)==2147484,
        "shared cooldown must ceil positive milliseconds into seconds separately");
    Check(!SharedSeconds(-1,0) && !SharedSeconds(0,-1),"timer wrap/rebase was guessed to be expired");
    auto api=Ready();api.Put(1327483,-1);Check(!Reader<Api>{api}.Capture().shared_deadline,"negative shared deadline was exposed as expired");
}
void HistoryStopsAllVariants() {
    for(const auto [row,key]:{std::pair{7,27},std::pair{69,147},std::pair{149,267}}) {
        const int base=kHistory+1+row*6;
        for(const auto [outcomes,attempts]:{std::pair{3,0},std::pair{4,0},std::pair{0,3},std::pair{2,3}}) {
            auto api=Ready();api.Put(base+1,outcomes);api.Put(base+2,attempts);
            const auto s=Reader<Api>{api}.Capture();Check(s.history_ok==false,"one rider's terminal history did not stop the entire native event");
            for(int variant=0;variant<11;++variant)Check(!Candidate(s,variant,0,true),"history stopped only one rider instead of all event variants");
        }
        for(const auto [outcomes,attempts]:{std::pair{0,0},std::pair{2,2},std::pair{0,4}}) {
            auto api=Ready();api.Put(base+1,outcomes);api.Put(base+2,attempts);
            Check(Reader<Api>{api}.History()==true,"history f1>=3 or f2==3 semantics changed");
        }
        for(int field:{0,1,2}) {
            auto api=Ready();api.cells.erase(base+field);Check(!Reader<Api>{api}.History().has_value(),"history hole became a No/Yes result");
        }
        for(int field:{1,2}){auto api=Ready();api.Put(base+field,-1);Check(!Reader<Api>{api}.History().has_value(),"negative history counter accepted");}
        auto api=Ready();api.Put(kHistory+1+170*6,key);api.Put(kHistory+1+170*6+1,0);api.Put(kHistory+1+170*6+2,0);
        Check(!Reader<Api>{api}.History().has_value(),"duplicate native history key accepted");
    }
    auto api=Ready();for(int row=0;row<180;++row)api.Put(kHistory+1+row*6,0);
    Check(Reader<Api>{api}.History()==true,"missing recorded key must follow the native zero-history lookup");
    api.cells.erase(kHistory+1+179*6);Check(!Reader<Api>{api}.History().has_value(),"unreadable directory tail became missing-key zero");
}
void DirectoryAndGroupQuotas() {
    auto api=Ready();api.Put(CompletionCell(9),7);api.Put(kCounts+1+9*2+1,42);
    api.reads.clear();Check(Reader<Api>{api}.Completions(9)==7,"region ID was substituted for its directory row");
    Check(std::find(api.reads.begin(),api.reads.end(),CompletionCell(9))!=api.reads.end() &&
        std::find(api.reads.begin(),api.reads.end(),kCounts+1+9*2+1)==api.reads.end(),"completion count used wrong row or field");
    api.Put(kStarts+1+9,20);api.Put(kEnds+1+9,23);
    for(int row=20;row<23;++row)api.Put(kDirectory+1+row,1);
    Check(Reader<Api>{api}.Completions(9)==7,"directory scan ignored non-race entries before event 27");
    api.Put(kDirectory+1+23,1);Check(Reader<Api>{api}.Completions(9)==0,"known race-free directory did not yield zero");
    api.cells.erase(kDirectory+1+22);Check(!Reader<Api>{api}.Completions(9),"directory hole became zero completions");
    struct Group{int region,cap;std::vector<int> members;};
    for(const Group group:std::vector<Group>{{0,0,{0,2,11}},{11,0,{0,2,11}},{1,1,{1,7,6,12}},
        {6,1,{1,7,6,12}},{3,3,{3,10}},{10,3,{3,10}},{9,4,{4,9}}}) {
        api=Ready();int total=0;
        for(int member:group.members){api.Put(CompletionCell(member),2);total+=2;}
        api.Put(kConfig+64+1+group.cap,total+1);
        Check(Reader<Api>{api}.Quota(group.region)==true,"runtime group quota incorrectly blocked remaining allowance");
        api.Put(kConfig+64+1+group.cap,total);
        Check(Reader<Api>{api}.Quota(group.region)==false,"group total equal to cap remained eligible");
        api.Put(kConfig+64+1+group.cap,total-1);
        Check(Reader<Api>{api}.Quota(group.region)==false,"group over quota remained eligible");
        api.Put(kConfig+64+1+group.cap,total+1);api.cells.erase(CompletionCell(group.members.back()));
        Check(!Reader<Api>{api}.Quota(group.region).has_value(),"one unknown group member was assumed zero");
    }
    for(int region:{-1,2,4,7,12,17})Check(!Reader<Api>{Ready()}.Quota(region).has_value(),"unconfigured quota group guessed");
    for(int scenario=0;scenario<8;++scenario) {
        api=Ready();const int row=DirectoryRow(0);
        if(scenario==0)api.Put(kStarts+1,-1);
        if(scenario==1)api.Put(kEnds+1,row-2);
        if(scenario==2)api.Put(kEnds+1,64);
        if(scenario==3)api.Put(kDirectory,501);
        if(scenario==4)api.Put(kCounts,row);
        if(scenario==5)api.Put(kDirectory+1+row,120);
        if(scenario==6)api.Put(CompletionCell(0),-1);
        if(scenario==7)api.cells.erase(kStarts);
        Check(!Reader<Api>{api}.Completions(0),"malformed dynamic directory/count layout accepted");
    }
}
void MissionMappingAndStoryWindows() {
    auto api=Ready();constexpr int mission=56,id=333;
    api.Put(kMissionMap+1+mission*74+1,id);api.Put(kMissionRecords+1+mission*7+1,5);
    for(int state=0;state<=9;++state) {
        api.Put(kMissionRecords+1+id*7+1,state);api.reads.clear();
        Check(Reader<Api>{api}.Mission(mission)==Truth{state==5||state==6},"mission completion is only record state 5 or 6");
        Check(api.reads==std::vector<int>{kMissionMap,kMissionMap+1+mission*74+1,kMissionRecords,kMissionRecords+1+id*7+1},
            "mission ID mapping, +1 header or stride-seven state field was bypassed");
    }
    for(int state:{-1,10}){api.Put(kMissionRecords+1+id*7+1,state);Check(!Reader<Api>{api}.Mission(mission),"invalid mission state became No");}
    for(int bad:{-1,0,771}){api.Put(kMissionMap+1+mission*74+1,bad);Check(!Reader<Api>{api}.Mission(mission),"invalid mapped mission record accepted");}
    for(int region:{3,6,11})for(bool before:{false,true})for(bool after:{false,true}) {
        api=Ready();SetMission(api,region==11?26:56,before?5:0);SetMission(api,region==11?28:60,after?6:0);
        Check(Reader<Api>{api}.Region(region,kAllowed,false)==Truth{!before||after},"regional story exclusion window changed");
        Check(Reader<Api>{api}.Region(region,kAllowed,true)==Truth{!before||after},"quota bypass also bypassed story gating");
    }
    for(int region:{3,6,11}) {
        api=Ready();api.cells.erase(kMissionRecords+1+MissionId(region==11?28:60)*7+1);
        Check(!Reader<Api>{api}.Region(region,kAllowed,false),"unknown regional story state became known No/Yes");
    }
    api=Ready();Check(Reader<Api>{api}.Region(0,0,false)==false,"known disallowed region was not denied");
    Check(!Reader<Api>{api}.Region(2,1u<<2,false),"unknown configured region was treated as supported");
    api.cells.erase(kConfig+64);Check(Reader<Api>{api}.Region(0,kAllowed,true)==true &&
        !Reader<Api>{api}.Region(0,kAllowed,false),"explicit quota bypass or ordinary quota lookup changed");
}
void ActiveSchedulerSlots() {
    auto api=Ready();Check(Reader<Api>{api}.ActiveSlots()==true,"empty active slots blocked event");
    api.Put(kActive+1+3*9,5);api.Put(1310750+1+5*111,32);
    Check(Reader<Api>{api}.ActiveSlots()==true,"one category-32 event exhausts two-event capacity");
    api.Put(kActive+1,6);api.Put(1310750+1+6*111,32);
    Check(Reader<Api>{api}.ActiveSlots()==false,"two category-32 events did not block scheduler");
    api.Put(1310750+1+6*111,0);Check(Reader<Api>{api}.ActiveSlots()==true,"unrelated event counted as category 32");
    api.cells[1310750+1+6*111]=0x80000000ULL;Check(Reader<Api>{api}.ActiveSlots()==false,"exclusive active event did not block scheduler");
    for(int bad:{-2,120}){api=Ready();api.Put(kActive+1+2*9,bad);Check(!Reader<Api>{api}.ActiveSlots(),"malformed active event slot became No/Yes");}
    api=Ready();api.cells.erase(kActive+1+3*9);Check(!Reader<Api>{api}.ActiveSlots(),"active slot read hole became empty slot");
    api=Ready();api.Put(kActive,3);Check(!Reader<Api>{api}.ActiveSlots(),"wrong active-slot header was accepted");
}
void SnapshotAndCandidates() {
    auto api=Ready();const auto globals=api.cells;const auto ready=Reader<Api>{api}.Capture();
    Check(Yes(ready.supported)&&Yes(ready.history_ok)&&Yes(ready.common_ok)&&Yes(ready.bonding)&&
        ready.cooldown_steps==0 && ready.shared_deadline==0 && ready.player_region==0 && ready.used==0u,
        "complete synthetic snapshot did not retain independently verified values");
    Check(api.cells==globals,"read-only snapshot modified synthetic globals");
    for(int variant=0;variant<11;++variant)Check(Candidate(ready,variant,0,true),"unused potential point was not a candidate");
    for(int consumed=0;consumed<11;++consumed) {
        api=Ready();api.Put(kUsed+1+8,1u<<(11+consumed));const auto s=Reader<Api>{api}.Capture();
        for(int variant=0;variant<11;++variant)Check(Candidate(s,variant,0,true)==(variant!=consumed),
            "persisted bits 259..269 must independently use word 8 bits 11..21 (31-bit words)");
    }
    for(unsigned unrelated:{1u<<2,1u<<3,1u<<4,1u<<5,1u<<6,1u<<7,1u<<8,1u<<9,1u<<10,1u<<22,1u<<31}) {
        api=Ready();api.cells[kUsed+1+8]=unrelated;const auto s=Reader<Api>{api}.Capture();
        for(int variant=0;variant<11;++variant)Check(Candidate(s,variant,0,true),"unrelated persisted bit hid a potential point");
    }
    for(int variant:{-1,11,32})Check(!Candidate(ready,variant,0,true),"invalid variant shifted outside its used-bit range");
    for(int region:{-1,17,32})Check(!Candidate(ready,0,region,true),"invalid candidate region admitted");
    Check(!Candidate(ready,0,0,false),"caller current-player failure was ignored");
    for(int field=0;field<7;++field)for(bool known_no:{false,true}) {
        auto s=ready;
        const Truth unknown_or_no=known_no?Truth{false}:std::nullopt;
        if(field==0)s.supported=unknown_or_no;
        if(field==1)s.history_ok=unknown_or_no;
        if(field==2)s.common_ok=unknown_or_no;
        if(field==3)s.regions[0]=unknown_or_no;
        if(field==4)s.used.reset();
        if(field==5)s.player_region.reset();
        if(field==6){s.player_region=1;s.regions[1]=unknown_or_no;}
        Check(!Candidate(s,0,0,true),"candidate treated an unknown/adverse required field as permission");
    }
    auto s=ready;s.regions[11].reset();Check(!Candidate(s,0,11,true),"unknown destination region admitted");
    for(int bad:{-1,17}){s=ready;s.player_region=bad;Check(!Candidate(s,0,0,true),"invalid current player region admitted");}
    api=Ready();api.supported=false;api.reads.clear();s=Reader<Api>{api}.Capture();
    Check(!s.supported && !s.history_ok && !s.common_ok && !s.bonding && !s.used && api.reads.empty() &&
        !Candidate(s,0,0,true),"unsupported build read cells or turned Unknown into No/permission");
    Api empty;s=Reader<Api>{empty}.Capture();Check(!s.history_ok && !s.common_ok && !s.bonding && !s.cooldown_steps &&
        !s.shared_deadline && !s.used && !Candidate(s,0,0,true),"missing globals produced a false result or candidate");
}
void UnknownConfigurationAndHoles() {
    for(const int index:{1310750,kEvents,kUsed,kHistory}) {
        auto api=Ready();api.Put(index,1);const auto s=Reader<Api>{api}.Capture();
        Check(!Candidate(s,0,0,true),"malformed required array header created a candidate");
    }
    for(int missing:{kDirectory,kCounts,kStarts,kEnds,kConfig+64,CompletionCell(11),kUsed+1+8,1897952+41}) {
        auto api=Ready();api.cells.erase(missing);const auto s=Reader<Api>{api}.Capture();
        Check(!Candidate(s,0,0,true),"unknown directory/group/used/current-region data created a potential-point marker");
        if(missing==kUsed+1+8)Check(!s.used,"missing consumed word was silently treated as all unused");
        else if(missing==1897952+41)Check(!s.player_region,"missing current region was silently guessed");
        else Check(!s.regions[0].has_value(),"unknown group quota became a false No or permission");
    }
    for(const auto [index,value]:{std::pair{kConfig,0},std::pair{kConfig+1,0},std::pair{kConfig+1,3},
        std::pair{kConfig+38,1},std::pair{kConfig+39,0},std::pair{kConfig+6,0},std::pair{kConfig+7,0},
        std::pair{kEventState+7,2},std::pair{kEventState+6,-1},std::pair{kConfig+48,2},
        std::pair{kEventState+5,8},std::pair{1935630+30,2}}) {
        auto api=Ready();api.Put(index,value);const auto s=Reader<Api>{api}.Capture();
        Check(!s.common_ok.has_value() && !Candidate(s,0,0,true),"unknown config or malformed BOOL became No/permission");
    }
    for(int index:{1572887+12,kConfig,kConfig+1,kConfig+38,kConfig+39,kConfig+4,kConfig+5,kConfig+6,kConfig+7,
        kEventState+7,kEventState+6,kEventState+5,kConfig+48,kConfig+34,1935630+32,1935630+33,1935630+30}) {
        auto api=Ready();api.cells.erase(index);const auto s=Reader<Api>{api}.Capture();
        Check(!s.common_ok.has_value() && !Candidate(s,0,0,true),"required global hole became No/permission");
    }
    for(const auto [index,value]:{std::pair{1572887+12,0},std::pair{kEventState+7,0},std::pair{kEventState+6,1},
        std::pair{kConfig+48,1},std::pair{kEventState+5,7}}) {
        auto api=Ready();api.Put(index,value);const auto s=Reader<Api>{api}.Capture();
        Check(s.common_ok==false && !Candidate(s,0,0,true),"known native event blocker was ignored");
    }
    auto api=Ready();api.Put(kConfig+4,56);SetMission(api,56,0);
    Check(Reader<Api>{api}.Capture().common_ok==false,"incomplete prerequisite mission ignored");
    SetMission(api,56,5);Check(Reader<Api>{api}.Capture().common_ok==true,"completed prerequisite did not satisfy common gate");
    api.Put(kConfig+5,60);SetMission(api,60,6);Check(Reader<Api>{api}.Capture().common_ok==false,"completed stop mission ignored");
    api=Ready();api.Put(kEventState+5,1);api.cells.erase(1935630+32);api.cells.erase(1935630+33);api.cells.erase(1935630+30);
    const auto s=Reader<Api>{api}.Capture();Check(s.common_ok==true && s.ignore_human_cargo,"native state-one human-cargo exemption lost");
}
constexpr int kNow=100000;
Api AdmissionReady() {
    auto api=Ready();
    api.Put(1310750+16075,0);api.Put(1327479,-1);api.Put(1326862+4,0);api.Put(1310750+16111,1000);
    api.Put(1310750+16077,0);api.Put(1310750+16035,0);api.Put(1894899+2,-1);api.Put(1894899,0);
    api.Put(1310750+16074,0);api.Put(40+431,18);
    for(int region=0;region<18;++region)api.Put(40+431+1+region,0);
    api.Put(23118,151);api.Put(1888801,151);
    for(int place=0;place<151;++place){api.Put(23118+1+place*11,0);api.Put(1888801+1+place*35+20,0);}
    api.Put(1392194,4);for(int slot=0;slot<4;++slot)api.Put(1392194+1+slot*10+5,100+slot);
    for(int offset:{18,13,19,17,20,21,44})api.Put(1935630+offset,0);
    api.Put(1934266+67,0);api.Put(1430242,0);api.Put(1430242+2,0);api.Put(1310750+16037,0);
    api.Put(16,0);api.Put(18,0);api.Put(1935436,0);api.Put(1935630,0);api.Put(1430231+4,0);
    api.Put(40+450,643);api.Put(40+1094,0);
    for(int i=0;i<643;++i)api.Put(40+450+1+i,0);
    return api;
}
Admission AdmissionOf(const Api& api,int now=kNow){return ReadAdmission(Reader<Api>{api},now);}
void AdmissionBaselineAndUnknowns() {
    auto api=AdmissionReady();const auto globals=api.cells;
    const Reader<Api> read{api};const auto snapshot=read.Capture();const auto allowed=ReadAdmission(read,kNow);
    Check(allowed.allowed==true && !allowed.restricted_volume && Candidate(snapshot,0,0,Yes(allowed.allowed)) &&
        api.cells==globals,"complete synthetic global admission was denied or wrote native state");
    auto reads=api.reads;std::sort(reads.begin(),reads.end());
    Check(std::adjacent_find(reads.begin(),reads.end())==reads.end(),"snapshot/admission did not share successful cell samples");
    for(int missing:{1310750+16075,1327479,1326862+4,1310750+16111,1310750+16077,1310750+16035,
        1894899+2,1894899,1897952+41,1310750+16074,kActive,kActive+1,kActive+1+9,kActive+1+18,kActive+1+27,40+431,40+431+1,
        1935630+18,1935630+13,1935630+19,1935630+17,1935630+20,1935630+21,1934266+67,
        1430242,1430242+2,1310750+16037,1935630+44,16,18,1935436,1935630,1430231+4}) {
        api=AdmissionReady();api.cells.erase(missing);const auto result=AdmissionOf(api);
        Check(!result.allowed.has_value() && !Candidate(snapshot,0,0,Yes(result.allowed)),
            "global admission read hole became No or allowed a candidate");
    }
    for(int malformed:{1310750+16075,1326862+4,1935630+18,1935630+13,16,18,1430231+4}) {
        api=AdmissionReady();api.Put(malformed,2);Check(!AdmissionOf(api).allowed.has_value(),"malformed admission BOOL became No/Yes");
    }
    for(const auto [index,value]:{std::pair{1894899+2,-2},std::pair{1894899+2,151},
        std::pair{1897952+41,-1},std::pair{1897952+41,17},std::pair{40+431,19},std::pair{40+431,0}}) {
        api=AdmissionReady();api.Put(index,value);Check(!AdmissionOf(api).allowed.has_value(),"invalid region/place layout admitted");
    }
    api=AdmissionReady();api.Put(1310750+16075,1);
    Check(!AdmissionOf(api).allowed.has_value(),"forced native dispatch was guessed to be ordinary No/Yes");
    api=AdmissionReady();api.supported=false;Check(!AdmissionOf(api).allowed && api.reads.empty(),"unsupported admission touched cells");
    api=AdmissionReady();Check(!AdmissionOf(api,-1).allowed && api.reads.empty(),"negative runtime clock read admission cells");
}
void AdmissionStartAndKnownBlockers() {
    for(int started:{0,-1,kNow-4999,std::numeric_limits<int>::max()}) {
        auto api=AdmissionReady();api.Put(1310750+16111,started);
        Check(AdmissionOf(api).allowed==false,"native scheduler startup window or overflow guard changed");
    }
    auto api=AdmissionReady();api.Put(1310750+16111,kNow-5000);
    Check(AdmissionOf(api).allowed==true,"five-second startup boundary should be inclusive");
    api.Put(1310750+16111,0);api.Put(1326862+4,1);Check(AdmissionOf(api).allowed==true,"explicit native startup bypass lost");
    for(int queued:{0,26,27,119})for(bool bypass:{false,true}) {
        api=AdmissionReady();api.Put(1327479,queued);api.Put(1326862+4,bypass?1:0);
        Check(AdmissionOf(api).allowed==false,"an event already dispatching (including 27) admitted a fresh potential point");
    }
    for(int queued:{-2,120,999}) {
        api=AdmissionReady();api.Put(1327479,queued);
        Check(!AdmissionOf(api).allowed.has_value(),"invalid dispatch queue value became No or permission");
    }
    for(const auto [index,value]:{std::pair{1310750+16077,1},std::pair{1310750+16035,1},std::pair{1894899,2},
        std::pair{40+431+1,1},std::pair{1935630+20,1},std::pair{1934266+67,kNow-29999},
        std::pair{16,1},std::pair{18,1},std::pair{1935436,2},std::pair{1935630,1<<14},std::pair{1430231+4,1}}) {
        api=AdmissionReady();api.Put(index,value);const auto result=AdmissionOf(api);
        Check(result.allowed==false,"known global admission blocker was ignored");
        Check(!Candidate(Reader<Api>{api}.Capture(),0,0,Yes(result.allowed)),"blocked admission leaked a native candidate");
    }
    api=AdmissionReady();api.Put(40+431+1,9);Check(AdmissionOf(api).allowed==true,"region bit 8 did not override region bit 1");
    api=AdmissionReady();api.Put(1897952+41,16);api.Put(40+431+1+16,1);
    Check(AdmissionOf(api).allowed==false,"last accessed region did not use header +1");
    api.Put(40+431+1+16,0);api.Put(40+431,16);Check(!AdmissionOf(api).allowed,"short region array admitted region 16");
    for(int conflict:{0,kNow-30001,kNow-30000}) {
        api=AdmissionReady();api.Put(1934266+67,conflict);Check(AdmissionOf(api).allowed==true,"old/exact-boundary conflict wrongly blocks admission");
    }
    for(int counter:{19,17}) {
        api=AdmissionReady();api.Put(1935630+counter,1);Check(AdmissionOf(api).allowed==true,"law counter without its flag blocked");
        api.Put(1935630+18,1);Check(AdmissionOf(api).allowed==false,"active law state with pursuit counter did not block");
    }
    api=AdmissionReady();api.Put(1935630+21,1);Check(AdmissionOf(api).allowed==true,"law21 without law13 blocked");
    api.Put(1935630+13,1);Check(AdmissionOf(api).allowed==false,"law13/law21 conjunction ignored");
    api=AdmissionReady();api.cells[1935630+44]=0xF6687C5AULL;
    Check(AdmissionOf(api).allowed==false,"binocular weapon hash did not block event admission");
}
void AdmissionSlotCapacity() {
    struct Slots{unsigned occupied,weighted;};
    for(const Slots slots:std::array<Slots,7>{{{0,0},{14,0},{7,0},{3,0},{3,1},{3,3},{1,1}}})
        for(bool populated:{false,true}) {
            auto api=AdmissionReady();api.Put(1894899+2,populated?40:-1);api.Put(1894899,populated?1:0);
            int occupied=0;bool free_slot=false;
            for(int slot=0;slot<4;++slot) {
                if(!(slots.occupied&(1u<<slot))){if(slot>0)free_slot=true;continue;}
                const int event=10+slot;const bool doubled=(slots.weighted&(1u<<slot))!=0;
                api.Put(kActive+1+slot*9,event);
                api.Put(1310750+1+event*111,1<<14); // Decoy main flags: only f_1 changes slot cost.
                api.Put(1310750+1+event*111+1,doubled?1<<14:0);occupied+=doubled?2:1;
            }
            const bool expected=free_slot && occupied<(populated?3:4);
            Check(AdmissionOf(api).allowed==Truth{expected},
                "four-slot capacity, reserved slot zero, secondary bit14 cost or place-specific limit changed");
        }
    for(int event:{0,119}) {
        auto api=AdmissionReady();api.Put(kActive+1+3*9,event);api.Put(1310750+1+event*111+1,0);
        Check(AdmissionOf(api).allowed==true,"valid extreme event ID was excluded from slot capacity scan");
        api.cells.erase(1310750+1+event*111+1);
        Check(!AdmissionOf(api).allowed.has_value(),"unknown active event slot cost was assumed one");
    }
    for(int event:{-2,120}) {
        auto api=AdmissionReady();api.Put(kActive+1+2*9,event);
        Check(!AdmissionOf(api).allowed.has_value(),"malformed capacity slot event became empty/No");
    }
    auto api=AdmissionReady();api.Put(kActive,3);
    Check(!AdmissionOf(api).allowed.has_value(),"capacity scan accepted a wrong active-slot header");
}
void AdmissionPlacesVolumesAndDensity() {
    constexpr std::array<int,25> places{40,78,37,38,93,82,95,65,68,69,3,61,110,5,35,22,105,8,32,26,115,76,92,56,120};
    for(std::size_t i=0;i<places.size();++i) {
        Check(PlaceMask(places[i])==(1u<<i),"named-place mask changed");
        auto api=AdmissionReady();api.Put(1894899+2,places[i]);api.Put(1310750+16074,1u<<i);
        Check(AdmissionOf(api).allowed==false,"blocked named place was admitted");
    }
    Check(PlaceMask(-1)==0 && PlaceMask(150)==0,"unknown place guessed a bit in the blocked-place mask");
    auto api=AdmissionReady();api.Put(1894899+2,40);api.Put(23118+1+40*11,0x2000000);
    Check(AdmissionOf(api).allowed==false,"prohibited place flag used wrong stride or ignored bit");
    for(const auto [place,slot]:{std::pair{11,0},std::pair{76,1},std::pair{15,3},std::pair{128,2}}) {
        api=AdmissionReady();api.Put(1894899+2,place);api.Put(1888801+1+place*35+20,8);
        const auto result=AdmissionOf(api);
        Check(result.allowed==true && result.restricted_volume==100+slot,
            "type-eight restricted volume used wrong place-to-slot mapping or stride");
        api.cells.erase(1392194+1+slot*10+5);Check(!AdmissionOf(api).allowed.has_value(),"unknown restricted volume became permission");
    }
    for(int missing:{23118,23118+1+40*11,1888801,1888801+1+40*35+20}) {
        api=AdmissionReady();api.Put(1894899+2,40);api.cells.erase(missing);
        Check(!AdmissionOf(api).allowed.has_value(),"place table read hole became No/Yes");
    }
    for(int count:{1,2})for(int density:{1,2})for(bool flagged:{false,true}) {
        api=AdmissionReady();api.Put(1894899+2,40);api.Put(1894899,flagged?1:0);
        api.Put(1430242,1);api.Put(1430242+2,count-1);api.Put(1310750+16037,density);
        Check(AdmissionOf(api).allowed==Truth{!(count>1&&density>=2&&flagged)},"native density conjunction changed");
    }
    api=AdmissionReady();api.Put(1894899,1);api.Put(1430242,2);api.Put(1310750+16037,2);
    Check(AdmissionOf(api).allowed==true,"unknown-place sentinel incorrectly applied populated-place density cap");
}
void AdmissionSpecialPlaceList() {
    auto api=AdmissionReady();api.Put(1894899+2,5);
    Check(AdmissionOf(api).allowed==true,"empty special-place list blocked admission");
    api.Put(40+1094,1);api.Put(40+450+1,58);
    Check(AdmissionOf(api).allowed==false,"special-place entry 58 did not block admission");
    api.Put(40+450+1,57);Check(AdmissionOf(api).allowed==true,"unrelated special-place entry blocked admission");
    api.Put(40+1094,643);api.Put(40+450+1+642,58);
    Check(AdmissionOf(api).allowed==false,"last special-place list entry was skipped");
    for(int used:{-1,644}){api.Put(40+1094,used);Check(!AdmissionOf(api).allowed,"malformed special-place used length accepted");}
    for(int missing:{40+450,40+1094,40+450+1}) {
        api=AdmissionReady();api.Put(1894899+2,5);api.Put(40+1094,1);api.cells.erase(missing);
        Check(!AdmissionOf(api).allowed.has_value(),"special-place list hole became permission");
    }
}


void MultipleDiagnosticReasons() {
    auto api=AdmissionReady();const auto globals=api.cells;
    auto report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.mask==0 && !report.unknown && api.cells==globals,"ready reason report blocked, unknown or mutated state");
    // All distinct known failures must survive an earlier known or unknown group.
    api.Put(kHistory+1+7*6+1,3);
    api.Put(kEventState+6,1);
    api.Put(kConfig+4,56);SetMission(api,56,0);
    api.Put(1327479,10);api.Put(1310750+16035,1);
    api.Put(1894899,2);api.Put(1935630+20,1);api.Put(16,1);
    report=ReadReasons(Reader<Api>{api},kNow);
    for(auto reason:{BlockReason::RaceHistoryBlocked,BlockReason::RaceUnavailable,BlockReason::StoryProgressBlocked,
        BlockReason::NativeDispatchBusy,BlockReason::NativeSystemRestricted,BlockReason::LocationRestricted,
        BlockReason::LawConflictRestricted,BlockReason::PlayerStateRestricted})
        Check(report.Has(reason),"multi-reason report stopped after its first failure");
    Check(!report.unknown,"complete known simultaneous failures became unknown");
    api.cells.erase(kHistory);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.unknown && report.Has(BlockReason::LawConflictRestricted) && report.Has(BlockReason::PlayerStateRestricted),
        "unknown earlier field hid independently known later blockers");
    // Unknown must remain distinguishable from normal, including forced/unrecognized configuration.
    for(int missing:{kEvents,1310750,kUsed,1310750+16035,1935630+20,16}) {
        api=AdmissionReady();api.cells.erase(missing);report=ReadReasons(Reader<Api>{api},kNow);
        Check(report.unknown && report.mask==0,"missing field masqueraded as a normal or named blocked condition");
    }
    api=AdmissionReady();api.Put(kConfig+39,123);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.unknown && report.mask==0,"unsupported configuration invented a known reason");
    api=AdmissionReady();api.Put(1310750+16075,1);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.unknown && report.mask==0,"forced dispatch became known normal/blocked");
    api=AdmissionReady();api.supported=false;report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.unknown && report.mask==0 && api.reads.empty(),"unsupported diagnostics touched native memory");
    // Quota and region are separate, not the first failure from Region().
    api=AdmissionReady();api.Put(kConfig+64+1,0);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.Has(BlockReason::RegionQuotaReached) && !report.Has(BlockReason::RegionNotAllowed),"quota mislabeled as region");
    api=AdmissionReady();api.Put(kConfig+34,0);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.Has(BlockReason::RegionNotAllowed) && !report.Has(BlockReason::RegionQuotaReached),"region mislabeled as quota");
    api=AdmissionReady();api.Put(kActive+1+9,10);api.Put(1310750+1+10*111,static_cast<int>(1u<<31));
    api.Put(1310750+1+10*111+1,0);report=ReadReasons(Reader<Api>{api},kNow);
    Check(report.Has(BlockReason::OtherEncounterBlocking),"active blocking encounter absent from diagnostics");
    api=AdmissionReady();api.Put(1894899+2,40);api.Put(1894899,1);api.Put(1430242,2);api.Put(1310750+16037,2);
    report=ReadReasons(Reader<Api>{api},kNow);Check(report.Has(BlockReason::EncounterCapacityFull),"density reason missing");
    // Existing separate UI conditions must not leak into the other-restrictions reasons.
    api=AdmissionReady();api.Put(kEventState+4,900);api.Put(1327479+4,kNow+10000);api.Put(1509,0);
    report=ReadReasons(Reader<Api>{api},kNow);Check(report.mask==0 && !report.unknown,"separate cooldown/bonding condition duplicated");
    report.Add(BlockReason::HumanCargoRestricted,true);report.Add(BlockReason::PlayerStateRestricted,std::nullopt);
    Check(report.Has(BlockReason::HumanCargoRestricted) && report.unknown,"runtime extension lost known or unknown status");
    report.Add(BlockReason::HumanCargoRestricted,true);
    Check(report.mask==ReasonReport::Bit(BlockReason::HumanCargoRestricted),"reason bitmask accumulated duplicates");
}

}
int main() {
    try {
        ReadCellsAndArrayHeaders();PrimaryHorseAndBonding();PlayerPredicatesAndCooldowns();HistoryStopsAllVariants();
        DirectoryAndGroupQuotas();MissionMappingAndStoryWindows();ActiveSchedulerSlots();SnapshotAndCandidates();
        UnknownConfigurationAndHoles();AdmissionBaselineAndUnknowns();AdmissionStartAndKnownBlockers();
        AdmissionSlotCapacity();AdmissionPlacesVolumesAndDensity();AdmissionSpecialPlaceList();MultipleDiagnosticReasons();
        std::cout<<"Synthetic native encounter conditions and candidate filters passed; not live-game validation\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
