#include "render/renderer.h"
#include "core/log.h"
#include "core/time.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/mob.h"
#include "game/mob_model.h"
#include "game/particle.h"
#include "game/projectile.h"
#include "game/time_system.h"
#include "platform/gl_ctx.h"
#include "render/camera.h"
#include "render/font.h"
#include "render/mesher.h"
#include "render/shader.h"
#include "render/texture_atlas.h"
#include "render/vao_manager.h"
#include "ui/hud.h"
#include "world/chunk.h"
#include "world/world.h"

/* Portable GL system header for clear/viewport/enable/blend tokens.
 * NOTE (Windows/MSVC): <windows.h> must come first for WINGDIAPI. */
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Particle scratch capacity: worst case every pool slot alive. */
#define PARTICLE_SCRATCH_VERTS ((size_t)PARTICLE_MAX * 36)
/* Mob scratch capacity: every slot alive with the largest model
 * (6 parts x 36 triangle-soup verts; identity indices apply). */
#define MOB_SCRATCH_VERTS ((size_t)MOB_MAX * 6 * 36)
#define MOB_SCRATCH_IDX ((size_t)MOB_MAX * 6 * 36)

/* Concrete renderer type. */
struct Renderer {
    GlContext *gl; /* Non-owning GL context. */
    float clear_r, clear_g, clear_b, clear_a;
    int vp_width, vp_height;
    Shader *shader; /* Owned textured voxel program (NULL when GL limited). */
    unsigned int atlas; /* Owned GL atlas texture (0 when unavailable). */
    int mvp_loc; /* Uniform locations (-1 when unavailable). */
    int atlas_loc;
    int sun_dir_loc;
    int light_intensity_loc;
    int sky_color_loc;
    int fog_color_loc;
    int fog_density_loc;
    int cam_pos_loc;
    Shader *ui_shader; /* Owned flat UI program (NULL when GL limited). */
    unsigned int ui_vao; /* UI quad array (0 when unavailable). */
    unsigned int ui_vbo; /* UI quad buffer (dynamic, re-uploaded per frame). */
    int ui_ortho_loc; /* uOrtho location (-1 when unavailable). */
    Shader *ui_tex_shader; /* Owned textured-icon program (NULL when limited). */
    unsigned int ui_tex_vao; /* Icon quad array (0 when unavailable). */
    unsigned int ui_tex_vbo; /* Icon quad buffer (dynamic, per frame). */
    int ui_tex_ortho_loc; /* uOrtho location (-1 when unavailable). */
    int ui_tex_atlas_loc; /* uAtlas location (-1 when unavailable). */
    GpuChunkBuffer gpu[WORLD_MAX_CHUNKS][RENDERER_PASSES]; /* Per-slot opaque/transparent pair. */
    GpuChunkBuffer ent_buf; /* Scratch buffer for entities/overlay (re-uploaded). */
    float *part_verts; /* Owned particle vertex scratch (NULL when OOM). */
    unsigned int *part_idx; /* Owned identity indices for part_verts. */
    float *mob_verts; /* Owned mob vertex scratch (NULL when OOM). */
    unsigned int *mob_idx; /* Owned identity indices for mob_verts. */
    unsigned int mob_skin_tex[3]; /* Per-model skin GL textures (0 = tile path). */
    RendererPerf perf; /* Last-frame counters. */
    char atlas_pack[64]; /* Active pack name ("Default" = procedural). */
};

/* Find the GPU slot for chunk (cx,cz), or a free slot. Either pass being
 * live counts as occupied; both passes share the slot index.
 * Returns index or -1 when full (and no match).
 */
