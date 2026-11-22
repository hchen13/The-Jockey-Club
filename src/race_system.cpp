#include "race_system.h"
#include "world/frame.h"
#include "world/identity.h"
#include "world/scan.h"
#include "race_map_textures.h"
#include "race_progression_runtime.h"
#include "race_progression_messages.h"
#include "race_progression_toast.h"
#include "race_bookmaker_runtime.h"
#include "race_bookmaker_resource_probe.h"
#include "race_showcase.h"
#include "race_destination.h"
#include "file_stamp.h"
#include "log_sink.h"
#include "runtime_paths.h"
#include "race_route_config.h"
#include "race_rules.h"
#include "race_course_native.h"
#include "race_circuit_guidance.h"
#include "race_native_follower.h"
#include "race_far_handoff.h"
#include "race_trial_spectator.h"
#include "race_live_overtake_native.h"
#include "race_pace_plan.h"
#include "race_catchup.h"
#include "race_terrain_line.h"
#include "race_following.h"
#include "race_boundary_warning.h"
#include "race_help.h"
#include "race_elimination_notice.h"
#include "race_native_far_probe.h"
#include "race_localization.h"
#include "race_wager.h"
#include "horse_ledger_runtime.h"
#include "race_transaction_feed.h"
#include "horse_attribute_backup.h"
#include "horse_force_trial.h"
#include "marybeth_clips.h"
#include "player_race_voice.h"
#include "sadie_clips.h"
#include "charles_clips.h"
#include "gang_clips.h"
#include "tilly_clips.h"
#include "javier_clips.h"
#include "bill_clips.h"
#include "race_dialogue_test.h"
#include "rider_outfits.h"
#include "fixed_horse_pool.h"
#include "race_host_horses.h"
#include "horse_catalog_runtime.h"
#include "fixed_rider_models.h"
#include "race_horse_compat.h"
#include "horse_story_support.h"
#include "horse_inspection.h"
#include "race_horse_inspection.h"
#include "race_opponent_difficulty.h"
#include "race_corridor_override.h"
#include "race_tracking_margin_config.h"
#include "race_corridor_overlay.h"
#include "data/race_bgm_catalogue.h"
#include "race_bgm_native_audio.h"
#include "race_bgm_fade.h"
#include "race_bgm_fade_audio.h"
#include "race_bgm_volume.h"
#include "native_horse_race_reward.h"
#include "native_horse_race_admission.h"
#include "native_horse_race_reasons.h"
#include "race_bgm_native_music.h"
#include "race_bgm_native_music_diagnostics.h"
#include "race_bgm_music_output.h"

#include <windows.h>

#include <main.h>
#include "game_natives.h"
#include "game_clock.h"
#include "race_firearm_guard.h"
#include "race_appearance.h"
#include "race_circuit_scene.h"
#include "race_circuit_start.h"
#include "race_starter.h"
#include "race_field.h"
#include "race_host_boost.h"
#include "race_bookmaker_blips.h"
#include "race_obstacle_learning.h"
#include "race_line_pins.h"
#include "race_recorded_reference.h"
#include "race_collision_learner.h"
#include "race_passage_learner.h"
#include "race_obstacle_map.h"
#include "race_obstacle_profiles.h"
#include "race_obstacle_survey.h"
#include "race_obstacle_storage.h"
#include "race_obstacle_snapshot.h"
#include "race_native_corridor.h"
#include "telemetry_retention.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace horse_races {
namespace {
using Clock=horse_time::GameClock;

#include "race_runtime/encounter_definitions.inl"
#include "race_runtime/encounter_progress.inl"
#include "race_runtime/driving_state.inl"
#include "race_runtime/course_evidence.inl"
#include "race_runtime/station_state.inl"
#include "race_runtime/labels_and_config.inl"

#include "race_runtime/horse_attributes.inl"
#include "race_runtime/horse_roster.inl"
#include "race_runtime/speech_and_prompts.inl"
#include "race_runtime/blip_icons.inl"
#include "race_runtime/native_encounter_options.inl"
#include "race_runtime/guidance.inl"
#include "race_runtime/racing_line.inl"
#include "race_runtime/traffic_and_entities.inl"

#include "race_runtime/lifecycle.inl"
#include "race_runtime/race_result.inl"

#include "race_runtime/route_loading.inl"

#include "race_runtime/actor_loading.inl"
#include "race_runtime/host_approach.inl"

#include "race_runtime/native_navigation.inl"
#include "race_runtime/race_start.inl"

#include "race_runtime/staging.inl"

#include "race_runtime/invitation.inl"

#include "race_runtime/race_progress.inl"
#include "race_runtime/online_survey.inl"
#include "race_runtime/far_riders.inl"
#include "race_runtime/racing.inl"

#include "race_runtime/result_scene.inl"

#include "race_runtime/scanning.inl"

} // namespace

#include "race_runtime/configuration_api.inl"
#include "race_runtime/frame_api.inl"

#include "race_runtime/camp_api.inl"

#include "race_runtime/motion_telemetry.inl"

#include "race_runtime/line_updates.inl"
#include "race_runtime/navigation_telemetry.inl"
#include "race_runtime/corridor_api.inl"

} // namespace horse_races
