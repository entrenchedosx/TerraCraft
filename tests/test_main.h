#pragma once

/* Minimal test harness: counts, assertion macro, and test declarations.
 * Each test returns the number of failures (0 = pass).
 */

#include <stdio.h>

/* Global counters (defined in test_main.c). */
extern int g_tests_run;
extern int g_tests_failed;

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
int test_physics_water_motion(void);
int test_physics_spawn(void);
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
int test_esave_corrupt(void);
int test_esave_missing(void);
int test_esave_transient(void);

/* Crafting/durability/food tests (defined in test_craft.c). */
int test_recipe_registry(void);
int test_recipe_shapeless(void);
int test_recipe_shaped(void);
int test_recipe_bench(void);
int test_recipe_consume(void);
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
int test_mob_strike_pitch(void);

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

/* LAN transport tests (defined in test_lan.c). */
int test_lan_loopback_transport(void);
int test_lan_local_address_filter(void);

/* Atlas tile tests (defined in test_texture.c). */
int test_atlas_mcfaces(void);
int test_atlas_apply_dir(void);
int test_atlas_resource_pack_paths(void);