static int renderer_find_slot(Renderer *r, int cx, int cz)
{
    int free_slot = -1;
    for (int i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        bool live = r->gpu[i][0].vao != 0 || r->gpu[i][0].valid || r->gpu[i][1].vao != 0 ||
                    r->gpu[i][1].valid;
        if (live) {
            if (r->gpu[i][0].cx == cx && r->gpu[i][0].cz == cz) {
                return i;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    return free_slot;
}

/* Mob part face layout: unit-cube corner indices (x + 2y + 4z) per face,
 * in the mesher's A/B/C/D order with the same (0,1,2,2,1,3) pattern, so
 * winding matches the world pipeline exactly (CCW front under culling).
 * (Forward declarations: the voxel pass helpers live further below.)
 */
static bool renderer_begin_voxel(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts);
static bool renderer_begin_voxel_tex(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts,
                                     unsigned int tex);
static void renderer_end_voxel(Renderer *r);
static const int MOB_FACE_IDX[6][4] = {
    {0, 4, 2, 6}, /* -X */
    {5, 1, 7, 3}, /* +X */
    {0, 1, 4, 5}, /* -Y */
    {6, 7, 2, 3}, /* +Y */
    {1, 0, 3, 2}, /* -Z */
    {4, 5, 6, 7}, /* +Z */
};
static const float MOB_FACE_N[6][3] = {
    {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
};
static const float MOB_FACE_U[4] = {0.0f, 1.0f, 0.0f, 1.0f};
static const float MOB_FACE_V[4] = {0.0f, 0.0f, 1.0f, 1.0f};

/* Rotate a point around the X axis (pitch). */
static Vec3 mob_rot_x(Vec3 p, float a)
{
    float c = cosf(a);
    float s = sinf(a);
    return mmath_vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
}

/* Rotate a point around the Y axis (yaw+PI so model +Z faces forward). */
static Vec3 mob_rot_y(Vec3 p, float a)
{
    float c = cosf(a);
    float s = sinf(a);
    return mmath_vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

/* Emit one rotated cuboid part (6 faces x 2 triangles in soup order,
 * matching entity_emit_cube so identity indices apply). Transform: pitch
 * about the part pivot first, then yaw+PI, then translate to mob feet.
 * Normals rotate identically (no translation). UVs are per-face
 * (fuv[f][0..3] = u0,v0,u1,v1) so skins map each face to its own art
 * region while tile-path parts repeat one rect on all faces.
 *
 * Args:
 *   dst: 36-vert destination (must hold 36*9 floats).
 *   mob_pos: mob feet position (world).
 *   yaw: mob facing (radians).
 *   part: model part.
 *   pitch: part pitch (radians, procedural animation).
 *   pivot_override: when true, rotate about the mob feet origin instead
 *     of the part pivot (death fall-over).
 *   fuv: per-face UV rects (must not be NULL).
 */
static void mob_emit_part(float *dst, Vec3 mob_pos, float yaw, const MobModelPart *part, float pitch,
                          bool pivot_override, const float fuv[6][4])
{
    Vec3 pivot;
    if (pivot_override) {
        pivot = mmath_vec3(0.0f, 0.0f, 0.0f);
    } else {
        pivot = mmath_vec3(part->offset.x + part->size.x * 0.5f, part->pivot_y,
                           part->offset.z + part->size.z * 0.5f);
    }
    float ya = yaw + 3.14159265f;
    /* Transform the 8 unit corners once. */
    Vec3 corners[8];
    Vec3 norms[6];
    for (int i = 0; i < 8; ++i) {
        float lx = (i & 1) ? 1.0f : 0.0f;
        float ly = (i & 2) ? 1.0f : 0.0f;
        float lz = (i & 4) ? 1.0f : 0.0f;
        Vec3 local = mmath_vec3(part->offset.x + lx * part->size.x - pivot.x,
                                part->offset.y + ly * part->size.y - pivot.y,
                                part->offset.z + lz * part->size.z - pivot.z);
        Vec3 pitched = mob_rot_x(local, pitch);
        Vec3 yawed = mob_rot_y(pitched, ya);
        corners[i] = mmath_vec3(mob_pos.x + yawed.x + pivot.x, mob_pos.y + yawed.y + pivot.y,
                                mob_pos.z + yawed.z + pivot.z);
    }
    for (int f = 0; f < 6; ++f) {
        Vec3 n = mob_rot_y(mob_rot_x(mmath_vec3(MOB_FACE_N[f][0], MOB_FACE_N[f][1], MOB_FACE_N[f][2]),
                                     pitch),
                           ya);
        norms[f] = n;
    }
    size_t n = 0;
    static const int TRIS[6] = {0, 1, 2, 2, 1, 3};
    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 6; ++k) {
            int q = TRIS[k];
            int ci = MOB_FACE_IDX[f][q];
            float *v = dst + n * MESHER_FLOATS_PER_VERTEX;
            v[0] = corners[ci].x;
            v[1] = corners[ci].y;
            v[2] = corners[ci].z;
            v[3] = norms[f].x;
            v[4] = norms[f].y;
            v[5] = norms[f].z;
            v[6] = (MOB_FACE_U[q] == 0.0f) ? fuv[f][0] : fuv[f][2];
            v[7] = (MOB_FACE_V[q] == 0.0f) ? fuv[f][1] : fuv[f][3];
            v[8] = 1.0f;
            n++;
        }
    }
}

/* Forward: mob batch helpers (defined below draw_mobs). */
static void mob_tile_uvs(const MobModelPart *part, float fuv[6][4]);
static void mob_skin_uvs(const MobSkin *skin, int part, float fuv[6][4]);
static size_t mob_emit_batch(Renderer *r, const MobPool *pool, const float planes[6][4],
                             float anim_time, int model_filter, const MobSkin *skin, size_t o,
                             int *drawn, int *culled);
static void mob_draw_batch(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts,
                           size_t o, unsigned int tex);

/* Draw living mobs as articulated blocky models (see header). */
void renderer_draw_mobs(Renderer *r, const MobPool *pool, const Camera *cam, float aspect,
                        const TimeSystem *ts, float anim_time, int *out_drawn, int *out_culled)
{
    if (out_drawn != NULL) {
        *out_drawn = 0;
    }
    if (out_culled != NULL) {
        *out_culled = 0;
    }
    if (r == NULL || pool == NULL || cam == NULL || r->mob_verts == NULL || r->mob_idx == NULL) {
        return;
    }
    float planes[6][4];
    camera_get_frustum_planes(cam, aspect, planes);
    int drawn = 0;
    int culled = 0;
    /* Tile pass (atlas): every mob without a live skin texture. */
    size_t o = mob_emit_batch(r, pool, planes, anim_time, -1, NULL, 0, &drawn, &culled);
    mob_draw_batch(r, cam, aspect, ts, o, r->atlas);
    /* Skin passes: one batch per skinned model with its own texture. */
    for (int mi = 0; mi < 3; ++mi) {
        if (r->mob_skin_tex[mi] == 0) {
            continue;
        }
        const MobSkin *skin = mob_skin_for(mi);
        const MobModel *model = mob_model_for(mi);
        if (skin == NULL || model == NULL || !mob_skin_validate(skin, model->nparts)) {
            continue;
        }
        size_t so = mob_emit_batch(r, pool, planes, anim_time, mi, skin, 0, &drawn, &culled);
        mob_draw_batch(r, cam, aspect, ts, so, r->mob_skin_tex[mi]);
    }
    if (out_drawn != NULL) {
        *out_drawn = drawn;
    }
    if (out_culled != NULL) {
        *out_culled = culled;
    }
}
/* Per-face UVs for one tile-path part (same atlas rect all faces). */
static void mob_tile_uvs(const MobModelPart *part, float fuv[6][4])
{
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texture_atlas_tile_uv(part->tile, &u0, &v0, &u1, &v1);
    for (int f = 0; f < 6; ++f) {
        fuv[f][0] = u0;
        fuv[f][1] = v0;
        fuv[f][2] = u1;
        fuv[f][3] = v1;
    }
}

/* Per-face UVs for one skin-path part (each face maps its own art
 * region; half-texel inset against bleed, skin space with v measured
 * from the GL bottom after the upload flip).
 */
static void mob_skin_uvs(const MobSkin *skin, int part, float fuv[6][4])
{
    float w = (float)skin->width;
    float h = (float)skin->height;
    for (int f = 0; f < 6; ++f) {
        MobSkinRect rc = skin->parts[part].faces[f];
        fuv[f][0] = ((float)rc.x + 0.5f) / w;
        fuv[f][1] = 1.0f - ((float)(rc.y + rc.h) - 0.5f) / h;
        fuv[f][2] = ((float)(rc.x + rc.w) - 0.5f) / w;
        fuv[f][3] = 1.0f - ((float)rc.y + 0.5f) / h;
    }
}

/* Emit one mob batch (all mobs of one texture path) into the scratch
 * buffer. Tile pass skips mobs that have a live skin texture; skin
 * passes take a single model index. Returns vertices emitted (o is the
 * running cursor, also used for the scratch-full bound).
 */
static size_t mob_emit_batch(Renderer *r, const MobPool *pool, const float planes[6][4],
                             float anim_time, int model_filter, const MobSkin *skin, size_t o,
                             int *drawn, int *culled)
{
    for (int i = 0; i < MOB_MAX; ++i) {
        const Mob *m = &pool->mobs[i];
        if (!m->active) {
            continue;
        }
        const MobDefinition *def = mob_definition(m->type);
        if (def->type == ENTITY_NONE) {
            continue;
        }
        const MobModel *model = mob_model_for(def->model);
        if (model == NULL || !mob_model_validate(model)) {
            continue;
        }
        bool skinned = skin != NULL && r->mob_skin_tex[def->model] != 0;
        if (model_filter < 0 && skinned) {
            continue; /* Tile pass skips live-skinned mobs. */
        }
        if (model_filter >= 0 && def->model != model_filter) {
            continue; /* Skin pass takes one model only. */
        }
        /* Frustum cull on the collision box (cheap, conservative). */
        float hw = m->width * 0.5f;
        Vec3 mn = mmath_vec3(m->pos.x - hw, m->pos.y, m->pos.z - hw);
        Vec3 mx = mmath_vec3(m->pos.x + hw, m->pos.y + m->height, m->pos.z + hw);
        if (!camera_aabb_visible(planes, mn, mx)) {
            ++(*culled);
            continue;
        }
        if ((o + (size_t)model->nparts * 36) * MESHER_FLOATS_PER_VERTEX >
            MOB_SCRATCH_VERTS * MESHER_FLOATS_PER_VERTEX) {
            break; /* Scratch full: remaining mobs skip a frame (bounded). */
        }
        /* Animation state (procedural, no per-frame allocation). */
        float hurt_shake = 0.0f;
        if (m->hurt_t > 0.0f) {
            float age = MOB_HURT_WINDOW - m->hurt_t;
            hurt_shake = sinf(age * 60.0f) * 0.05f;
        }
        float death_tilt = 0.0f;
        float sink = 0.0f;
        if (m->dead) {
            float k = m->dead_t / 0.4f;
            if (k > 1.0f) {
                k = 1.0f;
            }
            death_tilt = k * 1.5707963f;
            sink = m->dead_t < 0.6f ? m->dead_t * 0.3f : 0.18f;
        }
        float idle_bob = sinf(anim_time * 2.0f + (float)i) * 0.02f;
        Vec3 base =
            mmath_vec3(m->pos.x + hurt_shake, m->pos.y - sink + (m->grounded ? idle_bob : 0.0f),
                       m->pos.z);
        for (int p = 0; p < model->nparts; ++p) {
            const MobModelPart *part = &model->parts[p];
            float pitch = 0.0f;
            if (part->anim == MOB_ANIM_LEG) {
                float phase = m->walk_phase + ((p % 2) ? 3.14159265f : 0.0f);
                pitch = sinf(phase) * 0.6f;
            } else if (part->anim == MOB_ANIM_HEAD) {
                pitch = sinf(m->walk_phase * 0.5f) * 0.08f;
            } else if (part->anim == MOB_ANIM_AIM_ARM) {
                /* Bow arm: raised forward while aiming/firing, swings
                 * with the walk like any arm otherwise. */
                if (m->state == MOB_STATE_AIM || m->state == MOB_STATE_ATTACK) {
                    pitch = -1.3f;
                } else {
                    float phase = m->walk_phase + ((p % 2) ? 3.14159265f : 0.0f);
                    pitch = sinf(phase) * 0.6f;
                }
            }
            float fuv[6][4];
            if (skinned) {
                mob_skin_uvs(skin, p, fuv);
            } else {
                mob_tile_uvs(part, fuv);
            }
            mob_emit_part(r->mob_verts + o * MESHER_FLOATS_PER_VERTEX, base, m->yaw, part,
                          pitch + death_tilt, m->dead, fuv);
            o += 36;
        }
        ++(*drawn);
    }
    return o;
}

/* Upload the scratch prefix and draw it with the bound voxel texture. */
static void mob_draw_batch(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts,
                           size_t o, unsigned int tex)
{
    if (o == 0) {
        return;
    }
    if (!renderer_begin_voxel_tex(r, cam, aspect, ts, tex)) {
        return;
    }
    MeshData mesh;
    mesh.vertices = r->mob_verts;
    mesh.indices = r->mob_idx;
    mesh.vertex_count = o;
    mesh.index_count = o;
    mesh.vertex_cap = o;
    mesh.index_cap = o;
    if (gpu_chunk_upload(&r->ent_buf, &mesh, 0, 0) == 0) {
        gpu_chunk_draw(&r->ent_buf);
    }
    renderer_end_voxel(r);
}

/* Draw live projectiles as small oriented shafts (one transient upload
 * per call from the shared mob scratch, reused after the mob pass —
 * calls are sequential, so no aliasing; no per-frame allocation).
 * Shaft + head boxes rotate with the velocity (flying) or the retained
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
void renderer_draw_projectiles(Renderer *r, const ProjectilePool *pool, const Camera *cam, float aspect,
                               const TimeSystem *ts, int *out_drawn)
{
    if (out_drawn != NULL) {
        *out_drawn = 0;
    }
    if (r == NULL || pool == NULL || cam == NULL || r->mob_verts == NULL || r->mob_idx == NULL) {
        return;
    }
    float planes[6][4];
    camera_get_frustum_planes(cam, aspect, planes);
    float auv[6][4];
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        texture_atlas_tile_uv(TILE_ARROW, &u0, &v0, &u1, &v1);
        for (int f = 0; f < 6; ++f) {
            auv[f][0] = u0;
            auv[f][1] = v0;
            auv[f][2] = u1;
            auv[f][3] = v1;
        }
    }
    /* Shaft along local Z (mob convention: yaw + pitch about the part
     * center), head cube at the tip. Pivot 0 keeps rotation centered:
     * both parts are built symmetric about the origin. */
    MobModelPart shaft = {{-0.045f, -0.045f, -0.35f}, {0.09f, 0.09f, 0.70f}, TILE_ARROW, -1, 0.0f,
                          MOB_ANIM_NONE};
    MobModelPart head = {{-0.06f, -0.06f, 0.30f}, {0.12f, 0.12f, 0.18f}, TILE_ARROW, -1, 0.0f,
                         MOB_ANIM_NONE};
    size_t o = 0;
    int drawn = 0;
    for (int i = 0; i < PROJECTILE_MAX; ++i) {
        const Projectile *p = &pool->projs[i];
        if (!p->active) {
            continue;
        }
        Vec3 mn = mmath_vec3(p->pos.x - 0.5f, p->pos.y - 0.5f, p->pos.z - 0.5f);
        Vec3 mx = mmath_vec3(p->pos.x + 0.5f, p->pos.y + 0.5f, p->pos.z + 0.5f);
        if (!camera_aabb_visible(planes, mn, mx)) {
            continue;
        }
        if ((o + 72) * MESHER_FLOATS_PER_VERTEX > MOB_SCRATCH_VERTS * MESHER_FLOATS_PER_VERTEX) {
            break; /* Scratch full: remaining arrows skip a frame (bounded). */
        }
        float yaw;
        float pitch;
        if (p->state == PROJECTILE_EMBEDDED) {
            yaw = p->embed_yaw;
            pitch = -p->embed_pitch;
        } else {
            float vl = mmath_vec3_length(p->vel);
            if (!(vl > 1e-6f)) {
                continue;
            }
            Vec3 v = mmath_vec3_scale(p->vel, 1.0f / vl);
            yaw = atan2f(-v.x, -v.z);
            float sy = v.y > 1.0f ? 1.0f : (v.y < -1.0f ? -1.0f : v.y);
            pitch = -asinf(sy);
        }
        mob_emit_part(r->mob_verts + o * MESHER_FLOATS_PER_VERTEX, p->pos, yaw, &shaft, pitch, false,
                      auv);
        o += 36;
        mob_emit_part(r->mob_verts + o * MESHER_FLOATS_PER_VERTEX, p->pos, yaw, &head, pitch, false,
                      auv);
        o += 36;
        ++drawn;
    }
    if (out_drawn != NULL) {
        *out_drawn = drawn;
    }
    if (o == 0) {
        return;
    }
    if (!renderer_begin_voxel(r, cam, aspect, ts)) {
        return;
    }
    MeshData mesh;
    mesh.vertices = r->mob_verts;
    mesh.indices = r->mob_idx;
    mesh.vertex_count = o;
    mesh.index_count = o;
    mesh.vertex_cap = o;
    mesh.index_cap = o;
    if (gpu_chunk_upload(&r->ent_buf, &mesh, 0, 0) == 0) {
        gpu_chunk_draw(&r->ent_buf);
    }
    renderer_end_voxel(r);
}

/* Create a renderer.
 *
 * Args:
 *   gl: GL context.
 *
 * Returns: owned Renderer (NULL only on OOM).
 */
Renderer *renderer_create(GlContext *gl)
{
    if (gl == NULL) {
        LOG_ERROR("renderer_create: gl is NULL");
        return NULL;
    }
    Renderer *r = (Renderer *)calloc(1, sizeof(Renderer));
    if (r == NULL) {
        LOG_ERROR("renderer_create: out of memory");
        return NULL;
    }
    r->gl = gl;
    r->clear_r = 0.529f;
    r->clear_g = 0.808f;
    r->clear_b = 0.922f;
    r->clear_a = 1.0f;
    r->vp_width = 1280;
    r->vp_height = 720;
    r->mvp_loc = -1;
    r->atlas_loc = -1;
    r->sun_dir_loc = -1;
    r->light_intensity_loc = -1;
    r->sky_color_loc = -1;
    r->fog_color_loc = -1;
    r->fog_density_loc = -1;
    r->cam_pos_loc = -1;
    r->ui_ortho_loc = -1;
    r->ui_tex_ortho_loc = -1;
    r->ui_tex_atlas_loc = -1;

    /* Depth + culling + single-pass alpha blending (system GL, 1.1 tokens). */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    r->shader = shader_create(shader_voxel_vert_src(), shader_voxel_frag_src());
    if (r->shader == NULL) {
        LOG_ERROR("renderer_create: voxel shader failed; world will render as clear-only");
    } else {
        r->mvp_loc = shader_get_uniform_location(r->shader, "uMVP");
        r->atlas_loc = shader_get_uniform_location(r->shader, "uAtlas");
        r->sun_dir_loc = shader_get_uniform_location(r->shader, "uSunDir");
        r->light_intensity_loc = shader_get_uniform_location(r->shader, "uLightIntensity");
        r->sky_color_loc = shader_get_uniform_location(r->shader, "uSkyColor");
        r->fog_color_loc = shader_get_uniform_location(r->shader, "uFogColor");
        r->fog_density_loc = shader_get_uniform_location(r->shader, "uFogDensity");
        r->cam_pos_loc = shader_get_uniform_location(r->shader, "uCamPos");
        if (r->mvp_loc < 0 || r->atlas_loc < 0) {
            LOG_WARN("renderer_create: core uniforms missing (mvp=%d atlas=%d)", r->mvp_loc, r->atlas_loc);
        }
    }

    r->atlas = texture_atlas_generate();
    if (r->atlas == 0) {
        LOG_ERROR("renderer_create: atlas unavailable; textured draws will be wrong");
    }
    memcpy(r->atlas_pack, "Default", 8);
    /* Owner-converted mob skins (missing files keep the procedural tile
     * path per mob — normal on CI without mcassets/). */
    for (int i = 0; i < 3; ++i) {
        r->mob_skin_tex[i] = 0;
        const char *stem = mob_skin_file(i);
        if (stem == NULL) {
            continue;
        }
        const MobSkin *skin = mob_skin_for(i);
        const MobModel *model = mob_model_for(i);
        if (skin == NULL || model == NULL || !mob_skin_validate(skin, model->nparts)) {
            continue;
        }
        int sw = 0;
        int sh = 0;
        unsigned int tex = mob_skin_load(stem, &sw, &sh);
        if (tex != 0 && sw == skin->width && sh == skin->height) {
            r->mob_skin_tex[i] = tex;
        } else {
            texture_atlas_delete(tex);
        }
    }

    /* Particle scratch: one allocation for the process lifetime (no
     * per-frame heap churn for transient cubes). NULL on OOM degrades
     * to no particles (checked at draw time). */
    r->part_verts =
        (float *)malloc(PARTICLE_SCRATCH_VERTS * (size_t)MESHER_FLOATS_PER_VERTEX * sizeof(float));
    r->part_idx = (unsigned int *)malloc(PARTICLE_SCRATCH_VERTS * sizeof(unsigned int));
    if (r->part_verts != NULL && r->part_idx != NULL) {
        for (size_t i = 0; i < PARTICLE_SCRATCH_VERTS; ++i) {
            r->part_idx[i] = (unsigned int)i;
        }
    } else {
        free(r->part_verts);
        free(r->part_idx);
        r->part_verts = NULL;
        r->part_idx = NULL;
        LOG_WARN("renderer_create: particle scratch OOM, particles disabled");
    }
    r->mob_verts = (float *)malloc(MOB_SCRATCH_VERTS * (size_t)MESHER_FLOATS_PER_VERTEX * sizeof(float));
    r->mob_idx = (unsigned int *)malloc(MOB_SCRATCH_IDX * sizeof(unsigned int));
    if (r->mob_verts != NULL && r->mob_idx != NULL) {
        for (size_t i = 0; i < MOB_SCRATCH_IDX; ++i) {
            r->mob_idx[i] = (unsigned int)i;
        }
    } else {
        free(r->mob_verts);
        free(r->mob_idx);
        r->mob_verts = NULL;
        r->mob_idx = NULL;
        LOG_WARN("renderer_create: mob scratch OOM, mobs disabled");
    }

    /* Flat UI pipeline (crosshair + hotbar overlay). Degrades silently to
     * no-HUD when GL entry points are missing. */
    r->ui_shader = shader_create(shader_ui_vert_src(), shader_ui_frag_src());
    if (r->ui_shader == NULL) {
        LOG_WARN("renderer_create: UI shader failed; HUD will be hidden");
    } else {
        r->ui_ortho_loc = shader_get_uniform_location(r->ui_shader, "uOrtho");
        if (minec_glGenVertexArrays != NULL && minec_glGenBuffers != NULL) {
            minec_glGenVertexArrays(1, &r->ui_vao);
            minec_glGenBuffers(1, &r->ui_vbo);
        }
        if (r->ui_vao != 0 && r->ui_vbo != 0 && minec_glBindVertexArray != NULL &&
            minec_glBindBuffer != NULL && minec_glBufferData != NULL &&
            minec_glVertexAttribPointer != NULL && minec_glEnableVertexAttribArray != NULL) {
            minec_glBindVertexArray(r->ui_vao);
            minec_glBindBuffer((MinecGLenum)MINEC_GL_ARRAY_BUFFER, r->ui_vbo);
            minec_glBufferData((MinecGLenum)MINEC_GL_ARRAY_BUFFER, 0, NULL,
                               (MinecGLenum)MINEC_GL_STATIC_DRAW);
            minec_glVertexAttribPointer(0, 2, (MinecGLenum)MINEC_GL_FLOAT, 0, (MinecGLsizei)24,
                                        (const void *)0);
            minec_glEnableVertexAttribArray(0);
            minec_glVertexAttribPointer(1, 4, (MinecGLenum)MINEC_GL_FLOAT, 0, (MinecGLsizei)24,
                                        (const void *)8);
            minec_glEnableVertexAttribArray(1);
            minec_glBindVertexArray(0);
        } else {
            LOG_WARN("renderer_create: UI buffers unavailable; HUD will be hidden");
        }
    }

    /* Textured icon pipeline (atlas-tile item sprites for hotbar/menus).
     * Degrades silently to no icons (color quads already drawn). */
    r->ui_tex_shader = shader_create(shader_ui_tex_vert_src(), shader_ui_tex_frag_src());
    if (r->ui_tex_shader == NULL) {
        LOG_WARN("renderer_create: icon shader failed; item icons will be hidden");
    } else {
        r->ui_tex_ortho_loc = shader_get_uniform_location(r->ui_tex_shader, "uOrtho");
        r->ui_tex_atlas_loc = shader_get_uniform_location(r->ui_tex_shader, "uAtlas");
        if (minec_glGenVertexArrays != NULL && minec_glGenBuffers != NULL) {
            minec_glGenVertexArrays(1, &r->ui_tex_vao);
            minec_glGenBuffers(1, &r->ui_tex_vbo);
        }
        if (r->ui_tex_vao != 0 && r->ui_tex_vbo != 0 && minec_glBindVertexArray != NULL &&
            minec_glBindBuffer != NULL && minec_glBufferData != NULL &&
            minec_glVertexAttribPointer != NULL && minec_glEnableVertexAttribArray != NULL) {
            minec_glBindVertexArray(r->ui_tex_vao);
            minec_glBindBuffer((MinecGLenum)MINEC_GL_ARRAY_BUFFER, r->ui_tex_vbo);
            minec_glBufferData((MinecGLenum)MINEC_GL_ARRAY_BUFFER, 0, NULL,
                               (MinecGLenum)MINEC_GL_STATIC_DRAW);
            minec_glVertexAttribPointer(0, 2, (MinecGLenum)MINEC_GL_FLOAT, 0, (MinecGLsizei)16,
                                        (const void *)0);
            minec_glEnableVertexAttribArray(0);
            minec_glVertexAttribPointer(1, 2, (MinecGLenum)MINEC_GL_FLOAT, 0, (MinecGLsizei)16,
                                        (const void *)8);
            minec_glEnableVertexAttribArray(1);
            minec_glBindVertexArray(0);
        } else {
            LOG_WARN("renderer_create: icon buffers unavailable; item icons will be hidden");
        }
    }
    return r;
}

/* Destroy a renderer.
 *
 * Args:
 *   r: renderer to destroy.
 */
void renderer_destroy(Renderer *r)
{
    if (r == NULL) {
        return;
    }
    for (int i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        gpu_chunk_destroy(&r->gpu[i][0]);
        gpu_chunk_destroy(&r->gpu[i][1]);
    }
    gpu_chunk_destroy(&r->ent_buf);
    if (r->ui_vbo != 0 && minec_glDeleteBuffers != NULL) {
        minec_glDeleteBuffers(1, &r->ui_vbo);
    }
    if (r->ui_vao != 0 && minec_glDeleteVertexArrays != NULL) {
        minec_glDeleteVertexArrays(1, &r->ui_vao);
    }
    if (r->ui_tex_vbo != 0 && minec_glDeleteBuffers != NULL) {
        minec_glDeleteBuffers(1, &r->ui_tex_vbo);
    }
    if (r->ui_tex_vao != 0 && minec_glDeleteVertexArrays != NULL) {
        minec_glDeleteVertexArrays(1, &r->ui_tex_vao);
    }
    shader_destroy(r->ui_shader);
    shader_destroy(r->ui_tex_shader);
    texture_atlas_delete(r->atlas);
    r->atlas = 0;
    for (int i = 0; i < 3; ++i) {
        texture_atlas_delete(r->mob_skin_tex[i]);
        r->mob_skin_tex[i] = 0;
    }
    shader_destroy(r->shader);
    free(r->part_verts);
    free(r->part_idx);
    free(r->mob_verts);
    free(r->mob_idx);
    free(r);
}

/* Set the clear color.
 *
 * Args:
 *   r: renderer.
 *   r_, g, b, a: color components.
 */
void renderer_set_clear_color(Renderer *r, float r_, float g, float b, float a)
{
    if (r == NULL) {
        return;
    }
    r->clear_r = r_;
    r->clear_g = g;
    r->clear_b = b;
    r->clear_a = a;
}

/* Set the viewport.
 *
 * Args:
 *   r: renderer.
 *   width, height: size in pixels.
 */
void renderer_set_viewport(Renderer *r, int width, int height)
{
    if (r == NULL) {
        return;
    }
    if (width <= 0 || height <= 0) {
        LOG_WARN("renderer_set_viewport: ignoring invalid size %dx%d", width, height);
        return;
    }
    r->vp_width = width;
    r->vp_height = height;
    glViewport(0, 0, width, height);
}

/* Clear buffers.
 *
 * Args:
 *   r: renderer.
 */
void renderer_clear(Renderer *r)
{
    if (r == NULL) {
        return;
    }
    glClearColor(r->clear_r, r->clear_g, r->clear_b, r->clear_a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Rebuild dirty chunk meshes and upload to GPU (timed phases).
 * Builds both passes (opaque + transparent) per dirty chunk. Only a
 * bounded number of chunks rebuild per call: a fresh forest view can
 * queue a dozen dirty meshes at once, and building them all in one
 * frame costs 30-60 ms (the classic "30fps" hitch). Leftovers stay
 * dirty and stream in over the next frames — popping one chunk late
 * beats freezing the whole game.
 *
 * Args:
 *   r: renderer.
 *   w: world.
 */
void renderer_refresh_world(Renderer *r, World *w)
{
    if (r == NULL || w == NULL) {
        return;
    }
    double mesh_ms = 0.0;
    double upload_ms = 0.0;
    int rebuilt = 0;
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        if (rebuilt >= RENDERER_MESH_BUDGET) {
            break; /* Catch-up cap: the rest stream in next frames. */
        }
        Chunk *c = w->chunks[i];
        if (c == NULL) {
            continue;
        }
        if (!c->dirty) {
            continue;
        }
        double t0 = time_now_seconds();
        MeshData *opaque = mesher_build_chunk_mesh(c, w);
        MeshData *transp = mesher_build_transparent_mesh(c, w);
        double t1 = time_now_seconds();
        mesh_ms += (t1 - t0) * 1000.0;
        if (opaque == NULL || transp == NULL) {
            LOG_ERROR("renderer: mesher failed for chunk (%d,%d)", c->cx, c->cz);
            mesher_free(opaque);
            mesher_free(transp);
            continue;
        }
        int slot = renderer_find_slot(r, c->cx, c->cz);
        if (slot < 0) {
            LOG_ERROR("renderer: no GPU slot for chunk (%d,%d)", c->cx, c->cz);
            mesher_free(opaque);
            mesher_free(transp);
            continue;
        }
        double t2 = time_now_seconds();
        int rc = gpu_chunk_upload(&r->gpu[slot][RENDERER_PASS_OPAQUE], opaque, c->cx, c->cz);
        int rc2 = gpu_chunk_upload(&r->gpu[slot][RENDERER_PASS_TRANSPARENT], transp, c->cx, c->cz);
        double t3 = time_now_seconds();
        upload_ms += (t3 - t2) * 1000.0;
        if (rc != 0 || rc2 != 0) {
            LOG_ERROR("renderer: GPU upload failed for chunk (%d,%d)", c->cx, c->cz);
        }
        mesher_free(opaque);
        mesher_free(transp);
        c->dirty = false;
        ++rebuilt;
    }
    r->perf.mesh_ms = mesh_ms;
    r->perf.upload_ms = upload_ms;
    if (mesh_ms > 0.0 || upload_ms > 0.0) {
        LOG_DEBUG("renderer refresh: mesh %.2f ms, upload %.2f ms", mesh_ms, upload_ms);
    }
}

/* Delete GPU buffers for chunks missing from the world.
 *
 * Args:
 *   r: renderer.
 *   w: world.
 */
void renderer_prune_world(Renderer *r, const World *w)
{
    if (r == NULL || w == NULL) {
        return;
    }
    for (int i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        bool live = r->gpu[i][0].vao != 0 || r->gpu[i][0].valid || r->gpu[i][1].vao != 0 ||
                    r->gpu[i][1].valid;
        if (!live) {
            continue;
        }
        if (world_get_chunk(w, r->gpu[i][0].cx, r->gpu[i][0].cz) == NULL) {
            LOG_DEBUG("renderer: pruning GPU buffers for unloaded chunk (%d,%d)", r->gpu[i][0].cx,
                      r->gpu[i][0].cz);
            gpu_chunk_destroy(&r->gpu[i][0]);
            gpu_chunk_destroy(&r->gpu[i][1]);
        }
    }
}

/* Bind the voxel shader + atlas and upload a full lighting uniform set
 * for the given camera. Shared by world, entity, and overlay draws so GL
 * state handling cannot drift between passes.
 *
 * Args:
 *   r: renderer. cam: camera. aspect: viewport aspect.
 *   ts: time of day (NULL = fixed noon lighting).
 *
 * Returns: true when bound and ready to draw.
 */
static bool renderer_begin_voxel(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts)
{
    return renderer_begin_voxel_tex(r, cam, aspect, ts, r != NULL ? r->atlas : 0);
}

static bool renderer_begin_voxel_tex(Renderer *r, const Camera *cam, float aspect,
                                     const TimeSystem *ts, unsigned int tex)
{
    if (r->shader == NULL || !shader_is_ready(r->shader) || tex == 0) {
        return false;
    }
    if (shader_bind(r->shader) != MINEC_SHADER_OK) {
        return false;
    }
    Vec3 sky = mmath_vec3(0.529f, 0.808f, 0.922f);
    Vec3 sun = mmath_vec3(-0.45f, 0.85f, 0.30f);
    float intensity = 1.0f;
    if (ts != NULL) {
        sky = time_get_sky_color(ts);
        sun = time_get_sun_dir(ts);
        intensity = time_get_light_intensity(ts);
    } else {
        sun = mmath_vec3_normalize(sun);
    }
    Mat4 view = camera_get_view(cam);
    Mat4 proj = camera_get_proj(cam, aspect);
    Mat4 mvp = mmath_mat4_mul(proj, view);
    shader_set_uniform_mat4(r->mvp_loc, mvp.m);
    if (minec_glActiveTexture != NULL) {
        minec_glActiveTexture((MinecGLenum)MINEC_GL_TEXTURE0);
    }
    if (minec_glBindTexture != NULL) {
        minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, tex);
    }
    shader_set_uniform_int(r->atlas_loc, 0);
    shader_set_uniform_vec3(r->sun_dir_loc, sun.x, sun.y, sun.z);
    shader_set_uniform_float(r->light_intensity_loc, intensity);
    shader_set_uniform_vec3(r->sky_color_loc, sky.x, sky.y, sky.z);
    shader_set_uniform_vec3(r->fog_color_loc, sky.x, sky.y, sky.z);
    shader_set_uniform_float(r->fog_density_loc, RENDERER_FOG_DENSITY);
    Vec3 cam_pos = camera_get_position(cam);
    shader_set_uniform_vec3(r->cam_pos_loc, cam_pos.x, cam_pos.y, cam_pos.z);
    return true;
}

/* Unbind the voxel shader + atlas after a pass. */
static void renderer_end_voxel(Renderer *r)
{
    (void)r;
    if (minec_glBindTexture != NULL) {
        minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, 0);
    }
    shader_unbind();
}

/* Draw the world with frustum culling under the current time of day.
 *
 * Args:
 *   r: renderer.
 *   w: world.
 *   cam: camera.
 *   aspect: viewport aspect.
 *   ts: time of day (NULL = fixed noon lighting).
 */
void renderer_draw_world(Renderer *r, const World *w, const Camera *cam, float aspect, const TimeSystem *ts)
{
    if (r == NULL || w == NULL || cam == NULL) {
        return;
    }
    /* Day/night sky drives the clear color (NULL ts keeps noon defaults). */
    Vec3 sky = mmath_vec3(0.529f, 0.808f, 0.922f);
    Vec3 sun = mmath_vec3(-0.45f, 0.85f, 0.30f);
    float intensity = 1.0f;
    if (ts != NULL) {
        sky = time_get_sky_color(ts);
        sun = time_get_sun_dir(ts);
        intensity = time_get_light_intensity(ts);
    } else {
        sun = mmath_vec3_normalize(sun);
    }
    renderer_set_clear_color(r, sky.x, sky.y, sky.z, 1.0f);

    double t0 = time_now_seconds();
    renderer_clear(r);
    r->perf.drawn = 0;
    r->perf.drawn_t = 0;
    r->perf.culled = 0;
    if (!renderer_begin_voxel(r, cam, aspect, ts)) {
        r->perf.draw_ms = (time_now_seconds() - t0) * 1000.0;
        return;
    }
    Vec3 eye = camera_get_position(cam);
    float planes[6][4];
    camera_get_frustum_planes(cam, aspect, planes);

    /* PASS 1 (opaque): iterate world chunks so stale buffers are never
     * drawn; look up each chunk's GPU slot for the actual draw. */
    for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        const Chunk *c = w->chunks[i];
        if (c == NULL) {
            continue;
        }
        Vec3 mn = mmath_vec3(c->aabb_min[0], c->aabb_min[1], c->aabb_min[2]);
        Vec3 mx = mmath_vec3(c->aabb_max[0], c->aabb_max[1], c->aabb_max[2]);
        if (!camera_aabb_visible(planes, mn, mx)) {
            r->perf.culled++;
            continue;
        }
        int slot = renderer_find_slot(r, c->cx, c->cz);
        if (slot < 0) {
            continue;
        }
        if (!r->gpu[slot][RENDERER_PASS_OPAQUE].valid) {
            continue;
        }
        gpu_chunk_draw(&r->gpu[slot][RENDERER_PASS_OPAQUE]);
        r->perf.drawn++;
    }

    /* PASS 2 (transparent): collect visible slots, sort far-to-near by
     * chunk-center distance, draw with depth writes off. Within-chunk
     * face order stays mesher order (documented limitation). */
    {
        typedef struct {
            int slot;
            float dist2;
        } TEntry;
        /* 1024 entries x 8 B = 8 KiB of stack; fine on desktop stacks. */
        TEntry order[WORLD_MAX_CHUNKS];
        size_t n = 0;
        for (size_t i = 0; i < WORLD_MAX_CHUNKS; ++i) {
            const Chunk *c = w->chunks[i];
            if (c == NULL) {
                continue;
            }
            Vec3 mn = mmath_vec3(c->aabb_min[0], c->aabb_min[1], c->aabb_min[2]);
            Vec3 mx = mmath_vec3(c->aabb_max[0], c->aabb_max[1], c->aabb_max[2]);
            if (!camera_aabb_visible(planes, mn, mx)) {
                continue; /* Already counted as culled in pass 1. */
            }
            int slot = renderer_find_slot(r, c->cx, c->cz);
            if (slot < 0 || !r->gpu[slot][RENDERER_PASS_TRANSPARENT].valid) {
                continue;
            }
            float dx = (mn.x + mx.x) * 0.5f - eye.x;
            float dy = (mn.y + mx.y) * 0.5f - eye.y;
            float dz = (mn.z + mx.z) * 0.5f - eye.z;
            order[n].slot = slot;
            order[n].dist2 = dx * dx + dy * dy + dz * dz;
            ++n;
        }
        /* Insertion sort, far-to-near (n is small in practice). */
        for (size_t i = 1; i < n; ++i) {
            TEntry key = order[i];
            size_t j = i;
            while (j > 0 && order[j - 1].dist2 < key.dist2) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = key;
        }
        glDepthMask(GL_FALSE);
        for (size_t i = 0; i < n; ++i) {
            gpu_chunk_draw(&r->gpu[order[i].slot][RENDERER_PASS_TRANSPARENT]);
            r->perf.drawn_t++;
        }
        glDepthMask(GL_TRUE);
    }
    renderer_end_voxel(r);
    r->perf.draw_ms = (time_now_seconds() - t0) * 1000.0;
}

