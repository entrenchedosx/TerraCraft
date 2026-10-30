#pragma once

/* LAN game-protocol constants and wire codecs (M9+).
 *
 * Single home for message IDs, frame sizes, the handshake version, and
 * the encode/decode routines for the combat/replication messages, so the
 * app handlers and the headless tests execute the SAME code (no drift
 * between tested logic and shipped logic).
 *
 * Wire conventions (match the transport in platform/lan.h):
 * integers big-endian, floats as big-endian IEEE-754 bits, frame size
 * includes the 1-byte type. Old peers reject unknown handshake versions
 * outright; unknown message types are ignored by older builds, which is
 * why combat/replication state requires LAN_PROTOCOL_VERSION 4 (see
 * version policy below).
 */

#include "game/game_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Handshake version. Bumped 3 -> 4 with PvP/health/rules replication:
 * a v3 peer would connect yet silently miss deaths, health, and rules
 * (asymmetric god-mode ghosts), so mismatched versions must disconnect
 * instead of interoperating. */
#define LAN_PROTOCOL_VERSION 4

/* Game message IDs (append-only; never reuse values). */
enum {
    LAN_MSG_HELLO = 1,
    LAN_MSG_WELCOME = 2,
    LAN_MSG_PLAYER = 3,
    LAN_MSG_CHAT = 4,
    LAN_MSG_BLOCK = 5,
    LAN_MSG_PLAYER_LEFT = 6,
    LAN_MSG_GRAVITY_START = 7,
    LAN_MSG_GRAVITY_LAND = 8,
    LAN_MSG_ATTACK = 9,  /* Client->host PvP strike request. */
    LAN_MSG_DAMAGE = 10, /* Host->all authoritative PvP damage. */
    LAN_MSG_HEALTH = 11, /* Peer self-health snapshot (host relays). */
    LAN_MSG_RESPAWN = 12, /* Peer respawn announcement (host relays). */
    LAN_MSG_RULES = 13,  /* Host->all world game rules. */
    LAN_MSG_HELD = 14,   /* Peer held-slot/item snapshot (host relays). */
    LAN_MSG_ATTACK_RANGED = 15, /* Client->host arrow strike request (same body as ATTACK). */
    LAN_MSG_RULES_SET = 16 /* Client->host rule change request (host decides). */
};

/* Exact frame sizes for the fixed-size combat/replication messages. */
#define LAN_MSG_ATTACK_SIZE 13  /* type + victim u32 + damage f32 + seq u32 */
#define LAN_MSG_DAMAGE_SIZE 13  /* type + victim u32 + damage f32 + attacker u32 */
#define LAN_MSG_HEALTH_SIZE 11  /* type + player u32 + health f32 + dead u8 + creative u8 */
#define LAN_MSG_RESPAWN_SIZE 21 /* type + player u32 + x/y/z f32 + health f32 */
#define LAN_MSG_RULES_SIZE 8    /* type + 7 rule bytes */
#define LAN_MSG_HELD_SIZE 8     /* type + player u32 + slot u8 + item u16 */
#define LAN_MSG_ATTACK_RANGED_SIZE 13 /* type + victim u32 + damage f32 + seq u32 */
#define LAN_MSG_RULES_SET_SIZE 8 /* type + 7 rule bytes (same body as RULES) */

/* True when a handshake version interoperates with this build. */
static inline bool lan_version_supported(uint8_t version)
{
    return version == (uint8_t)LAN_PROTOCOL_VERSION;
}

/* Host-side swing-rate floor (seconds): strikes from one attacker closer
 * than this are rejected without consulting damage state. Sits at half
 * the fastest legit weapon cooldown (fist 0.4 s), so real swings never
 * trip it while packet spam cannot outrun the victim hurt window. */
#define LAN_MIN_STRIKE_INTERVAL 0.2

/* Maximum host-accepted attacker->victim distance for arrow strikes
 * (arrows outrange melee; trajectory itself is client-simulated under
 * the trusted-LAN model, but range + solid LOS are host-checked). */
#define LAN_ARROW_VALIDATE_RANGE 64.0f

