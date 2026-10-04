#pragma once

/* High-level renderer (M5): textured voxel shader + per-chunk GPU buffers
 * in two passes (opaque, then blended transparent sorted back-to-front at
 * chunk level), plus a flat UI pass (HUD, menus, text).
 * Owns the viewport, shader programs, texture atlas, and GpuChunkBuffers.
 * Requires a current GL context for creation, upload, and drawing.
 * The clear color is driven by the day/night sky each frame (no static sky).
 *
 * Transparency: PASS 1 draws opaque + alpha-cutout geometry with depth
 * writes on; PASS 2 draws blended water/glass with depth writes off,
 * sorted far-to-near per chunk. Within-chunk face order is still mesher
 * order (documented limitation). See docs/DECISIONS.md.
 */

/* Render passes (index into the per-slot buffer pair). */
#define RENDERER_PASS_OPAQUE 0
#define RENDERER_PASS_TRANSPARENT 1
#define RENDERER_PASSES 2

/* Exponential fog density (1/m): R=4 fades distant chunks into the sky. */
#define RENDERER_FOG_DENSITY 0.014f

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations. */
typedef struct GlContext GlContext;
typedef struct World World;
typedef struct Camera Camera;
typedef struct TimeSystem TimeSystem;
typedef struct ItemStack ItemStack;
typedef struct EntityPool EntityPool;
typedef struct ParticlePool ParticlePool;
typedef struct IconBatch IconBatch;
typedef struct MobPool MobPool;
typedef struct ProjectilePool ProjectilePool;
typedef struct Player Player;

/* Opaque renderer handle. */
typedef struct Renderer Renderer;

/* Per-frame performance snapshot ( timings in milliseconds ). */
typedef struct RendererPerf {
    double mesh_ms; /* CPU meshing during the last refresh. */
    double upload_ms; /* GPU upload during the last refresh. */
    double draw_ms; /* Last draw-World time (clear + uniforms + draws). */
    size_t drawn; /* Opaque chunks drawn in the last draw. */
    size_t drawn_t; /* Transparent chunks drawn in the last draw. */
    size_t culled; /* Chunks skipped by frustum culling in the last draw. */
} RendererPerf;

/* Create a renderer bound to an existing GL context.
 * Compiles the embedded textured voxel shader, generates the procedural
 * texture atlas, enables depth test + back-face culling + alpha blending.
 * Degrades to clear-only rendering (with loud logs) when the shader or
 * atlas is unavailable.
 *
 * Args:
 *   gl: GL context (must not be NULL, must stay alive while renderer lives).
 *
 * Returns: owned Renderer on success, NULL on OOM (never NULL for GL issues).
 */
Renderer *renderer_create(GlContext *gl);

/* Destroy a renderer and all GPU buffers + shader + atlas. NULL-safe.
 * Does not destroy the GL context.
 *
 * Args:
 *   r: renderer to destroy (may be NULL).
 */
void renderer_destroy(Renderer *r);

/* Set the clear color (defaults to sky blue #87CEEB at creation).
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   r_, g, b, a: normalized color components.
 */
void renderer_set_clear_color(Renderer *r, float r_, float g, float b, float a);

/* Set the viewport in pixels. Safe with 0/negative (ignored with warning).
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   width, height: drawable size in pixels.
 */
void renderer_set_viewport(Renderer *r, int width, int height);

/* Clear color + depth buffers with the current clear color.
 *
 * Args:
 *   r: renderer (must not be NULL).
 */
void renderer_clear(Renderer *r);

/* Rebuild GPU meshes for dirty chunks (mesher + upload) with phase timing.
 * Chunks already up to date are skipped. Logs per-chunk mesh stats.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   w: world (must not be NULL).
 */
void renderer_refresh_world(Renderer *r, World *w);

/* Delete GPU buffers whose chunks are no longer in the world (streaming
 * unloads). Must be called after streamer updates, before/after refresh.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   w: world (must not be NULL).
 */
void renderer_prune_world(Renderer *r, const World *w);

/* Draw the whole world in two passes (opaque, then blended transparent
 * sorted far-to-near per chunk) under the current time of day. Sets the
 * clear color to the sky color, uploads lighting uniforms, then draws each
 * valid chunk buffer whose AABB passes frustum culling.
 * A NULL time system falls back to fixed noon lighting (tests/headless).
 * Records draw timing and drawn/culled counts for renderer_get_perf.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   w: world (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL for fixed noon lighting).
 */
void renderer_draw_world(Renderer *r, const World *w, const Camera *cam, float aspect, const TimeSystem *ts);

/* Drop every chunk GPU buffer (both passes) without touching the world.
 * Used when closing a world session so no stale GL objects survive.
 *
 * Args:
 *   r: renderer (must not be NULL).
 */
void renderer_drop_all(Renderer *r);

/* Replace the atlas texture with a resource pack (or procedural "Default").
 * Requires the current GL context. Failures keep the old texture.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   pack: pack directory name ("Default"/NULL/empty = procedural).
 *
 * Returns: 0 on success, non-zero when the new texture failed (old kept).
 */
int renderer_reload_atlas(Renderer *r, const char *pack);

/* Number of chunk buffers currently holding drawable meshes.
 *
 * Args:
 *   r: renderer (may be NULL).
 *
 * Returns: drawable count.
 */
size_t renderer_drawable_chunks(const Renderer *r);