/* Cube corner tables with the mesher's verified CCW winding (faces
 * -X,+X,-Y,+Y,-Z,+Z; corners A,B,C,D; triangles (0,1,2)+(2,1,3)).
 * Positions are unit-cube offsets scaled by the caller.
 */
static const float CUBE_CORNERS[6][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0, 1, 1}},
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 1}, {1, 1, 0}},
    {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {0, 1, 0}, {1, 1, 0}},
    {{1, 0, 0}, {0, 0, 0}, {1, 1, 0}, {0, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}},
};
static const float CUBE_NORMALS[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
};
static const float CUBE_U[4] = {0.0f, 1.0f, 0.0f, 1.0f};
static const float CUBE_V[4] = {0.0f, 0.0f, 1.0f, 1.0f};

/* Emit one textured cube (36 non-indexed verts, VoxelVertex layout) into
 * dst (36*9 floats); ao is constant across the cube.
 *
 * Args:
 *   dst: 324-float destination (must not be NULL).
 *   minx, miny, minz: cube minimum corner.
 *   size: edge length (> 0).
 *   tile: atlas tile index.
 *   ao: brightness multiplier.
 */
static void entity_emit_cube(float *dst, float minx, float miny, float minz, float size, int tile, float ao)
{
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texture_atlas_tile_uv(tile, &u0, &v0, &u1, &v1);
    /* Two triangles per face: (0,1,2) + (2,1,3). */
    static const int TRIS[6] = {0, 1, 2, 2, 1, 3};
    size_t n = 0;
    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 6; ++k) {
            int i = TRIS[k];
            float *v = dst + n * MESHER_FLOATS_PER_VERTEX;
            v[0] = minx + CUBE_CORNERS[f][i][0] * size;
            v[1] = miny + CUBE_CORNERS[f][i][1] * size;
            v[2] = minz + CUBE_CORNERS[f][i][2] * size;
            v[3] = CUBE_NORMALS[f][0];
            v[4] = CUBE_NORMALS[f][1];
            v[5] = CUBE_NORMALS[f][2];
            v[6] = (CUBE_U[i] == 0.0f) ? u0 : u1;
            v[7] = (CUBE_V[i] == 0.0f) ? v0 : v1;
            v[8] = ao;
            n += 1;
        }
    }
}

