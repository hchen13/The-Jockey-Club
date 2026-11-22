// Enable/disable lifecycle, legacy adoption and script-loop initialization.
// Included at the original definition site; declaration and initialization order are preserved.
#include "plugin_casual_mode.inl"
#include "race_speed_policy.h"
void SetEnabled(bool enabled, bool audible = true)
{
    horse_races::race_speed_policy::SetEnabled(enabled);
    if (g_enabled == enabled) {
        return;
    }
    g_enabled = enabled;
    if (enabled) {
        // The sprint gait the whole field is driven at. It was tuned under
        // [GaitAccel] and an ini that still carries it there is answered.
        const std::string legacy_gait_target = LoadString("GaitAccel", "TargetDesiredBlend", "2.90");
        g_gait_target_desired_blend = LoadFloat(
            "AutoCurve", "TargetDesiredBlend", legacy_gait_target.c_str(), 1.0F, 5.0F, 2.9F);
        for (std::size_t index = 0; index < g_auto_speed_targets.size(); ++index) {
            const std::string key = "SpeedTarget" + std::to_string(index + 1);
            const std::string fallback = std::to_string(g_auto_speed_targets[index]);
            g_auto_speed_targets[index] =
                LoadFloat("AutoCurve", key.c_str(), fallback.c_str(), 10.0F, 120.0F, g_auto_speed_targets[index]);
        }
        for (std::size_t index = 0; index < g_auto_low_speed_blends.size(); ++index) {
            const std::string key = "LowSpeedBlend" + std::to_string(index + 1);
            const std::string fallback = std::to_string(g_auto_low_speed_blends[index]);
            g_auto_low_speed_blends[index] = LoadFloat(
                "AutoCurve", key.c_str(), fallback.c_str(), 1.0F, 4.0F, g_auto_low_speed_blends[index]);
        }
        for (std::size_t index = 0; index < g_auto_high_speed_forces.size(); ++index) {
            const std::string key = "HighSpeedForce" + std::to_string(index + 6);
            const std::string fallback = std::to_string(g_auto_high_speed_forces[index]);
            g_auto_high_speed_forces[index] = LoadFloat(
                "AutoCurve", key.c_str(), fallback.c_str(), 0.0F, 20.0F, g_auto_high_speed_forces[index]);
        }
        for (std::size_t index = 0; index < g_auto_low_accel_ramp_seconds.size(); ++index) {
            const std::string key = "LowAccelRamp" + std::to_string(index + 1);
            const std::string fallback = std::to_string(g_auto_low_accel_ramp_seconds[index]);
            g_auto_low_accel_ramp_seconds[index] = LoadFloat(
                "AutoCurve", key.c_str(), fallback.c_str(), 0.1F, 30.0F, g_auto_low_accel_ramp_seconds[index]);
        }
        for (std::size_t index = 0; index < g_auto_high_accel_responses.size(); ++index) {
            const std::string key = "HighAccelResponse" + std::to_string(index + 6);
            const std::string fallback = std::to_string(g_auto_high_accel_responses[index]);
            g_auto_high_accel_responses[index] = LoadFloat(
                "AutoCurve", key.c_str(), fallback.c_str(), 0.001F, 1.0F, g_auto_high_accel_responses[index]);
        }
        for (std::size_t index = 0; index < g_auto_force_ramp_seconds.size(); ++index) {
            const std::string key = "ForceRampSeconds" + std::to_string(index + 1);
            const std::string fallback = std::to_string(g_auto_force_ramp_seconds[index]);
            g_auto_force_ramp_seconds[index] = LoadFloat(
                "AutoCurve", key.c_str(), fallback.c_str(), 0.1F, 30.0F, g_auto_force_ramp_seconds[index]);
        }
        g_auto_ramp_start_blend =
            LoadFloat("AutoCurve", "RampStartBlend", "1.50", 0.1F, 3.0F, 1.5F);
        g_auto_open_blend = LoadFloat("AutoCurve", "OpenBlend", "3.20", 2.9F, 12.2F, 3.2F);
        g_auto_force_start_kmh =
            LoadFloat("AutoCurve", "ForceStartKmh", "40.0", 3.0F, 80.0F, 40.0F);
        g_rider_gait_cap = LoadFloat("AutoCurve", "RiderGaitCap", "2.90", 0.0F, 3.0F, 2.9F);
        g_player_gait_by_bonding = LoadFloat("AutoCurve", "PlayerGaitByBonding", "1", 0.0F, 1.0F, 1.0F) >= .5F;
        g_rider_force_offset = LoadFloat("AutoCurve", "RiderForceOffset", "0.0", 0.0F, 8.0F, 0.0F);
        g_stimulant_config.regular_seconds = LoadFloat("Stimulant", "RegularSeconds", "8.0", 0.0F, 120.0F, 8.0F);
        g_stimulant_config.potent_seconds = LoadFloat("Stimulant", "PotentSeconds", "10.0", 0.0F, 120.0F, 10.0F);
        g_stimulant_config.special_seconds = LoadFloat("Stimulant", "SpecialSeconds", "20.0", 0.0F, 120.0F, 20.0F);
        g_stimulant_config.speed_level = static_cast<int>(LoadFloat("Stimulant", "SpeedLevel", "11", 1.0F, 11.0F, 11.0F));
        g_stimulant_config.acceleration_level = static_cast<int>(LoadFloat("Stimulant", "AccelerationLevel", "10", 1.0F, 10.0F, 10.0F));
        for (int b = 1; b <= 5; ++b) {
            const std::string key = "BondingFactor" + std::to_string(b);
            const char* fallback = b == 1 ? "1.0" : b == 2 ? "1.5" : b == 3 ? "2.0" : b == 4 ? "2.5" : "3.0";
            g_stimulant_config.bonding_factor[static_cast<std::size_t>(b - 1)] =
                LoadFloat("Stimulant", key.c_str(), fallback, 0.1F, 10.0F, static_cast<float>(b == 1 ? 1.0 : b == 2 ? 1.5 : b == 3 ? 2.0 : b == 4 ? 2.5 : 3.0));
        }
        {
            const std::string tier=LoadString("Stimulant","UncountedDoseTier","potent");
            g_stimulant_config.uncounted_tier=tier=="regular"?horse_races::stimulant::Tier::Regular:
                tier=="special"?horse_races::stimulant::Tier::Special:horse_races::stimulant::Tier::Potent;
        }
        LoadStimulantHudConfig();
        g_auto_force_hold_kmh =
            LoadFloat("AutoCurve", "ForceHoldKmh", "33.0", 3.0F, 80.0F, 33.0F);
        g_auto_force_reset_kmh =
            LoadFloat("AutoCurve", "ForceResetKmh", "30.0", 0.0F, 80.0F, 30.0F);
        g_auto_force_reset_seconds =
            LoadFloat("AutoCurve", "ForceResetSeconds", "1.0", 0.0F, 10.0F, 1.0F);
        g_player_motion.curve={};
        g_auto_mount = 0;
        g_player_motion.Reset();
        ResetRaceOpponentCurve();
        RefreshMotionConfig();
        g_next_attribute_refresh = Clock::time_point{};
        LogMountAttributes();
        const Ped player = PLAYER::PLAYER_PED_ID();
        if (PED::IS_PED_ON_MOUNT(player)) {
            SelectAutoMount(PED::GET_MOUNT(player));
        }
        if (audible) {
            SoundOn();
        }
        Log("F11 ON: AutoCurve started; displayed speed=" + std::to_string(g_auto_speed_level) +
            " acceleration=" + std::to_string(g_auto_accel_level) +
            " target=" + std::to_string(g_metrics_target_kmh) + " km/h");
    } else {
        ReleasePlayerGaitExperiment("override switched off");
        ReleasePlayerGaitMinimum();
        for(auto& motion:g_extra_motion)motion.Reset();
        g_stimulant.Reset();g_stimulant_detection.Reset();
        if(g_stimulant_reported)horse_races::PlayerBurstChanged(false,0);
        g_stimulant_reported=false;
        g_player_motion.Reset();
        g_auto_mount = 0;
        ResetRaceOpponentCurve();
        g_player_motion.curve={};
        if (audible) {
            SoundOff();
        }
        Log("F11 OFF: AutoCurve stopped");
    }
}

