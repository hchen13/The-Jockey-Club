#include "horse_ledger_runtime.h"
#include "horse_race_ledger_store.h"
#include "horse_ledger_binding.h"
#include "horse_save_startup_probe.h"
#include "horse_individual_identity.h"
#include "race_progression.h"
#include "race_localization.h"
#include "game_natives.h"
#include "log_sink.h"
#include "world/camera_blackout.h"
#include "world/frame.h"
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include "runtime_paths.h"
#include <map>
#include <memory>
#include <sstream>
#include <limits>

namespace horse_races::horse_ledger::runtime {
namespace {
namespace fs = std::filesystem;
struct Race { Win win; Revision base; std::uint64_t timeline{}; };
struct Pack { Frozen frozen; std::uint64_t tick{}; };
struct Loading { std::uint32_t slot{}; std::uint64_t request{}; std::optional<StableFileHash> before; };
struct State {
    bool initialized{}, enabled{}, pumping{}, sales{}, sale_in_progress{}, cash_in_progress{};
    std::uint64_t timeline{1}, mutation_tick{}, next_index{}, next_horse{}, ready_since{};
    std::uint64_t payment_timeline{}, payment_tick{};
    bool startup_pending{};
    std::uint64_t handled_sequence{};
    std::shared_ptr<const Frozen> archive_mirror;
    std::uint32_t payment_pack{};
    std::string profile, last_horse, identity_issue, startup_wait, archive_issue;
    fs::path profile_dir;
    void (*log)(const std::string&){};
    std::unique_ptr<log_sink::Sink> audit;
    std::unique_ptr<Store> store;
    Ledger ledger;
    std::optional<Ledger> suspended;
    std::optional<std::uint64_t> market_blocked_timeline;
    std::string market_blocked_event;
    binding::Detector detector;
    std::deque<save_observer::Observation> observations;
    std::array<std::optional<StableFileHash>,16> cache;
    std::map<std::uint32_t,Pack> packs;
    std::optional<Loading> loading;
    std::optional<Snapshot> restoring;
    std::optional<SlotBinding> active_slot, restore_slot, restore_previous;
    std::optional<Revision> transaction_base;
    std::optional<JournalIdentity> transaction_journal;
    bool create_restore{}, waiting_world{};
    std::string restore_origin;
    std::optional<Race> racing, payment;
};
State& S() { static State state; return state; }
void RevokeArchiveMirror(){save_observer::RevokeArchiveMirror();}
void OfferArchiveMirror(){
    auto& s=S();
    if(!s.enabled||!s.store||!s.ledger.Active()||s.loading||s.restoring||s.sale_in_progress||s.cash_in_progress){
        RevokeArchiveMirror();return;
    }
    if(!s.archive_mirror||s.archive_mirror->epoch!=s.ledger.Epoch()||
       s.archive_mirror->mutation!=s.ledger.Mutation()||s.archive_mirror->parent!=*s.ledger.Active()){
        const auto frozen=s.ledger.Freeze();if(!frozen){RevokeArchiveMirror();return;}
        s.archive_mirror=std::make_shared<const Frozen>(*frozen);
    }
    save_observer::OfferArchiveMirror(s.archive_mirror,s.handled_sequence,s.mutation_tick,s.store->Root().native());
}
struct ReofferArchiveOnExit {
    ~ReofferArchiveOnExit(){try{if(!S().pumping)OfferArchiveMirror();}catch(...){try{RevokeArchiveMirror();}catch(...){}}}
};
void Log(const std::string& message) {
    auto& s=S();
    const auto line="[HorseLedger] "+message;
    if(s.log)s.log(line);
    if(s.audit)s.audit->Write(std::to_string(GetTickCount64())+" "+line,true);
}
void WarnArchiveSave(){
    S().archive_issue=i18n::Text("Racing records could not be saved. Your records have been kept. Check storage and save again.",
        "赛马记录保存失败，原有记录已保留。请检查存储空间后重新保存。");
}
bool RefreshArchivePins(const char* reason) {
    auto& s=S();if(!s.store)return true;
    std::vector<Revision> revisions;
    const auto keep=[&](const Revision& revision){
        if(std::find(revisions.begin(),revisions.end(),revision)==revisions.end())revisions.push_back(revision);
    };
    if(const auto active=s.ledger.Active())keep(*active);
    if(s.suspended)if(const auto active=s.suspended->Active())keep(*active);
    if(s.restoring)keep(s.restoring->revision);
    if(s.restore_previous)keep(s.restore_previous->archive);
    for(const auto& [counter,pack]:s.packs)keep(pack.frozen.parent);
    std::vector<JournalIdentity> journals;
    if(s.racing){keep(s.racing->base);journals.push_back({s.racing->base.profile,s.racing->win.event_id});}
    if(s.payment){keep(s.payment->base);journals.push_back({s.payment->base.profile,s.payment->win.event_id});}
    if(s.transaction_base)keep(*s.transaction_base);
    if(s.transaction_journal)journals.push_back(*s.transaction_journal);
    s.store->SetJournalPins(journals);
    std::string error;
    if(s.store->RetainRevisions(revisions,error))return true;
    WarnArchiveSave();
    Log(std::string("archive retention failed during ")+reason+"; working state retained: "+error);
    return false;
}
// Native transaction callbacks may pump queued saves or even a later load.
// Keep their original journal base and event alive until the callback returns.
struct TransactionPins {
    State& state;
    std::optional<Revision> previous_base;
    std::optional<JournalIdentity> previous_journal;
    TransactionPins(State& s,const Revision& revision,const std::string& event):state(s),
        previous_base(s.transaction_base),previous_journal(s.transaction_journal){
        state.transaction_base=revision;state.transaction_journal=JournalIdentity{revision.profile,event};
    }
    ~TransactionPins(){
        state.transaction_base=std::move(previous_base);state.transaction_journal=std::move(previous_journal);
        try{RefreshArchivePins("transaction completion");}catch(...){}
    }
};
void CloseStartup(const std::string& reason){
    auto& s=S();if(!s.startup_pending)return;
    s.startup_pending=false;save_startup_probe::RequestStop();
    Log("startup Continue closed: "+reason);
}
std::string Describe(const save_observer::Observation& observation) {
    const auto& r=observation.raw;
    const auto end=std::find(r.filename.begin(),r.filename.end(),'\0');
    return "seq="+std::to_string(observation.sequence)+" tick="+std::to_string(observation.tick_ms)+
        " healthy="+std::to_string(observation.healthy)+" load="+std::to_string(r.load_state)+
        "/"+std::to_string(r.load_phase)+" pending="+std::to_string(r.pending)+
        " load_slot="+std::to_string(r.load_slot)+" op="+std::to_string(r.operation)+
        " save="+std::to_string(r.save_state)+" slot="+std::to_string(r.save_slot)+
        " disk="+std::to_string(r.disk_state)+" pack_state="+std::to_string(r.pack_state)+
        " pack="+std::to_string(r.pack_counter)+" disk_request="+std::to_string(r.disk_counter)+
        " file="+std::string(r.filename.begin(),end);
}
void PullObservations() {
    auto& pending=S().observations;
    for(auto& observation:save_observer::Drain())pending.push_back(std::move(observation));
}
std::string SlotName(std::uint32_t slot) {
    char name[16]{};std::snprintf(name,sizeof(name),"SRDR3%04u",slot);return name;
}
fs::path SlotPath(std::uint32_t slot) { return S().profile_dir/SlotName(slot); }
bool HexProfile(const std::string& name) {
    return name.size()==8 && std::all_of(name.begin(),name.end(),[](unsigned char c){
        return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    });
}
std::string EventId() {
    std::array<unsigned char,16> bytes{};
    if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
    constexpr char hex[]="0123456789abcdef";std::string out;
    for(const auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
std::uint64_t UnixMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
void Unbind(const std::string& reason) {
    auto& s=S();RevokeArchiveMirror();s.ledger.Unbind();s.packs.clear();s.restoring.reset();s.ready_since=0;
    s.active_slot.reset();s.restore_slot.reset();s.restore_previous.reset();
    s.create_restore=false;s.waiting_world=false;s.restore_origin.clear();
    s.racing.reset();s.payment.reset();s.cash_in_progress=false;s.last_horse.clear();Log("unbound reason="+reason);
    RefreshArchivePins("unbind");
}
void Quarantine(const std::string& reason) {
    auto& s=S();
    if(s.ledger.Active())s.suspended=s.ledger;
    Unbind(reason);
    // A failed native load may already have changed the world. Preserve the old
    // working copy, but never re-enable it merely because native fields go idle.
    Log("previous working archive suspended until a proven successful load");
}
void Disable(const std::string& reason) {
    S().startup_pending=false;save_startup_probe::RequestStop();
    ++S().timeline;Unbind(reason);S().enabled=false;save_observer::RequestStop();
    Log("ledger and sales disabled; existing race economics remain active");
}
bool WorldReady() {
    if(NETWORK::NETWORK_IS_GAME_IN_PROGRESS() || HUD::IS_PAUSE_MENU_ACTIVE() ||
        !world::CameraBlackout{}.FullyVisible())return false;
    const auto ped=PLAYER::PLAYER_PED_ID();
    return ped && ENTITY::DOES_ENTITY_EXIST(ped) && !ENTITY::IS_ENTITY_DEAD(ped);
}
bool Busy(const save_observer::Observation& observation) {
    const auto& r=observation.raw;
    return !observation.healthy || r.pending!=0 || r.load_state!=0 || r.operation!=0 ||
        r.save_state!=0 || r.disk_state!=0 || r.pack_state!=0 || r.auto_state==1;
}
std::optional<StableFileHash> ReadSlot(std::uint32_t slot) {
    if(slot>=16)return {};
    std::string error;auto hash=HashStableFile(SlotPath(slot),error);
    if(!hash)Log("save hash rejected slot="+std::to_string(slot)+" reason="+error);
    return hash;
}
// Metadata is merely an index optimization. Binding always rehashes the target
// and requires the full contents to agree across a witnessed load window.
void IndexFiles() {
    auto& s=S();
    for(std::uint32_t slot=0;slot<16;++slot){
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(!GetFileAttributesExW(SlotPath(slot).c_str(),GetFileExInfoStandard,&data)){
            s.cache[slot].reset();continue;
        }
        const auto stamp=(std::uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
        const auto size=(std::uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
        if(!s.cache[slot]||s.cache[slot]->write_filetime!=stamp||s.cache[slot]->size!=size)
            s.cache[slot]=ReadSlot(slot);
    }
}
void Report(const Guid& guid,const std::string& name) {
    auto& s=S();const auto sum=s.ledger.SummaryFor(guid);
    if(!sum.valid)return;
    Log("horse guid="+guid.Key()+" name="+name+" official_wins="+std::to_string(sum.official_wins)+
        " practice_wins="+std::to_string(sum.practice_wins)+" premium_cents="+std::to_string(sum.premium_cents)+
        " evidence="+std::to_string(static_cast<int>(s.ledger.Quality()))+
        " sold="+std::to_string(s.ledger.Sold(guid))+" sale_enabled="+std::to_string(s.sales));
}
void Append(const Race& race) {
    ReofferArchiveOnExit reoffer;
    auto& s=S();
    if(race.timeline!=s.timeline){Log("record discarded: previous loaded world");return;}
    RevokeArchiveMirror();
    const auto added=s.ledger.Append(race.win,true);
    if(added==AddResult::Added){
        s.mutation_tick=GetTickCount64();s.last_horse.clear();
        Log("win event="+race.win.event_id+" guid="+race.win.horse.Key()+" race="+race.win.race_name+
            " net_cents="+std::to_string(race.win.net_cents)+" practice="+std::to_string(race.win.practice));
        Report(race.win.horse,race.win.horse_name);
    } else if(added!=AddResult::Duplicate)Unbind("win append rejected code="+std::to_string(static_cast<int>(added)));
}
Revision NewArchiveRevision() {
    const auto nonce=EventId();
    return {S().profile,nonce.empty()?std::string{}:Sha256(std::span(
        reinterpret_cast<const std::uint8_t*>(nonce.data()),nonce.size()))};
}
#include "horse_ledger_runtime_binding.inl"
}
namespace {
void AdoptStartupProof(){
    auto& s=S();
    // Only the first untouched startup can adopt this one-shot capability.
    // Manual requests/restores have precedence and never consume old startup state.
    if(!s.startup_pending)return;
    if(!s.enabled||s.timeline!=1||s.ledger.Active()||
        s.loading||s.restoring||s.racing||s.payment||s.sale_in_progress||s.ledger.Mutation()){
        CloseStartup("runtime timeline or ledger activity superseded startup");return;
    }
    const auto before=save_observer::Latest();
    const auto& raw=before.raw;
    if((before.sequence&&!before.healthy)||raw.pending||raw.load_state||raw.pack_state||
        raw.pack_counter||raw.disk_counter||raw.save_state||raw.disk_state||raw.auto_state==1||
        (raw.operation!=0&&raw.operation!=3)){CloseStartup("observer failed or write/load activity overlapped "+Describe(before));return;}
    auto status=save_startup_probe::ProofState::NotReady;
    std::string reason;const auto proof=save_startup_probe::TakeProof(s.profile,
        before,GetTickCount64(),reason,&status);
    if(!proof){
        if(status==save_startup_probe::ProofState::Rejected)CloseStartup("proof rejected: "+reason);
        else if(s.startup_wait!=reason){s.startup_wait=reason;Log("startup Continue waiting: "+reason);}
        return;
    }
    Pump(true);
    const auto after=save_observer::Latest();
    if(!s.enabled||s.timeline!=1||s.ledger.Active()||s.loading||s.restoring||
        !after.healthy||after.raw!=before.raw||after.sequence!=before.sequence||
        !save_startup_probe::ConfirmProof(*proof,after,GetTickCount64())){
        CloseStartup("changed while reading exact snapshot; left unbound");return;
    }
    ++s.timeline;s.packs.clear();
    if(!StageArchive(proof->slot,proof->sha)){CloseStartup("exact archive staging failed");return;}
    // Confirm the one-shot capability and stage its exact source before stopping
    // acquisition. Pending hashes/readiness alone never expire this handoff.
    CloseStartup("exact source confirmed and archive staged");
    Log("startup Continue exact restore staged slot="+std::to_string(proof->slot)+
        " sha="+proof->sha+" wins="+std::to_string(s.restoring->wins.size())+
        " request="+std::to_string(proof->request)+" terminal="+std::to_string(proof->terminal)+
        " max_poll_gap="+std::to_string(proof->max_gap));
}
}
void Initialize(const std::string& directory,void (*log)(const std::string&),Options options) {
    horse_diagnostics::MarkFaultStage("ledger init: enter");
    auto& s=S();s.log=log;
    // ScriptHook may re-enter ScriptMain after a load while ASI globals/worker
    // survive. Drain that load before touching its pre-request file evidence.
    if(s.initialized){
        horse_diagnostics::MarkFaultStage("ledger reentry: observer pump");
        if(s.enabled)Pump(true);return;
    }
    s.initialized=true;
    s.sales=options.sales;
    try{
        s.audit=std::make_unique<log_sink::Sink>();
        const auto paths=runtime_paths::Resolve(directory);
        runtime_paths::EnsureDirectory(paths.Log("TheJockeyClubHorseLedger.log").parent_path());
        s.audit->Open(paths.Log("TheJockeyClubHorseLedger.log").string());
        PWSTR documents{};
        if(FAILED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents))){save_startup_probe::RequestStop();Log("Documents folder unavailable");return;}
        const fs::path profiles=fs::path(documents)/"Rockstar Games"/"Red Dead Redemption 2"/"Profiles";
        CoTaskMemFree(documents);
        std::vector<fs::path> choices;
        for(const auto& entry:fs::directory_iterator(profiles))
            if(entry.is_directory()&&HexProfile(entry.path().filename().string())&&
                (options.profile.empty()||entry.path().filename().string()==options.profile))choices.push_back(entry.path());
        if(choices.size()!=1){save_startup_probe::RequestStop();Log("profile selection ambiguous or missing; validation disabled");return;}
        s.profile_dir=choices.front();s.profile=s.profile_dir.filename().string();
        s.store=std::make_unique<Store>(paths.Archives()/"ModArchives-v2");
        // Old development ledgers/global progress are deliberately not imported.
        // Only an actually loaded source creates its own initial slot archive.
        IndexFiles();s.next_index=GetTickCount64()+1000;
        if(!save_observer::Start(s.profile_dir.native())){save_startup_probe::RequestStop();Log("observer unavailable: "+save_observer::Latest().issue);return;}
        s.enabled=true;s.startup_pending=true;
        horse_diagnostics::MarkFaultStage("ledger init: observer pump");
        Pump();
        horse_diagnostics::MarkFaultStage("ledger init: cold load proof");
        AdoptStartupProof();
        Log("ledger ready profile="+s.profile+"; startup Continue requires one-shot exact proof; manual load remains available; sale_policy="+
            std::string(s.sales?"paired_archive_v2":"disabled"));
    }catch(const std::exception& e){Disable(std::string("initialize failed: ")+e.what());}
}
void Tick() {
    auto& s=S();if(!s.enabled)return;
    Pump();if(!s.enabled)return;
    AdoptStartupProof();
    try{
        const auto now=GetTickCount64();
        if(now>=s.next_index){s.next_index=now+1000;IndexFiles();}
        if(now<s.next_horse||!s.ledger.Active()||!WorldReady())return;
        s.next_horse=now+1000;
        const auto horse=PED::GET_MOUNT(PLAYER::PLAYER_PED_ID());
        if(!horse){s.last_horse.clear();return;}
        std::string reason;const auto identity=individual_identity::Capture(horse,reason);
        if(!identity){
            if(s.identity_issue!=reason){s.identity_issue=reason;Log("mounted horse identity unavailable: "+reason);}
            return;
        }
        s.identity_issue.clear();
        const auto key=identity->guid.Key()+"/"+identity->name+"/"+std::to_string(s.ledger.Mutation());
        if(s.last_horse!=key){s.last_horse=key;Report(identity->guid,identity->name);}
    }catch(const std::exception& e){Disable(std::string("tick failed: ")+e.what());}
}
std::uint64_t Timeline(){if(S().enabled)Pump(true);return S().enabled?S().timeline:0;}
std::uint64_t ObservedTimeline(){return S().enabled?S().timeline:0;}
std::string ArchiveIssue(){return S().archive_issue;}
std::optional<ProgressView> ReadProgress(){
    const auto& s=S();
    if(!s.enabled||!s.ledger.Active()||s.loading||s.restoring||s.waiting_world||!s.ledger.Progression())return {};
    return ProgressView{s.timeline,*s.ledger.Progression()};
}
bool WriteProgress(std::uint64_t expected_generation,const std::string& expected,const std::string& replacement){
    ReofferArchiveOnExit reoffer;
    auto& s=S();if(!s.enabled)return false;Pump(true);
    const auto view=ReadProgress();const auto observed=save_observer::Latest();
    if(!view||view->generation!=expected_generation||view->encoded!=expected||s.sale_in_progress||
        !observed.healthy||observed.raw.pending||observed.raw.load_state)return false;
    const auto parsed=progression::State::Decode(replacement);
    if(!parsed || parsed->Encode()!=replacement)return false;
    RevokeArchiveMirror();
    if(!s.ledger.SetProgression(replacement))return false;
    if(expected!=replacement){
        s.mutation_tick=GetTickCount64();
        Log("archive campaign changed timeline="+std::to_string(s.timeline)+" save_required=1");
    }
    return true;
}
void BeginRace(int horse,Win win) {
    auto& s=S();CloseStartup("race attempt");if(!s.enabled)return;Pump(true);s.racing.reset();
    RefreshArchivePins("race preparation");
    const auto observed=save_observer::Latest();const auto operation=observed.raw.operation;
    if(!s.ledger.Active()||s.restoring||s.loading||s.waiting_world||!WorldReady()||!observed.healthy||
        observed.raw.pending||observed.raw.load_state||(operation!=0&&operation!=2&&operation!=6&&operation!=7)){
        Log("race not recorded: archive or world is not ready");return;
    }
    std::string reason;const auto identity=individual_identity::Capture(horse,reason);
    if(!identity){Log("race identity rejected: "+reason);return;}
    win.event_id=EventId();win.horse=identity->guid;win.horse_name=identity->name;win.horse_model=identity->model;
    if(win.practice)win.net_cents=0;
    if(!Validate(win,&reason)){Log("race metadata rejected: "+reason);return;}
    s.racing=Race{std::move(win),*s.ledger.Active(),s.timeline};
    if(!RefreshArchivePins("race capture")){s.racing.reset();return;}
    Log("race captured event="+s.racing->win.event_id+" guid="+identity->guid.Key()+" race="+s.racing->win.race_name);
}
void Outcome(bool won,bool accident) {
    auto& s=S();if(!s.enabled)return;Pump(true);
    auto race=std::move(s.racing);s.racing.reset();
    if(!race||!won||race->timeline!=s.timeline||!s.ledger.Active()){RefreshArchivePins("race outcome");return;}
    race->win.unix_ms=UnixMs();race->win.opponent_accident=accident;
    if(race->win.practice||race->win.net_cents==0){Append(*race);RefreshArchivePins("race outcome");return;}
    if(s.payment){Unbind("another unpaid winning receipt is pending");return;}
    s.payment=std::move(race);
    RefreshArchivePins("pending race payment");
}
void CancelRace(){S().racing.reset();RefreshArchivePins("race cancellation");}
void PaymentQueued(std::uint64_t timeline){S().payment_timeline=timeline;}
PaymentDecision BeforePayment(int amount,std::uint64_t timeline) {
    auto& s=S();CloseStartup("race payment attempt");
    if(s.enabled)Pump(true);
    if(timeline && timeline!=s.timeline){
        if(s.payment&&s.payment->timeline==timeline){s.payment.reset();RefreshArchivePins("stale race payment");}
        Log("discarded pending cash from previous loaded world");return PaymentDecision::Discard;
    }
    if(!s.enabled)return PaymentDecision::Allow;
    const auto observed=save_observer::Latest();
    if(!observed.healthy)return PaymentDecision::Defer;
    if(Busy(observed)||!WorldReady())return PaymentDecision::Defer;
    if(s.payment){
        if(amount<=0||amount!=s.payment->win.net_cents||s.payment->timeline!=s.timeline||
            s.payment_timeline!=timeline){Unbind("cash and pending win differ");}
        else {
            if(!RefreshArchivePins("race payment"))return PaymentDecision::Defer;
            std::string error;const auto result=s.store->Journal(s.payment->base,s.payment->win,false,error);
            if(result==WriteResult::Error||result==WriteResult::Conflict){WarnArchiveSave();Log("race payment deferred: prepared receipt failed: "+error);return PaymentDecision::Defer;}
        }
    }
    RevokeArchiveMirror();
    s.payment_pack=observed.raw.pack_counter;s.payment_tick=GetTickCount64();s.cash_in_progress=true;
    return PaymentDecision::Allow;
}
void PaymentFailed() {
    ReofferArchiveOnExit reoffer;
    auto& s=S();if(s.enabled)Pump(true);s.cash_in_progress=false;
}
void AfterPayment(int amount) {
    ReofferArchiveOnExit reoffer;
    auto& s=S();if(!s.enabled)return;
    struct CashGuard {State& state;~CashGuard(){state.cash_in_progress=false;}} guard{s};
    const auto race=s.payment;
    if(race && (amount<=0 || amount!=race->win.net_cents)){
        Unbind("actual cash amount differs from winning receipt");return;
    }
    // Preserve immutable paid evidence even if a load/save edge now invalidates
    // active binding. Receipts are audit data and can NEVER repay or auto-restore.
    if(race && amount>0 && amount==race->win.net_cents){
        std::string error;const auto result=s.store->Journal(race->base,race->win,true,error);
        if(result==WriteResult::Error||result==WriteResult::Conflict){
            WarnArchiveSave();
            Log("paid race audit write failed; confirmed live result retained: "+error);
        }
    }
    Pump(true);
    const auto after=save_observer::Latest();
    if(race){
        if(race->timeline!=s.timeline){Log("paid receipt belongs to the previous world; no replay");return;}
        const auto operation=after.raw.operation;
        if(!after.healthy||after.raw.pending||after.raw.load_state||
            (operation!=0&&operation!=2&&operation!=6&&operation!=7)){
            Quarantine("payment overlaps unknown load state; receipt retained without replay");return;
        }
        Append(*race);
    }
    s.payment.reset();
    RefreshArchivePins("race payment completion");
}
#include "horse_ledger_runtime_market.inl"
}