/* Emit one dropped-item sprite: two diagonal quads (X shape, both
 * windings each = 24 non-indexed verts, VoxelVertex layout) showing the
 * full item tile — the readable MC-fast-graphics look instead of a
 * tiny textured cube. Sprites slowly spin around Y (MC drops rotate;
 * a static X reads as a tan blob from most angles) and bob gently.
 * Normals point up so lighting stays bright; the shader's alpha cutout
 * discards transparent texels (same rule as cross-sprite plants).
 * Depth-tested like everything else.
 *
 * Args:
 *   dst: 216-float destination (must not be NULL).
 *   cx, cy, cz: sprite center (world).
 *   size: sprite edge length (> 0).
 *   tile: atlas tile index.
 *   spin: yaw radians (0 faces the diagonal like a static sprite).
 */
static void entity_emit_sprite(float *dst, float cx, float cy, float cz, float size, int tile,
                               float spin)
{
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    texture_atlas_tile_uv(tile, &u0, &v0, &u1, &v1);
    float h = size * 0.5f;
    float c = cosf(spin);
    float s = sinf(spin);
    /* Two diagonal quads, each wound both ways (CCW front under culling
     * from either side). Corner order per quad: BL, BR, TL, TR with
     * (0,1,2,2,1,3) triangles. */
    static const float QUAD[4][3] = {{-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, 1, 0}};
    static const int TRIS[6] = {0, 1, 2, 2, 1, 3};
    size_t n = 0;
    for (int q = 0; q < 2; ++q) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int k = 0; k < 6; ++k) {
                int kk = (pass == 0) ? k : (5 - k); /* Reverse winding. */
                int i = TRIS[kk];
                float lx = QUAD[i][0];
                float ly = QUAD[i][1];
                float *v = dst + n * MESHER_FLOATS_PER_VERTEX;
                /* Quad 0 runs (-x,-z)..(+x,+z); quad 1 mirrors in z;
                 * the pair spins together around Y. */
                float qx = lx * h;
                float qz = (q == 0 ? lx : -lx) * h;
                float dx = qx * c + qz * s;
                float dz = -qx * s + qz * c;
                v[0] = cx + dx;
                v[1] = cy + ly * h;
                v[2] = cz + dz;
                v[3] = 0.0f;
                v[4] = 1.0f;
                v[5] = 0.0f;
                v[6] = (lx < 0.0f) ? u0 : u1;
                v[7] = (ly < 0.0f) ? v0 : v1;
                v[8] = 1.0f;
                n += 1;
            }
        }
    }
}