// The mod shipped as HorseSpeedLab through 1.18.0.
// Keep an ASI-only update on one complete old layout. Installation, while the
// game is stopped, owns migration; startup must not copy a config after the
// early save observer has already selected its profile.
void AdoptLegacyInstall()
{
    std::error_code failed;
    const std::filesystem::path directory(g_directory);
    if(g_paths.legacy)
        Log("legacy installation compatibility: config="+g_paths.MainConfig().string()+
            "; all mod data stays in the legacy layout until an offline install migrates it");
    if(std::filesystem::exists(directory/"HorseSpeedLab.asi",failed))
        Log("WARNING: HorseSpeedLab.asi is still installed. It is the same mod under its old name and will "
            "run a second, independent race system. Delete it before playing.");
}

__declspec(noinline) void InitializeScriptSession()
{
    horse_diagnostics::MarkFaultStage("startup: session");
    horse_races::map_textures::BeginScriptSession();
    g_stimulant.Reset();g_stimulant_detection.Reset();g_stimulant_reported=false;
    g_player_speed_learning.Reset();
    g_stimulant_observed_reported=g_stimulant_counts_reported=false;
    g_directory = ModuleDirectory();
    g_paths=horse_races::runtime_paths::Resolve(g_directory);
    const bool paths_ready=g_paths.PrepareRuntimeDirectories();
    horse_races::log_sink::Shared().Open(g_paths.Log("TheJockeyClub.log").string());
    if(!paths_ready)Log("some mod directories are unavailable; existing files remain in their selected layout");
    AdoptLegacyInstall();
    g_motion_recorder.directory=g_directory;
    g_motion_recorder.enabled=LoadFloat("Telemetry","RaceMotionDiagnostics","1",0,1,1)>=.5F;
    const bool driving_trace=g_motion_recorder.enabled && LoadFloat("Telemetry","RaceDrivingTrace","1",0,1,1)>=.5F;
    horse_races::race_pace::diagnostics_enabled=driving_trace;
    horse_motion::native_vector::diagnostics_enabled.store(driving_trace,std::memory_order_relaxed);
    Log(std::string("race driving trace: ")+(driving_trace?"enabled; observation only":"disabled"));
    // A recording is evidence for the race under investigation, not an
    // archive: 71 files and 369 MB had accumulated before anything bounded it.
    g_motion_recorder.retention.keep_files=
        static_cast<unsigned>(LoadFloat("Telemetry","RaceMotionKeepFiles","6",0,100000,6));
    g_motion_recorder.retention.keep_bytes=static_cast<std::uint64_t>(
        LoadFloat("Telemetry","RaceMotionKeepMegabytes","256",0,1000000,256)*1024.0*1024.0);
    g_motion_recorder.log=Log;
    // [Telemetry] FrameCost=0 switches the per-stage timing off; it is read once, here, and never polled.
    horse_diagnostics::costs.enabled=LoadFloat("Telemetry","FrameCost","1",0,1,1)>=.5F;
    // [Telemetry] LogFold=0 lifts the two-lines-a-second limit on each place that writes, for when
    // something is being tracked down through the log itself. Read once, here.
    horse_races::log_sink::Shared().SetFolding(LoadFloat("Telemetry","LogFold","1",0,1,1)>=.5F);
    g_sound_on = MakeCue(true);
    g_sound_off = MakeCue(false);
    Log("The Jockey Club 1.18.0 loaded; native race BGM selections; bonding gait minimum active on sprint; "
        "isolated gait probes default off");
    horse_diagnostics::MarkFaultStage("startup: native resources");
    horse_races::cues::Load(g_paths.MainConfig().string());
    horse_races::cues::PrepareAll(); // A mission soundset has to be asked for early.
    horse_races::bg_text::Initialize();
    horse_races::progression::runtime::SetArchiveProvider(
        []()->std::optional<horse_races::progression::runtime::ArchiveView>{
            const auto view=horse_races::horse_ledger::runtime::ReadProgress();
            if(!view)return {};
            return horse_races::progression::runtime::ArchiveView{view->generation,view->encoded};
        },horse_races::horse_ledger::runtime::WriteProgress,horse_races::horse_ledger::runtime::ArchiveIssue);
    horse_diagnostics::MarkFaultStage("startup: progression");
    horse_races::progression::runtime::Initialize(g_directory,Log);
    horse_races::progression_toast::Initialize(Log);
    horse_races::progression::runtime::SetNoticeHandler([](std::optional<horse_races::progression::Race> race){
        auto text=horse_races::progression::Notice(race,horse_races::i18n::Chinese());
        horse_races::progression_toast::Show(std::move(text.title),std::move(text.body),text.duration_ms,text.icon);
        Log("progression toast queued="+(race?std::string(horse_races::progression::kKeys[horse_races::progression::Index(*race)]):"honorary-membership"));
    });
    char ledger_profile[64]{};
    GetPrivateProfileStringA("HorseLedger","Profile","",ledger_profile,64,
        g_paths.MainConfig().string().c_str());
    const bool horse_market_enabled=LoadFloat("HorseMarket","Enabled","1",0,1,1)>=.5F;
    horse_diagnostics::MarkFaultStage("startup: horse ledger");
    horse_races::horse_ledger::runtime::Initialize(g_directory,Log,
        {LoadFloat("HorseLedger","Validation","0",0,1,0)>=.5F,ledger_profile,horse_market_enabled});
    horse_races::progression::runtime::SetMembershipProvider([](horse_races::progression::Era era){
        using horse_races::horse_ledger::SellerEra;
        const auto seller=era==horse_races::progression::Era::Arthur?SellerEra::Arthur:
            era==horse_races::progression::Era::John?SellerEra::John:SellerEra::Unknown;
        return horse_races::horse_ledger::runtime::IsMember(seller);
    });
    horse_diagnostics::MarkFaultStage("startup: race owners");
    horse_races::Initialize(g_directory);
    horse_races::bookmaker_runtime::Initialize(g_directory,Log);
    horse_races::arthur_camp::Initialize(g_directory,Log);
    horse_races::horse_market_runtime::Initialize(Log,{horse_market_enabled,
        static_cast<std::int64_t>(LoadFloat("HorseMarket","BaseDollars","10",0,10000,10)*100.0F+.5F)});
    horse_settings::Initialize(g_directory);
    const bool enabled_on_startup =
        LoadFloat("General", "EnabledOnStartup", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    horse_diagnostics::MarkFaultStage("startup: speed setting");
    SetEnabled(enabled_on_startup, false);
}

__declspec(noinline) void RunInitializedScript()
{
    horse_diagnostics::MarkFaultStage("startup: bind observed timeline");
    auto toast_era=horse_races::progression::runtime::RealEra();
    auto toast_timeline=horse_races::horse_ledger::runtime::Timeline();
    horse_wheel::Labels wheel_labels;
    for (;;) {
        horse_diagnostics::MarkFaultStage("frame: compatibility services");
        horse_races::suhm::Tick(true);
        horse_races::bonus_horse_shop::Tick();
        horse_diagnostics::MarkFaultStage("frame: horse ledger");
        horse_races::horse_ledger::runtime::Tick();
        horse_diagnostics::MarkFaultStage("frame: camp and progression");
        horse_races::arthur_camp::ObserveWorld();
        horse_races::progression::runtime::Tick(horse_races::GameSeconds());
        const auto timeline=horse_races::horse_ledger::runtime::ObservedTimeline();
        const auto era=horse_races::progression::runtime::RealEra();
        if(timeline!=toast_timeline || era!=toast_era){
            horse_races::progression_toast::ResetWorld();toast_timeline=timeline;toast_era=era;
        }
        if(horse_settings::Open())horse_settings::SetProgressView(
            horse_races::progression::runtime::Summary(),horse_races::progression::runtime::status,
            horse_races::progression::runtime::WinSummary());
        horse_diagnostics::Stage("settings tick");
        // The race HUD is drawn with the menu shut, so its view is pushed every frame.
        horse_settings::SetRaceLapView(horse_races::GetLapView(),horse_races::bookmaker_runtime::Active());
        // Everything else here fills rows in the menu. Filling them with the menu shut cost
        // 0.108-0.144 ms a frame - two fresh vectors of strings among them - for rows nobody could
        // see (measured 2026-09-22). The requests are still taken every frame: one made on the frame
        // the menu closes must not be dropped.
        // Primed once so the rows are never empty the first time the menu is opened; after that only
        // while it is open, which means at most one frame of staleness if a file changed meanwhile.
        static bool menu_primed=false;
        if(horse_settings::Open() || !menu_primed) {
            menu_primed=true;
            horse_settings::SetCorridorView(horse_races::GetCorridorView());
            horse_settings::SetAutoSurveyView(horse_races::GetAutoSurveyState());
            horse_settings::SetCampActivities(horse_races::arthur_camp::ActivityNames());
            horse_settings::SetMaryActivities(horse_races::arthur_camp::MaryActivityNames());
            horse_settings::SetCampSweep(horse_races::arthur_camp::SweepLearning(),horse_races::arthur_camp::KnownPieces());
        }
        horse_settings::SetShowcaseActive(horse_races::showcase::Active());
        if (const auto requested = horse_settings::Tick(g_enabled)) SetEnabled(*requested);
        TickCasualModeChange();
        if(const auto action=horse_settings::TakeProgressAction();action!=horse_races::progression::Action::None){
            const auto before=horse_races::progression::runtime::state;
            const bool ready=horse_races::PrepareProgressionChange(
                horse_races::bookmaker_runtime::Active() ||
                horse_races::horse_market_runtime::Active() || MISC::GET_MISSION_FLAG());
            const auto result=horse_races::progression::runtime::Perform(action,!ready);
            if(result!=horse_races::progression::ActionResult::Ignored)
                horse_settings::PlayActionFeedback(result==horse_races::progression::ActionResult::Applied);
            if(before!=horse_races::progression::runtime::state)horse_races::progression_toast::Clear();
        }
        if(horse_settings::TakeMembershipReset()){
            std::string reason;
            const bool cleared=horse_races::horse_ledger::runtime::ClearMembershipRecords(
                horse_races::ProgressionBusy() || horse_races::bookmaker_runtime::Active() ||
                horse_races::horse_market_runtime::Active() || MISC::GET_MISSION_FLAG(),reason);
            if(cleared){
                horse_races::progression_toast::Clear();
            }
            horse_settings::SetMembershipResetStatus(cleared?horse_races::i18n::Text(
                "Sale and membership records cleared for this character. Save your game; sell another winning horse to rejoin.",
                "已清除当前主角卖马记录与会员。请保存游戏；重新卖马后才能入会。"):reason);
            horse_settings::PlayActionFeedback(cleared);
        }
        if(const auto corridor=horse_settings::TakeCorridorRequest())horse_races::ApplyCorridorRequest(*corridor);
        if(horse_settings::TakeAutoSurveyRequest())horse_races::ToggleAutoSurvey();
        if(horse_settings::TakeSurveyRequest())horse_races::scene_survey::Begin(g_directory,60.0F,Log);
        if(horse_settings::TakeCapturePositionRequest()){
            const Ped player=PLAYER::PLAYER_PED_ID();
            if(player && ENTITY::DOES_ENTITY_EXIST(player)){
                const auto at=ENTITY::GET_ENTITY_COORDS(player,true,false);
                const float heading=ENTITY::GET_ENTITY_HEADING(player);
                std::ostringstream line;
                line<<std::fixed<<std::setprecision(6)<<"player position x="<<at.x
                    <<" y="<<at.y<<" z="<<at.z<<" heading="<<heading;
                Log(line.str());
                if(!horse_races::files::AppendLine(
                    g_paths.Diagnostic("TheJockeyClub-positions.txt"),line.str()))
                    Log("player position capture file could not be written");
            }else Log("player position capture unavailable: player entity missing");
        }
        if(horse_settings::TakeAimRequest())horse_races::scene_survey::ProbeAim(g_directory,Log);
        if(const auto activity=horse_settings::TakeCampActivityRequest())horse_races::arthur_camp::AskActivity(*activity);
        if(const auto activity=horse_settings::TakeMaryActivityRequest())horse_races::arthur_camp::AskMaryActivity(*activity);
        if(horse_settings::TakeCampSweepRequest())horse_races::arthur_camp::RelearnSweep();
        horse_races::scene_survey::Tick();
        {
            const bool wanted=horse_settings::SceneRecordingWanted();
            if(wanted && !horse_races::scene_survey::Recording())horse_races::scene_survey::BeginRecording(g_directory,20.0F,Log);
            else if(!wanted && horse_races::scene_survey::Recording())horse_races::scene_survey::EndRecording();
            horse_races::scene_survey::RecordTick();
        }
        horse_diagnostics::Stage("race tick");
        const auto controls=horse_settings::TakeControlInput();
        horse_races::Tick(controls);
        horse_races::bookmaker_runtime::Tick();
        if(g_enabled) {
            horse_diagnostics::Scope scope("horse wheel labels");
            wheel_labels.Tick(horse_catalog::current,Log);
        }
        horse_diagnostics::Stage("arthur camp tick");
        horse_races::arthur_camp::Tick(controls.end_activity);
        horse_diagnostics::Stage("horse market tick");
        const char* market_blocked_by=horse_races::ProgressionBusy()?"race activity":
            horse_races::bookmaker_runtime::Active()?"bookmaker menu":
            horse_settings::Open()?"settings menu":MISC::GET_MISSION_FLAG()?"mission":nullptr;
        horse_races::horse_market_runtime::Tick(!market_blocked_by,market_blocked_by);
        horse_races::progression_toast::Tick(!horse_settings::Open()&&!horse_races::ProgressionToastBusy()&&
            !horse_races::bookmaker_runtime::Active()&&!horse_races::horse_market_runtime::Active()&&
            !MISC::GET_MISSION_FLAG()&&!NETWORK::NETWORK_IS_SESSION_ACTIVE()&&
            PLAYER::IS_PLAYER_PLAYING(PLAYER::PLAYER_ID())&&!CAM::IS_SCREEN_FADED_OUT()&&!CAM::IS_SCREEN_FADING_OUT());
        horse_diagnostics::Stage("horse motion tick");
        TickAutoCurves();
        horse_diagnostics::Stage("route recorder");
        TickRouteRecorder();
        horse_diagnostics::Stage(horse_diagnostics::kYield);
        horse_diagnostics::ReportCosts(Log);
        WAIT(0);
    }
}

void RunScript()
{
    // Return from initialization before taking the frame/tick locals on the
    // ScriptHook-owned fiber. Do not change affinity or observation ordering.
    InitializeScriptSession();
    RunInitializedScript();
}