/* Big-endian scalar helpers (same wire format as the app's net helpers). */
static inline void lan_write_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static inline uint16_t lan_read_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static inline void lan_write_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static inline uint32_t lan_read_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static inline void lan_write_f32(uint8_t *out, float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    lan_write_u32(out, bits);
}

static inline float lan_read_f32(const uint8_t *in)
{
    uint32_t bits = lan_read_u32(in);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* Encode/decode the 7 rule bytes (shared by RULES send/parse). */
static inline void lan_rules_encode(uint8_t out[7], const GameRules *rules)
{
    GameRules def;
    game_rules_defaults(&def);
    const GameRules *r = rules != NULL ? rules : &def;
    out[0] = r->allow_cheats ? 1u : 0u;
    out[1] = r->keep_inventory ? 1u : 0u;
    out[2] = r->mob_spawning ? 1u : 0u;
    out[3] = r->daylight_cycle ? 1u : 0u;
    out[4] = r->natural_regen ? 1u : 0u;
    out[5] = r->fall_damage ? 1u : 0u;
    out[6] = r->pvp ? 1u : 0u;
}

static inline void lan_rules_decode(GameRules *out, const uint8_t in[7])
{
    game_rules_defaults(out);
    if (out == NULL || in == NULL) {
        return;
    }
    out->allow_cheats = in[0] != 0;
    out->keep_inventory = in[1] != 0;
    out->mob_spawning = in[2] != 0;
    out->daylight_cycle = in[3] != 0;
    out->natural_regen = in[4] != 0;
    out->fall_damage = in[5] != 0;
    out->pvp = in[6] != 0;
}

/* ATTACK codec: client strike request. */
static inline void lan_attack_encode(uint8_t out[LAN_MSG_ATTACK_SIZE], uint32_t victim_id,
                                     float damage, uint32_t seq)
{
    out[0] = (uint8_t)LAN_MSG_ATTACK;
    lan_write_u32(out + 1, victim_id);
    lan_write_f32(out + 5, damage);
    lan_write_u32(out + 9, seq);
}

static inline bool lan_attack_decode(const uint8_t *in, size_t size, uint32_t *victim_id,
                                     float *damage, uint32_t *seq)
{
    if (in == NULL || size != LAN_MSG_ATTACK_SIZE || in[0] != (uint8_t)LAN_MSG_ATTACK) {
        return false;
    }
    if (victim_id != NULL) {
        *victim_id = lan_read_u32(in + 1);
    }
    if (damage != NULL) {
        *damage = lan_read_f32(in + 5);
    }
    if (seq != NULL) {
        *seq = lan_read_u32(in + 9);
    }
    return true;
}

/* Ranged ATTACK codec: player-fired arrow strike request. */
static inline void lan_attack_ranged_encode(uint8_t out[LAN_MSG_ATTACK_RANGED_SIZE], uint32_t victim_id,
                                            float damage, uint32_t seq)
{
    out[0] = (uint8_t)LAN_MSG_ATTACK_RANGED;
    lan_write_u32(out + 1, victim_id);
    lan_write_f32(out + 5, damage);
    lan_write_u32(out + 9, seq);
}

static inline bool lan_attack_ranged_decode(const uint8_t *in, size_t size, uint32_t *victim_id,
                                            float *damage, uint32_t *seq)
{
    if (in == NULL || size != LAN_MSG_ATTACK_RANGED_SIZE || in[0] != (uint8_t)LAN_MSG_ATTACK_RANGED) {
        return false;
    }
    if (victim_id != NULL) {
        *victim_id = lan_read_u32(in + 1);
    }
    if (damage != NULL) {
        *damage = lan_read_f32(in + 5);
    }
    if (seq != NULL) {
        *seq = lan_read_u32(in + 9);
    }
    return true;
}

/* DAMAGE codec: authoritative host broadcast. */
static inline void lan_damage_encode(uint8_t out[LAN_MSG_DAMAGE_SIZE], uint32_t victim_id,
                                     float damage, uint32_t attacker_id)
{
    out[0] = (uint8_t)LAN_MSG_DAMAGE;
    lan_write_u32(out + 1, victim_id);
    lan_write_f32(out + 5, damage);
    lan_write_u32(out + 9, attacker_id);
}

static inline bool lan_damage_decode(const uint8_t *in, size_t size, uint32_t *victim_id,
                                     float *damage, uint32_t *attacker_id)
{
    if (in == NULL || size != LAN_MSG_DAMAGE_SIZE || in[0] != (uint8_t)LAN_MSG_DAMAGE) {
        return false;
    }
    if (victim_id != NULL) {
        *victim_id = lan_read_u32(in + 1);
    }
    if (damage != NULL) {
        *damage = lan_read_f32(in + 5);
    }
    if (attacker_id != NULL) {
        *attacker_id = lan_read_u32(in + 9);
    }
    return true;
}

/* HEALTH codec: self-health snapshot plus game-mode bit. The mode byte
 * exists so the host and peers agree on creative immunity; receivers
 * clamp it to their own world mode (modes are world-scoped, so a
 * mismatch is a spoof attempt, never a legitimate difference). */
static inline void lan_health_encode(uint8_t out[LAN_MSG_HEALTH_SIZE], uint32_t player_id,
                                     float health, bool dead, bool creative)
{
    out[0] = (uint8_t)LAN_MSG_HEALTH;
    lan_write_u32(out + 1, player_id);
    lan_write_f32(out + 5, health);
    out[9] = dead ? 1u : 0u;
    out[10] = creative ? 1u : 0u;
}

static inline bool lan_health_decode(const uint8_t *in, size_t size, uint32_t *player_id,
                                     float *health, bool *dead, bool *creative)
{
    if (in == NULL || size != LAN_MSG_HEALTH_SIZE || in[0] != (uint8_t)LAN_MSG_HEALTH) {
        return false;
    }
    if (player_id != NULL) {
        *player_id = lan_read_u32(in + 1);
    }
    if (health != NULL) {
        *health = lan_read_f32(in + 5);
    }
    if (dead != NULL) {
        *dead = in[9] != 0;
    }
    if (creative != NULL) {
        *creative = in[10] != 0;
    }
    return true;
}

/* RESPAWN codec: respawn position + authoritative health in ONE frame so
 * peers converge atomically (never a stale-health alive ghost). */
static inline void lan_respawn_encode(uint8_t out[LAN_MSG_RESPAWN_SIZE], uint32_t player_id, float x,
                                      float y, float z, float health)
{
    out[0] = (uint8_t)LAN_MSG_RESPAWN;
    lan_write_u32(out + 1, player_id);
    lan_write_f32(out + 5, x);
    lan_write_f32(out + 9, y);
    lan_write_f32(out + 13, z);
    lan_write_f32(out + 17, health);
}

static inline bool lan_respawn_decode(const uint8_t *in, size_t size, uint32_t *player_id,
                                      float *x, float *y, float *z, float *health)
{
    if (in == NULL || size != LAN_MSG_RESPAWN_SIZE || in[0] != (uint8_t)LAN_MSG_RESPAWN) {
        return false;
    }
    if (player_id != NULL) {
        *player_id = lan_read_u32(in + 1);
    }
    if (x != NULL) {
        *x = lan_read_f32(in + 5);
    }
    if (y != NULL) {
        *y = lan_read_f32(in + 9);
    }
    if (z != NULL) {
        *z = lan_read_f32(in + 13);
    }
    if (health != NULL) {
        *health = lan_read_f32(in + 17);
    }
    return true;
}

/* HELD codec: held-slot/item snapshot. */
static inline void lan_held_encode(uint8_t out[LAN_MSG_HELD_SIZE], uint32_t player_id, uint8_t slot,
                                   uint16_t item)
{
    out[0] = (uint8_t)LAN_MSG_HELD;
    lan_write_u32(out + 1, player_id);
    out[5] = slot;
    lan_write_u16(out + 6, item);
}

static inline bool lan_held_decode(const uint8_t *in, size_t size, uint32_t *player_id,
                                   uint8_t *slot, uint16_t *item)
{
    if (in == NULL || size != LAN_MSG_HELD_SIZE || in[0] != (uint8_t)LAN_MSG_HELD) {
        return false;
    }
    if (player_id != NULL) {
        *player_id = lan_read_u32(in + 1);
    }
    if (slot != NULL) {
        *slot = in[5];
    }
    if (item != NULL) {
        *item = lan_read_u16(in + 6);
    }
    return true;
}