/* Upload raw VoxelVertex verts (non-indexed count) into the scratch
 * buffer and draw it. No-op on bad args or missing pipe.
 */
static void renderer_draw_transient(Renderer *r, const float *verts, size_t vcount)
{
    if (vcount == 0) {
        gpu_chunk_destroy(&r->ent_buf);
        return;
    }
    size_t icount = vcount; /* One index per vert (identity mapping). */
    unsigned int *idx = (unsigned int *)malloc(icount * sizeof(unsigned int));
    if (idx == NULL) {
        return;
    }
    for (size_t i = 0; i < icount; ++i) {
        idx[i] = (unsigned int)i;
    }
    MeshData mesh;
    mesh.vertices = (float *)verts;
    mesh.indices = idx;
    mesh.vertex_count = vcount;
    mesh.index_count = icount;
    mesh.vertex_cap = vcount;
    mesh.index_cap = icount;
    if (gpu_chunk_upload(&r->ent_buf, &mesh, 0, 0) == 0) {
        gpu_chunk_draw(&r->ent_buf);
    }
    free(idx);
}

/* Draw active item entities as floating item sprites (crossed quads
 * with the full item texture + a gentle bob).
 *
 * Args:
 *   r: renderer. pool: entity pool. cam: camera. aspect: viewport aspect.
 *   ts: time of day (may be NULL).
 */
