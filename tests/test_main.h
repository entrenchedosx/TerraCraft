#pragma once
int test_visual_lightmap_and_fog(void);
int test_visual_biome_tints(void);
int test_texture_mipmap_edges(void);
int test_visual_leaves_and_timeline(void);

/* Minimal test harness: counts, assertion macro, and test declarations.
 * Each test returns the number of failures (0 = pass).
 */

#include <stdio.h>

/* Global counters (defined in test_main.c). */
extern int g_tests_run;
extern int g_tests_failed;
int test_voxel_light_sky_and_cave(void);
int test_voxel_light_torch_and_removal(void);
int test_voxel_light_seams_and_invalidation(void);
int test_ui_pointer_drawable_scaling(void);
int test_ui_scale_layout(void);
int test_ui_nine_slice_geometry(void);
int test_ui_focus_navigation(void);
int test_ui_control_capture(void);
int test_chat_layout_wrapping(void);
int test_chat_layout_tail(void);
int test_chat_layout_bounds(void);
int test_launch_options(void);
int test_menu_native_bmp(void);
int test_menu_asset_file_fallback(void);
int test_worldgen_v5_determinism(void);
int test_worldgen_v5_fields(void);
int test_worldgen_v5_spawn(void);
int test_worldgen_v5_save_compatibility(void);
int test_worldgen_v5_ore_depth(void);
int test_worldgen_v4_frozen_hashes(void);
int test_worldgen_v4_determinism(void);
int test_worldgen_v4_fields(void);
int test_worldgen_v4_spawn(void);
int test_worldgen_v4_materials(void);
int test_worldgen_v4_rescue_save(void);
int test_worldgen_v4_ore_depth(void);

/* Assertion: on failure, prints location and increments failure count.
 * Must be used inside a function returning int `failures`.
 */
