#include "game/interaction.h"
#include "core/log.h"
#include "game/player.h"
#include "game/raycast.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <stddef.h>

/* Dirty-mark the chunk owning (wx,wz) plus 4-neighbours when the block sits
 * on a chunk border, so shared faces remesh on both sides.
 */
static void dirty_for_edit(World *w, int wx, int wz)
{
    int ccx = wx >= 0 ? wx / 16 : -((-wx + 15) / 16);
    int ccz = wz >= 0 ? wz / 16 : -((-wz + 15) / 16);
    int lx = wx - ccx * 16;
    int lz = wz - ccz * 16;
    Chunk *c = world_get_chunk(w, ccx, ccz);
    if (c != NULL) {
        c->dirty = true;
    }
    if (lx == 0) {
        Chunk *n = world_get_chunk(w, ccx - 1, ccz);
        if (n != NULL) {
            n->dirty = true;
        }
    }
    if (lx == 15) {
        Chunk *n = world_get_chunk(w, ccx + 1, ccz);
        if (n != NULL) {
            n->dirty = true;
        }
    }
    if (lz == 0) {
        Chunk *n = world_get_chunk(w, ccx, ccz - 1);
        if (n != NULL) {
            n->dirty = true;
        }
    }
    if (lz == 15) {
        Chunk *n = world_get_chunk(w, ccx, ccz + 1);
        if (n != NULL) {
            n->dirty = true;
        }
    }
}

/* Flag the owning chunk for disk persistence (neighbors need mesh-only
 * refresh, handled by dirty_for_edit).
 */
static void mark_saved(World *w, int wx, int wz)
{
    int ccx = wx >= 0 ? wx / 16 : -((-wx + 15) / 16);
    int ccz = wz >= 0 ? wz / 16 : -((-wz + 15) / 16);
    Chunk *c = world_get_chunk(w, ccx, ccz);
    if (c != NULL) {
        c->save_dirty = true;
    }
}

/* Non-solid decor occupies a block cell but yields when a block is placed
 * there. The caller receives the replaced block so Survival can spawn its
 * normal drop and Creative can discard it.
 */
static bool interaction_replaceable(uint16_t block)
{
    return block == BLOCK_AIR || block_is_water(block) || block_is_cross(block);
}

/* Flowers and grass plants need a valid ground surface. A cross-sprite plant
 * is not a support block, so this also prevents vegetation columns. */
static bool interaction_plant_supported(const World *w, int wx, int wy, int wz)
{
    if (w == NULL || wy <= 0) {
        return false;
    }
    uint16_t support = world_get_block(w, wx, wy - 1, wz);
    return support == BLOCK_GRASS || support == BLOCK_DIRT || support == BLOCK_SNOW;
}
/* Write a block by world coords (chunk must be loaded).
 *
 * Args:
 *   w: world.
 *   wx, wy, wz: world coords.
 *   id: new block.
 *
 * Returns: true when written.
 */
static bool write_world_block(World *w, int wx, int wy, int wz, uint16_t id)
{
    return world_set_block(w, wx, wy, wz, id);
}

/* Break the targeted block.
 *
 * Args:
 *   w: world.
 *   hit: raycast result.
 *
 * Returns: broken ID or -1.
 */
int interaction_break(World *w, const HitResult *hit)
{
    if (w == NULL || hit == NULL || !hit->hit) {
        return -1;
    }
    int bx = hit->block[0];
    int by = hit->block[1];
    int bz = hit->block[2];
    uint16_t cur = world_get_block(w, bx, by, bz);
    if (cur == BLOCK_AIR || cur == BLOCK_BEDROCK) {
        return -1;
    }
    if (!write_world_block(w, bx, by, bz, BLOCK_AIR)) {
        return -1;
    }
    dirty_for_edit(w, bx, bz);
    mark_saved(w, bx, bz);
    const BlockInfo *info = block_get_info(cur);
    LOG_INFO("Block broken: %s at (%d,%d,%d) *particles burst*", info ? info->name : "?", bx, by, bz);
    return (int)cur;
}

/* Test whether a solid block placed at (wx,wy,wz) would overlap the player.
 * A placed solid block is a full unit cube; water/air never overlap.
 */
static bool placement_hits_player(const Player *p, int wx, int wy, int wz, uint16_t id)
{
    if (!block_is_solid(id)) {
        return false;
    }
    float hw = p->width * 0.5f;
    float pmn_x = p->pos.x - hw;
    float pmn_y = p->pos.y;
    float pmn_z = p->pos.z - hw;
    float pmx_x = p->pos.x + hw;
    float pmx_y = p->pos.y + p->height;
    float pmx_z = p->pos.z + hw;
    /* Strict overlap (touching faces is fine). */
    return wx < pmx_x && (wx + 1) > pmn_x && wy < pmx_y && (wy + 1) > pmn_y && wz < pmx_z && (wz + 1) > pmn_z;
}

/* Place a block against the targeted face.
 *
 * Args:
 *   w: world.
 *   p: player (self-overlap check).
 *   hit: raycast result.
 *   block_id: block to place.
 *
 * Returns: true when placed.
 */
bool interaction_place(World *w, const Player *p, const HitResult *hit, uint16_t block_id,
                       uint16_t *out_replaced_block)
{
    if (out_replaced_block != NULL) {
        *out_replaced_block = BLOCK_AIR;
    }
    if (w == NULL || p == NULL || hit == NULL || !hit->hit) {
        return false;
    }
    if (block_id == BLOCK_AIR) {
        return false;
    }
    int hx = hit->block[0];
    int hy = hit->block[1];
    int hz = hit->block[2];
    uint16_t clicked = world_get_block(w, hx, hy, hz);
    int tx = hx + hit->normal[0];
    int ty = hy + hit->normal[1];
    int tz = hz + hit->normal[2];
    /* Small decor is itself the placement surface: replace the clicked
     * cell instead of treating its top face as a shelf for another plant. */
    if (block_is_cross(clicked)) {
        tx = hx;
        ty = hy;
        tz = hz;
    }
    if (ty < 0 || ty >= CHUNK_Y) {
        return false;
    }
    uint16_t cur = world_get_block(w, tx, ty, tz);
    if (!interaction_replaceable(cur)) {
        return false;
    }
    if ((block_id == BLOCK_GRASS_PLANT || block_id == BLOCK_FLOWER) &&
        !interaction_plant_supported(w, tx, ty, tz)) {
        return false;
    }
    if (placement_hits_player(p, tx, ty, tz, block_id)) {
        LOG_INFO("Place rejected: target (%d,%d,%d) overlaps player", tx, ty, tz);
        return false;
    }
    if (!write_world_block(w, tx, ty, tz, block_id)) {
        return false;
    }
    if (out_replaced_block != NULL) {
        *out_replaced_block = cur;
    }
    dirty_for_edit(w, tx, tz);
    mark_saved(w, tx, tz);
    LOG_INFO("Block placed: id %u at (%d,%d,%d)", (unsigned)block_id, tx, ty, tz);
    return true;
}