void renderer_draw_entities(Renderer *r, const EntityPool *pool, const Camera *cam, float aspect,
                            const TimeSystem *ts)
{
    if (r == NULL || pool == NULL || cam == NULL) {
        return;
    }
    int n = 0;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        if (pool->items[i].active) {
            ++n;
        }
    }
    if (n == 0) {
        gpu_chunk_destroy(&r->ent_buf);
        return;
    }
    if (!renderer_begin_voxel(r, cam, aspect, ts)) {
        return;
    }
    size_t vcount = (size_t)n * 24;
    float *verts = (float *)malloc(vcount * MESHER_FLOATS_PER_VERTEX * sizeof(float));
    if (verts == NULL) {
        renderer_end_voxel(r);
        return;
    }
    size_t o = 0;
    for (int i = 0; i < ENTITY_MAX; ++i) {
        const ItemEntity *e = &pool->items[i];
        if (!e->active) {
            continue;
        }
        float bob = 0.10f + 0.05f * sinf(e->age * 3.0f);
        float size = 0.35f;
        int tile = item_get_info(e->stack.item)->tile;
        /* Sprite center floats above the settled base (never sinks);
         * one slow turn every ~4 s so the art reads from all sides. */
        float spin = e->age * 1.5f;
        entity_emit_sprite(verts + o * MESHER_FLOATS_PER_VERTEX, e->pos.x, e->pos.y + bob + size * 0.5f,
                           e->pos.z, size, tile, spin);
        o += 24;
    }
    renderer_draw_transient(r, verts, vcount);
    free(verts);
    renderer_end_voxel(r);
}