#define TEST_ASSERT(cond)                                                                                         \
    do {                                                                                                          \
        if (!(cond)) {                                                                                            \
            printf("[FAIL] %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                                           \
        }                                                                                                         \
    } while (0)

/* Float comparison with epsilon. */
#define TEST_ASSERT_FLOAT_EQ(a, b, eps)                                                                            \
    do {                                                                                                          \
        float _a = (a);                                                                                           \
        float _b = (b);                                                                                           \
        float _d = _a - _b;                                                                                       \
        if (_d < 0.0f) {                                                                                          \
            _d = -_d;                                                                                             \
        }                                                                                                         \
        if (!(_d <= (eps))) {                                                                                     \
            printf("[FAIL] %s:%d: floats differ: %f vs %f (eps %f)\n", __FILE__, __LINE__, (double)_a, (double)_b,  \
                   (double)(eps));                                                                                \
            failures++;                                                                                           \
        }                                                                                                         \
    } while (0)

/* Run a single test function, updating global counters. Flushes so a
 * crash always leaves the last completed test visible in the log. */
#define RUN_TEST(fn)                                                                                              \
    do {                                                                                                          \
        int _f = fn();                                                                                            \
        g_tests_run++;                                                                                            \
        if (_f != 0) {                                                                                            \
            g_tests_failed++;                                                                                     \
            printf("[FAIL] %s: %d failure(s)\n", #fn, _f);                                                        \
        } else {                                                                                                  \
            printf("[PASS] %s\n", #fn);                                                                           \
        }                                                                                                         \
        fflush(stdout);                                                                                           \
    } while (0)

/* Test declarations (defined in test_core.c). */
int test_vec3_add(void);
int test_vec3_ops(void);
int test_mat4_identity(void);
int test_mat4_ortho_ydown(void);
int test_block_table(void);
int test_chunk_basic(void);
int test_arena(void);
int test_process_working_set_metric(void);
int test_path_dirs(void);
int test_camera_viewmodel_fov_compensation(void);
int test_player_viewmodel_arm_pose(void);

/* World tests (defined in test_world.c). */
int test_chunk_index(void);
int test_block_ids(void);
int test_world_gen(void);
int test_world_gen_determinism(void);
int test_water_simulation(void);
int test_water_queue_bound(void);
int test_gravity_blocks_fall_land_and_save_settle(void);
int test_gravity_support_chain_and_water(void);
int test_gravity_queue_bound_recovery(void);
int test_gravity_retries_when_lan_queue_is_full(void);
int test_gravity_lan_prediction_and_authority(void);
int test_gravity_lan_pool_full_start_clears_source(void);
int test_world_chunk_compaction_and_seam_dirtying(void);
int test_ao_diagonal_chunk_lifecycle(void);
int test_chunk_water_version_compat(void);
int test_mesher_water_height(void);

/* Mesher tests (defined in test_mesher.c). */
int test_mesher_empty(void);
int test_mesher_single_block(void);
int test_mesher_two_adjacent(void);
int test_mesher_hidden_faces(void);
int test_mesher_cross_sprite(void);
int test_mesher_torch_model(void);

/* Atlas tests (defined in test_texture.c). */
int test_atlas_layout(void);
int test_atlas_tile_mapping(void);
int test_atlas_pixels(void);

/* Streamer/hashmap/world-gen tests (defined in test_streamer.c). */
int test_hashmap_basic(void);
int test_world_gen_chunk(void);
int test_streamer_load_unload(void);
int test_streamer_determinism(void);

/* Physics/interaction tests (defined in test_physics.c). */
int test_physics_fall_land(void);
int test_physics_high_drop(void);
int test_physics_wall_slide(void);
int test_physics_jump(void);
int test_physics_creative_flight(void);
int test_physics_acceleration(void);
int test_physics_water_motion(void);
int test_physics_water_contact(void);
int test_physics_water_step_up(void);
int test_physics_water_escape_deep(void);
int test_physics_water_fall_cushion(void);
int test_physics_spawn(void);
int test_physics_step_up_and_eject(void);
int test_physics_auto_jump_off(void);
int test_physics_sneak_edge(void);
int test_physics_sneak_posture(void);
int test_physics_sneak_edge_corner(void);
int test_player_view_bob(void);
int test_interaction_break_place(void);

/* Raycast tests (defined in test_raycast.c). */
int test_raycast_down(void);
int test_raycast_wall(void);
int test_raycast_miss(void);
int test_raycast_camera(void);

/* Time-of-day tests (defined in test_time.c). */
int test_time_phases(void);
int test_time_sun(void);
int test_time_wrap(void);

/* Fixed-rate simulation scheduler tests (defined in test_simulation_clock.c). */
int test_simulation_clock_render_rates(void);
int test_simulation_clock_catch_up_and_remainder(void);
int test_simulation_clock_invalid_elapsed(void);
int test_input_buffer_press_release_pulse(void);
int test_input_buffer_held_and_repeat(void);
int test_input_buffer_clear_and_focus_loss(void);

/* AO mesher tests (defined in test_mesher_ao.c). */
int test_ao_levels(void);
int test_ao_flat(void);
int test_ao_corner(void);
int test_ao_seam(void);
int test_ao_sun_shade(void);

/* Item/inventory tests (defined in test_item.c). */
int test_item_registry(void);
int test_item_stackability_contract(void);
int test_item_texture_mapping(void);
int test_stack_ops(void);
int test_inventory_ops(void);
int test_inventory_sanitize(void);

/* Save/metadata tests (defined in test_save.c). */
int test_meta_roundtrip(void);
int test_session_world_sort(void);
int test_meta_corrupt(void);
int test_chunk_roundtrip(void);
int test_chunk_corrupt(void);
int test_sanitize(void);
int test_world_delete(void);
int test_session_persist(void);

/* Biome/terrain tests (defined in test_biome.c). */
int test_biome_determinism(void);
int test_biome_coverage(void);
int test_world_gen_terrain_v2(void);
int test_world_gen_terrain_v3(void);
int test_tree_determinism(void);
int test_vegetation(void);
int test_caves(void);
int test_ores(void);

/* Settings/seed/bmp tests (defined in test_settings.c). */
int test_settings_defaults(void);
int test_settings_parse(void);
int test_settings_roundtrip(void);
int test_seed_parse(void);
int test_bmp_parse(void);

/* Local profile tests (defined in test_profile.c). */
int test_profile_name_validation(void);
int test_profile_generated_name(void);
int test_profile_storage(void);

/* UI/state/font tests (defined in test_ui.c). */
int test_game_states(void);
int test_font_basic(void);
int test_ui_button(void);
int test_ui_text_field(void);
int test_ui_slider(void);
int test_ui_icons(void);
int test_hud_vitals_icons(void);

/* Survival/entity/persistence tests (defined in test_gameplay.c). */
int test_survival_mine_time(void);
int test_tool_tier_progression(void);
int test_survival_drops(void);
int test_survival_mining(void);
int test_survival_damage_heal(void);
int test_survival_fall(void);
int test_survival_hunger(void);
int test_survival_respawn(void);
int test_entity_pickup(void);
int test_entity_lifetime(void);
int test_save_m6_roundtrip(void);
int test_save_m5_migration(void);
int test_raycast_plants_water(void);
int test_chunk_save_dirty_semantics(void);
int test_save_fresh_world_meta(void);
int test_meta_partial_and_strict(void);
int test_meta_full_inventory(void);
int test_mesher_glass_light(void);

/* Entity-save tests (defined in test_esave.c). */
int test_esave_single(void);
int test_esave_multiple(void);
int test_entity_drop_stack(void);
int test_esave_corrupt(void);
int test_esave_missing(void);
int test_esave_transient(void);
int test_entity_merge_fallback(void);

/* Crafting/durability/food tests (defined in test_craft.c). */
int test_recipe_registry(void);
int test_recipe_shapeless(void);
int test_recipe_shaped(void);
int test_recipe_bench(void);
int test_recipe_consume(void);
int test_recipe_refined_tiers(void);
int test_durability_use(void);
int test_durability_merge(void);
int test_durability_save(void);
int test_food_eat(void);
int test_exhaustion(void);
int test_leaf_bonus(void);

/* Particle/audio tests (defined in test_fx.c). */
int test_particle_pool(void);
int test_particle_sim(void);
int test_audio_basics(void);
int test_audio_wav(void);
int test_audio_dummy(void);
int test_audio_pack_swap(void);
int test_audio_material(void);

/* Mob tests (defined in test_mob.c). */
int test_mob_handles(void);
int test_mob_physics_fall(void);
int test_mob_physics_wall_step(void);
int test_mob_definitions(void);
int test_mob_ai_passive(void);
int test_mob_ai_hostile(void);
int test_mob_damage(void);
int test_mob_fall_events(void);
int test_mob_raycast(void);
int test_mob_tool_stats(void);
int test_mob_spawn_passive(void);
int test_mob_spawn_hostile(void);
int test_mob_spawn_rules(void);
int test_save_mob_roundtrip(void);
int test_save_mob_v1_compat(void);
int test_save_mob_v2_compat(void);
int test_save_mob_write_failure_preserves(void);
int test_save_mob_corrupt(void);
int test_save_mob_dead_excluded(void);
int test_mob_models(void);
int test_player_swing_animation(void);
int test_player_anim_states(void);
int test_player_body_model(void);
int test_player_body_pose(void);
int test_player_body_pose_vanilla(void);
int test_mob_strike_pitch(void);
int test_mob_water_cushion(void);

/* Pathfinding tests (defined in test_path.c). */
int test_path_flat(void);
int test_path_wall(void);
int test_path_steps(void);
int test_path_unreachable(void);
int test_path_negative_deterministic(void);

/* Projectile/bow/skeleton tests (defined in test_projectile.c). */
int test_projectile_basics(void);
int test_projectile_flight(void);
int test_projectile_block_swept(void);
int test_projectile_ordering(void);
int test_projectile_owner(void);
int test_projectile_entities(void);
int test_projectile_damage(void);
int test_bow_policy(void);
int test_bow_slot_switch(void);
int test_skeleton_ai(void);
int test_skeleton_drops_save(void);
int test_projectile_remote_hit(void);
int test_projectile_remote_blocked(void);
int test_projectile_remote_miss(void);
int test_projectile_remote_dead_creative(void);
int test_projectile_remote_mob_owner(void);

/* LAN transport tests (defined in test_lan.c). */
int test_lan_loopback_transport(void);
int test_lan_local_address_filter(void);
int test_lan_discover_codec(void);
int test_lan_discover_scan(void);
int test_lan_pvp_melee_handlers(void);
int test_lan_pvp_death_respawn(void);
int test_lan_version_reject(void);
int test_lan_pvp_ranged_handlers(void);
int test_lan_pvp_creative_handlers(void);
int test_lan_rules_editing(void);
int test_lan_late_join_snapshot(void);
int test_lan_remote_interpolation(void);
int test_lan_replication_handlers(void);

/* PvP combat tests (defined in test_pvp.c). */
int test_pvp_hit_in_range(void);
int test_pvp_miss_beyond_range(void);
int test_pvp_miss_wrong_direction(void);
int test_pvp_sneak_hitbox(void);
int test_pvp_validate_strike(void);
int test_pvp_seq_accept(void);
int test_pvp_clamp_damage(void);
int test_pvp_clamp_arrow(void);
int test_pvp_lerp_angle(void);

/* Game-rule / cheats tests (defined in test_gamerules.c). */
int test_gamerules_defaults(void);
int test_gamerules_keep_inventory(void);
int test_gamerules_meta_roundtrip(void);
int test_gamerules_meta_missing_defaults(void);
int test_gamerules_session_create(void);
int test_gamerules_lan_framing(void);
int test_gamerules_codecs(void);
int test_death_gate_states(void);
int test_hand_golden_arm_matrices(void);
int test_hand_golden_item_matrices(void);
int test_hand_golden_eat_and_bob(void);
int test_hand_swing_timeline(void);
int test_hand_swing_retrigger_rule(void);
int test_hand_equip_transition(void);
int test_hand_attack_cooldown_dip(void);
int test_hand_tick_rate_independence(void);
int test_hand_walk_bob_state(void);
int test_hand_yaw_wrap_does_not_spike_sway(void);
int test_hand_cube_matches_modelpart(void);
int test_hand_rest_pose_screen_placement(void);
int test_worldgen_parity_primitives(void);
int test_worldgen_parity_known_values(void);
int test_vphys_walk_sprint_sneak_speeds(void);
int test_vphys_jump_apex_and_air(void);
int test_vphys_fall_distance_and_landing(void);
int test_vphys_water_sinking_and_exit(void);
int test_vphys_sneak_edge_and_collision(void);
int test_vphys_diagonal_input_matches_vanilla_scaling(void);
int test_vphys_mth_trig_table(void);
int test_menu_background_granularity(void);

/* Atlas tile tests (defined in test_texture.c). */
int test_atlas_mcfaces(void);
int test_atlas_apply_dir(void);
int test_atlas_resource_pack_paths(void);
int test_atlas_mcassets_coverage(void);