/* Last-frame performance snapshot (all zeros on NULL).
 *
 * Args:
 *   r: renderer (may be NULL).
 *
 * Returns: copy of the perf counters.
 */
RendererPerf renderer_get_perf(const Renderer *r);

/* Draw the 2D HUD overlay (crosshair + hotbar with item icons and stack
 * counts, durability bars, eat progress, damage flash, plus survival
 * vitals when requested) after the 3D scene.
 * No-op when the UI pipeline is unavailable or args are invalid.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   hotbar: 9 ItemStacks, slots 0..8 (must not be NULL).
 *   hotbar_sel: selected slot index.
 *   health, max_health, hunger, max_hunger: vitals (ignored unless shown).
 *   show_vitals: true to draw health/hunger bars (survival HUD).
 *   eat_frac: eating progress 0..1 (< 0 hides the bar).
 *   bow_frac: bow draw charge 0..1 (< 0 hides the bar).
 *   hurt_flash: damage flash strength 0..1 (0 hides).
 */
void renderer_draw_hud(Renderer *r, int width, int height, const ItemStack *hotbar, int hotbar_sel,
                       float health, float max_health, float hunger, float max_hunger, bool show_vitals,
                       float eat_frac, float bow_frac, float hurt_flash);

/* Draw active item entities as small textured cubes (one transient upload
 * per call; axis-aligned with a gentle bob, no rotation). Frustum culling
 * is skipped (bounded pool, tiny geometry). No-op on bad args.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   pool: entity pool (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL for fixed noon lighting).
 */
void renderer_draw_entities(Renderer *r, const EntityPool *pool, const Camera *cam, float aspect,
                            const TimeSystem *ts);

/* Draw active particles as small shrinking textured cubes (one transient
 * upload per call from renderer-owned scratch: no per-frame heap churn).
 * No-op on bad args or missing scratch/pipe.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   pool: particle pool (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL for fixed noon lighting).
 */
void renderer_draw_particles(Renderer *r, const ParticlePool *pool, const Camera *cam, float aspect,
                             const TimeSystem *ts);

/* Draw living mobs as articulated blocky models (one transient upload per
 * call from renderer-owned scratch: no per-frame heap churn). Parts
 * animate procedurally (leg swing, head bob, hurt shake, death fall);
 * frustum culled per mob. Drawn in the opaque family (depth-tested,
 * cutout discard). No-op on bad args or missing scratch/pipe.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   pool: living-mob pool (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL for fixed noon lighting).
 *   anim_time: seconds for idle motion (any epoch).
 *   out_drawn/out_culled: receive per-mob counts (each may be NULL).
 */
void renderer_draw_mobs(Renderer *r, const MobPool *pool, const Camera *cam, float aspect,
                        const TimeSystem *ts, float anim_time, int *out_drawn, int *out_culled);

/* Draw live projectiles as small oriented shafts (one transient upload
 * per call from renderer-owned scratch: no per-frame heap churn).
 * Shaft + head boxes align with velocity (flying) or the retained
 * embed orientation; frustum culled per arrow. Opaque, depth-tested.
 * No-op on bad args or missing scratch/pipe.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   pool: projectile pool (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL for fixed noon lighting).
 *   out_drawn: receives arrows drawn (may be NULL).
 */
void renderer_draw_projectiles(Renderer *r, const ProjectilePool *pool, const Camera *cam,
                               float aspect, const TimeSystem *ts, int *out_drawn);

/* Draw a crack-stage overlay cube on a block being mined (1.002 scale,
 * depth-tested, tile selected from progress). No-op on bad args or when
 * progress is outside [0,1).
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   cam: camera (must not be NULL).
 *   aspect: viewport width/height (> 0).
 *   ts: time of day (may be NULL).
 *   bx, by, bz: target block cell.
 *   progress: mining fraction 0..1 (stage = floor(progress*5), clamped 0..4).
 */
void renderer_draw_block_overlay(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts, int bx,
                                 int by, int bz, float progress);

/* Draw raw colored UI quads (HUD-vertex format: x,y,r,g,b,a) fullscreen.
 * Depth-tested off during the pass. No-op on bad args or missing UI pipe.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   verts: HUD-format vertices, 6 floats each (must not be NULL).
 *   count: vertex count (multiple of 3 expected).
 */
void renderer_draw_rects(Renderer *r, int width, int height, const float *verts, size_t count);

/* Draw textured item icons (atlas tiles) as 2D quads: depth off, blend
 * on, alpha cutout at 0.05. Empty batches draw nothing. No-op on bad
 * args or missing icon pipe/atlas.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   b: icon batch (must not be NULL).
 */
void renderer_draw_item_icons(Renderer *r, int width, int height, const IconBatch *b);

/* Draw a text string with the bitmap font (y-down, top-left origin).
 * Silently truncates past 160 chars. No-op on bad args or missing UI pipe.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   x, y: top-left origin in pixels.
 *   scale: pixel scale per font pixel (> 0).
 *   cr, cg, cb, ca: text color.
 *   text: NUL-terminated string (must not be NULL).
 */
void renderer_draw_text(Renderer *r, float x, float y, float scale, float cr, float cg, float cb, float ca,
                        const char *text);

/* Measure a text string in pixels (matches renderer_draw_text layout).
 *
 * Args:
 *   text: string (NULL measures empty).
 *   scale: pixel scale (> 0).
 *   out_w/out_h: receive extents (each may be NULL).
 */
void renderer_measure_text(const char *text, float scale, float *out_w, float *out_h);