/* Draw active particles as small shrinking textured cubes (one transient
 * upload per call from the owned scratch buffer: no per-frame heap
 * churn). Cubes shrink linearly with remaining life, then pop out.
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
                             const TimeSystem *ts)
{
    if (r == NULL || pool == NULL || cam == NULL || r->part_verts == NULL || r->part_idx == NULL) {
        return;
    }
    size_t o = 0;
    for (int i = 0; i < PARTICLE_MAX; ++i) {
        const Particle *p = &pool->items[i];
        if (!p->active || !(p->life > 0.0f)) {
            continue;
        }
        float remain = 1.0f - p->age / p->life;
        if (remain <= 0.0f) {
            continue;
        }
        float size = p->size * remain;
        if (size < 0.012f) {
            continue;
        }
        entity_emit_cube(r->part_verts + o * MESHER_FLOATS_PER_VERTEX, p->pos.x - size * 0.5f,
                         p->pos.y - size * 0.5f, p->pos.z - size * 0.5f, size, p->tile, 1.0f);
        o += 36;
    }
    if (o == 0) {
        return;
    }
    if (!renderer_begin_voxel(r, cam, aspect, ts)) {
        return;
    }
    MeshData mesh;
    mesh.vertices = r->part_verts;
    mesh.indices = r->part_idx;
    mesh.vertex_count = o;
    mesh.index_count = o;
    mesh.vertex_cap = o;
    mesh.index_cap = o;
    if (gpu_chunk_upload(&r->ent_buf, &mesh, 0, 0) == 0) {
        gpu_chunk_draw(&r->ent_buf);
    }
    renderer_end_voxel(r);
}

/* Draw a crack-stage overlay cube on a mining target.
 *
 * Args:
 *   r: renderer. cam: camera. aspect: viewport aspect. ts: time of day.
 *   bx, by, bz: target cell. progress: mining fraction 0..1.
 */
void renderer_draw_block_overlay(Renderer *r, const Camera *cam, float aspect, const TimeSystem *ts, int bx,
                                 int by, int bz, float progress)
{
    if (r == NULL || cam == NULL) {
        return;
    }
    if (!(progress >= 0.0f) || !(progress < 1.0f)) {
        return;
    }
    int stage = (int)(progress * 5.0f);
    if (stage < 0) {
        stage = 0;
    }
    if (stage > 4) {
        stage = 4;
    }
    if (!renderer_begin_voxel(r, cam, aspect, ts)) {
        return;
    }
    float verts[36 * 9];
    float pad = 0.001f;
    entity_emit_cube(verts, (float)bx - pad, (float)by - pad, (float)bz - pad, 1.0f + pad * 2.0f,
                     TILE_CRACK0 + stage, 1.0f);
    renderer_draw_transient(r, verts, 36);
    renderer_end_voxel(r);
}

/* Drop every chunk GPU buffer (both passes) for session teardown.
 *
 * Args:
 *   r: renderer.
 */
void renderer_drop_all(Renderer *r)
{
    if (r == NULL) {
        return;
    }
    for (int i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        gpu_chunk_destroy(&r->gpu[i][0]);
        gpu_chunk_destroy(&r->gpu[i][1]);
    }
    gpu_chunk_destroy(&r->ent_buf);
    r->perf.drawn = 0;
    r->perf.drawn_t = 0;
    r->perf.culled = 0;
}

/* Replace the atlas texture from a resource pack (old kept on failure).
 *
 * Args:
 *   r: renderer.
 *   pack: pack name ("Default"/NULL/empty = procedural).
 *
 * Returns: 0 on success.
 */
int renderer_reload_atlas(Renderer *r, const char *pack)
{
    if (r == NULL) {
        return -1;
    }
    unsigned int tex = texture_atlas_generate_from_pack(pack);
    if (tex == 0) {
        LOG_ERROR("renderer_reload_atlas: pack '%s' failed, keeping previous atlas",
                  pack != NULL ? pack : "Default");
        return -2;
    }
    texture_atlas_delete(r->atlas);
    r->atlas = tex;
    const char *name = (pack != NULL && pack[0] != '\0') ? pack : "Default";
    size_t n = strlen(name);
    if (n > sizeof(r->atlas_pack) - 1) {
        n = sizeof(r->atlas_pack) - 1;
    }
    memcpy(r->atlas_pack, name, n);
    r->atlas_pack[n] = '\0';
    LOG_INFO("renderer_reload_atlas: active pack '%s' (tex %u)", r->atlas_pack, tex);
    return 0;
}

/* Shared UI upload+draw: binds the UI shader, sets the ortho matrix,
 * uploads HUD-format verts, draws with depth off. Caller binds nothing.
 * Returns 0 on success.
 */
static int renderer_ui_draw_verts(Renderer *r, int width, int height, const float *verts, size_t count)
{
    if (shader_bind(r->ui_shader) != MINEC_SHADER_OK) {
        return -1;
    }
    Mat4 ortho = mmath_mat4_ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
    shader_set_uniform_mat4(r->ui_ortho_loc, ortho.m);
    minec_glBindVertexArray(r->ui_vao);
    minec_glBindBuffer((MinecGLenum)MINEC_GL_ARRAY_BUFFER, r->ui_vbo);
    minec_glBufferData((MinecGLenum)MINEC_GL_ARRAY_BUFFER,
                       (MinecGLsizeiptr)(count * HUD_FLOATS_PER_VERT * sizeof(float)), verts,
                       (MinecGLenum)MINEC_GL_STATIC_DRAW);
    /* UI quads are authored in y-down pixel space, which lands clockwise in
     * NDC after the Y-flip — i.e. back faces under the world's CCW convention.
     * Culling is meaningless for overlays, so lift it (and depth) for the pass.
     */
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    minec_glDrawArrays((MinecGLenum)MINEC_GL_TRIANGLES, 0, (MinecGLsizei)count);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    minec_glBindVertexArray(0);
    shader_unbind();
    return 0;
}

/* UI pipe readiness check. */
static bool renderer_ui_ready(const Renderer *r)
{
    return r->ui_shader != NULL && shader_is_ready(r->ui_shader) && r->ui_vao != 0 && r->ui_vbo != 0 &&
           minec_glBindVertexArray != NULL && minec_glBindBuffer != NULL && minec_glBufferData != NULL &&
           minec_glDrawArrays != NULL;
}

/* Icon pipe readiness check. */
static bool renderer_ui_tex_ready(const Renderer *r)
{
    return r != NULL && r->ui_tex_shader != NULL && shader_is_ready(r->ui_tex_shader) &&
           r->ui_tex_vao != 0 && r->ui_tex_vbo != 0 && r->atlas != 0 &&
           minec_glBindVertexArray != NULL && minec_glBindBuffer != NULL && minec_glBufferData != NULL &&
           minec_glDrawArrays != NULL && minec_glBindTexture != NULL;
}

/* Draw textured item icons (atlas tiles) as 2D quads. depth off, blend
 * on, alpha cutout at 0.05. No-op on bad args or missing pipe/atlas.
 *
 * Args:
 *   r: renderer (must not be NULL).
 *   width, height: viewport in pixels (> 0).
 *   b: icon batch (must not be NULL; empty batches draw nothing).
 */
void renderer_draw_item_icons(Renderer *r, int width, int height, const IconBatch *b)
{
    if (r == NULL || width <= 0 || height <= 0 || b == NULL || b->quads <= 0) {
        return;
    }
    if (!renderer_ui_tex_ready(r)) {
        return;
    }
    if (shader_bind(r->ui_tex_shader) != MINEC_SHADER_OK) {
        return;
    }
    Mat4 ortho = mmath_mat4_ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
    shader_set_uniform_mat4(r->ui_tex_ortho_loc, ortho.m);
    if (minec_glActiveTexture != NULL) {
        minec_glActiveTexture((MinecGLenum)MINEC_GL_TEXTURE0);
    }
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, r->atlas);
    shader_set_uniform_int(r->ui_tex_atlas_loc, 0);
    minec_glBindVertexArray(r->ui_tex_vao);
    minec_glBindBuffer((MinecGLenum)MINEC_GL_ARRAY_BUFFER, r->ui_tex_vbo);
    size_t verts = (size_t)b->quads * 6;
    minec_glBufferData((MinecGLenum)MINEC_GL_ARRAY_BUFFER,
                       (MinecGLsizeiptr)(verts * ICON_FLOATS_PER_VERT * sizeof(float)), b->verts,
                       (MinecGLenum)MINEC_GL_STATIC_DRAW);
    /* Same overlay convention as the flat pass: no culling, no depth. */
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    minec_glDrawArrays((MinecGLenum)MINEC_GL_TRIANGLES, 0, (MinecGLsizei)verts);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    minec_glBindTexture((MinecGLenum)MINEC_GL_TEXTURE_2D, 0);
    minec_glBindVertexArray(0);
    shader_unbind();
}

/* Count drawable buffers (opaque pass; semantics unchanged from M4).
 *
 * Args:
 *   r: renderer.
 *
 * Returns: drawable count.
 */
size_t renderer_drawable_chunks(const Renderer *r)
{
    if (r == NULL) {
        return 0;
    }
    size_t n = 0;
    for (int i = 0; i < WORLD_MAX_CHUNKS; ++i) {
        if (r->gpu[i][RENDERER_PASS_OPAQUE].valid) {
            n++;
        }
    }
    return n;
}

/* Performance snapshot.
 *
 * Args:
 *   r: renderer.
 *
 * Returns: perf copy.
 */
RendererPerf renderer_get_perf(const Renderer *r)
{
    RendererPerf zero = {0.0, 0.0, 0.0, 0, 0, 0};
    if (r == NULL) {
        return zero;
    }
    return r->perf;
}

/* Draw the 2D HUD overlay (crosshair + hotbar with icons and counts,
 * durability bars, eat progress, damage flash, plus survival vitals) on
 * top of the 3D scene. Counts use the bitmap font pass after the quad
 * pass. No-op when unavailable.
 *
 * Args:
 *   r: renderer.
 *   width, height: viewport in pixels.
 *   hotbar: 9 ItemStacks, slots 0..8.
 *   hotbar_sel: selected slot.
 *   health, max_health, hunger, max_hunger: vitals.
 *   show_vitals: draw health/hunger bars (survival).
 *   eat_frac: eating progress 0..1 (< 0 hides).
 *   bow_frac: bow draw charge 0..1 (< 0 hides).
 *   hurt_flash: damage flash 0..1 (0 hides).
 */
void renderer_draw_hud(Renderer *r, int width, int height, const ItemStack *hotbar, int hotbar_sel,
                       float health, float max_health, float hunger, float max_hunger, bool show_vitals,
                       float eat_frac, float bow_frac, float hurt_flash)
{
    if (r == NULL || width <= 0 || height <= 0 || hotbar == NULL || !renderer_ui_ready(r)) {
        return;
    }
    HudFrame frame;
    hud_build(&frame, width, height, hotbar, hotbar_sel);
    hud_build_durability(&frame, width, height, hotbar);
    if (show_vitals) {
        hud_build_vitals(&frame, width, height, health, max_health, hunger, max_hunger);
    }
    hud_build_eat(&frame, width, height, eat_frac);
    hud_build_bow(&frame, width, height, bow_frac);
    hud_build_flash(&frame, width, height, hurt_flash);
    if (frame.count == 0) {
        return;
    }
    renderer_ui_draw_verts(r, width, height, frame.verts, frame.count);
    /* Textured item icons, centered in their slots (24 px in 40 px cells). */
    {
        IconBatch icons;
        icons_clear(&icons);
        for (int i = 0; i < HUD_HOTBAR_SLOTS; ++i) {
            const ItemStack *s = &hotbar[i];
            if (stack_is_empty(s)) {
                continue;
            }
            float sx = 0.0f;
            float sy = 0.0f;
            hud_hotbar_slot(width, height, i, &sx, &sy);
            icons_push(&icons, sx + 8.0f, sy + 8.0f, 24.0f, item_get_info(s->item)->tile);
        }
        renderer_draw_item_icons(r, width, height, &icons);
    }
    /* Stack counts (tools show none: max_stack 1 implies count 1). */
    for (int i = 0; i < HUD_HOTBAR_SLOTS; ++i) {
        const ItemStack *s = &hotbar[i];
        if (stack_is_empty(s) || s->count <= 1) {
            continue;
        }
        float sx = 0.0f;
        float sy = 0.0f;
        hud_hotbar_slot(width, height, i, &sx, &sy);
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)s->count);
        renderer_draw_text(r, sx + 24.0f, sy + 22.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, num);
    }
}

/* Draw raw colored UI quads.
 *
 * Args:
 *   r: renderer.
 *   width, height: viewport.
 *   verts: HUD-format vertices.
 *   count: vertex count.
 */
void renderer_draw_rects(Renderer *r, int width, int height, const float *verts, size_t count)
{
    if (r == NULL || width <= 0 || height <= 0 || verts == NULL || count == 0 || !renderer_ui_ready(r)) {
        return;
    }
    renderer_ui_draw_verts(r, width, height, verts, count);
}

/* Draw a bitmap-font string (truncated past 160 chars).
 *
 * Args:
 *   r: renderer.
 *   x, y: top-left origin.
 *   scale: pixel scale.
 *   cr, cg, cb, ca: color.
 *   text: string.
 */
void renderer_draw_text(Renderer *r, float x, float y, float scale, float cr, float cg, float cb, float ca,
                        const char *text)
{
    if (r == NULL || text == NULL || text[0] == '\0' || !(scale > 0.0f) || !renderer_ui_ready(r)) {
        return;
    }
    /* Scratch cap: per-pixel quads, worst case 64 px/char x 6 verts =
     * 384 verts/char, so dense fills truncate early (font_build_quads stops
     * at the cap, never overflows). Typical sparse glyphs (~1/4 fill) fit
     * ~200 chars; hard clip is 160. */
    enum { TEXT_MAX_VERTS = 20000 };
    float *verts = (float *)malloc((size_t)TEXT_MAX_VERTS * HUD_FLOATS_PER_VERT * sizeof(float));
    if (verts == NULL) {
        return;
    }
    char clipped[161];
    size_t n = strlen(text);
    if (n > 160) {
        n = 160;
    }
    memcpy(clipped, text, n);
    clipped[n] = '\0';
    size_t count = font_build_quads(clipped, x, y, scale, cr, cg, cb, ca, verts, TEXT_MAX_VERTS);
    if (count > 0) {
        /* Viewport size only feeds the ortho matrix; reuse last known size. */
        renderer_ui_draw_verts(r, r->vp_width, r->vp_height, verts, count);
    }
    free(verts);
}

/* Measure a text string.
 *
 * Args:
 *   text, scale: string and pixel scale.
 *   out_w/out_h: receivers.
 */
void renderer_measure_text(const char *text, float scale, float *out_w, float *out_h)
{
    font_measure(text, scale, out_w, out_h);
}
