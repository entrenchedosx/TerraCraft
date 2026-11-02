#include "world/jrandom.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define LEGACY_MASK 0xFFFFFFFFFFFFULL
#define LEGACY_MULT 0x5DEECE66DULL
#define LEGACY_INC 0xBULL

static uint64_t rotl64(uint64_t v, int n)
{
    return (v << n) | (v >> (64 - n));
}

/* RandomSupport.mixStafford13. */
static uint64_t mix_stafford13(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ---- Legacy (java.util.Random) ------------------------------------------ */

void jr_legacy_init(JrRandom *r, int64_t seed)
{
    r->kind = JR_LEGACY;
    r->a = ((uint64_t)seed ^ LEGACY_MULT) & LEGACY_MASK;
    r->b = 0;
    r->next_gaussian = 0.0;
    r->have_gaussian = false;
}

static int32_t legacy_next(JrRandom *r, int bits)
{
    r->a = (r->a * LEGACY_MULT + LEGACY_INC) & LEGACY_MASK;
    return (int32_t)(uint32_t)(r->a >> (48 - bits)); /* bits <= 32: value fits uint32 */
}

/* ---- Xoroshiro128++ ------------------------------------------------------ */

static void xoro_fix_zero(JrRandom *r)
{
    if ((r->a | r->b) == 0) {
        r->a = 0x9E3779B97F4A7C15ULL; /* -7046029254386353131 */
        r->b = 0x6A09E667F3BCC909ULL; /* 7640891576956012809 */
    }
}

void jr_xoroshiro_init128(JrRandom *r, int64_t seed_lo, int64_t seed_hi)
{
    r->kind = JR_XOROSHIRO;
    r->a = (uint64_t)seed_lo;
    r->b = (uint64_t)seed_hi;
    r->next_gaussian = 0.0;
    r->have_gaussian = false;
    xoro_fix_zero(r);
}

/* RandomSupport.upgradeSeedTo128bit. */
static void upgrade_seed(int64_t seed, uint64_t *lo, uint64_t *hi)
{
    uint64_t l = (uint64_t)seed ^ 0x6A09E667F3BCC909ULL;
    uint64_t m = l + 0x9E3779B97F4A7C15ULL;
    *lo = mix_stafford13(l);
    *hi = mix_stafford13(m);
}

void jr_xoroshiro_init(JrRandom *r, int64_t seed)
{
    uint64_t lo, hi;
    upgrade_seed(seed, &lo, &hi);
    jr_xoroshiro_init128(r, (int64_t)lo, (int64_t)hi);
}

static uint64_t xoro_next_long(JrRandom *r)
{
    uint64_t l = r->a;
    uint64_t l1 = r->b;
    uint64_t out = rotl64(l + l1, 17) + l;
    l1 ^= l;
    r->a = rotl64(l, 49) ^ l1 ^ (l1 << 21);
    r->b = rotl64(l1, 28);
    return out;
}

/* ---- RandomSource API ---------------------------------------------------- */

int32_t jr_next_int(JrRandom *r)
{
    if (r->kind == JR_LEGACY) {
        return legacy_next(r, 32);
    }
    return (int32_t)(uint32_t)xoro_next_long(r);
}

int32_t jr_next_int_bound(JrRandom *r, int32_t bound)
{
    if (bound <= 0) {
        return 0;
    }
    if (r->kind == JR_LEGACY) {
        if ((bound & (bound - 1)) == 0) {
            return (int32_t)(((int64_t)bound * (int64_t)legacy_next(r, 31)) >> 31);
        }
        int32_t i, j;
        do {
            i = legacy_next(r, 31);
            j = i % bound;
        } while ((int32_t)((uint32_t)i - (uint32_t)j + (uint32_t)(bound - 1)) < 0);
        return j;
    }
    uint64_t l = (uint32_t)jr_next_int(r);
    uint64_t m = l * (uint64_t)(uint32_t)bound;
    uint64_t n = m & 0xFFFFFFFFULL;
    if (n < (uint64_t)(uint32_t)bound) {
        uint32_t ub = (uint32_t)bound;
        uint32_t i = (uint32_t)(~ub + 1u) % ub; /* Integer.remainderUnsigned(~bound + 1, bound) */
        while (n < (uint64_t)i) {
            l = (uint32_t)jr_next_int(r);
            m = l * (uint64_t)ub;
            n = m & 0xFFFFFFFFULL;
        }
    }
    return (int32_t)(m >> 32);
}

int64_t jr_next_long(JrRandom *r)
{
    if (r->kind == JR_LEGACY) {
        int32_t i = legacy_next(r, 32);
        int32_t j = legacy_next(r, 32);
        return (int64_t)(((uint64_t)(int64_t)i << 32) + (uint64_t)(int64_t)j);
    }
    return (int64_t)xoro_next_long(r);
}

bool jr_next_boolean(JrRandom *r)
{
    if (r->kind == JR_LEGACY) {
        return legacy_next(r, 1) != 0;
    }
    return (xoro_next_long(r) & 1ULL) != 0;
}

float jr_next_float(JrRandom *r)
{
    if (r->kind == JR_LEGACY) {
        return (float)legacy_next(r, 24) * 5.9604645E-8f;
    }
    return (float)(xoro_next_long(r) >> 40) * 5.9604645E-8f;
}

double jr_next_double(JrRandom *r)
{
    if (r->kind == JR_LEGACY) {
        uint64_t hi = (uint64_t)(uint32_t)legacy_next(r, 26);
        uint64_t lo = (uint64_t)(uint32_t)legacy_next(r, 27);
        return (double)((hi << 27) + lo) * 1.1102230246251565E-16;
    }
    return (double)(xoro_next_long(r) >> 11) * 1.1102230246251565E-16;
}

/* MarsagliaPolarGaussian. */
double jr_next_gaussian(JrRandom *r)
{
    if (r->have_gaussian) {
        r->have_gaussian = false;
        return r->next_gaussian;
    }
    double d, e, f;
    do {
        d = 2.0 * jr_next_double(r) - 1.0;
        e = 2.0 * jr_next_double(r) - 1.0;
        f = d * d + e * e;
    } while (f >= 1.0 || f == 0.0);
    double g = sqrt(-2.0 * log(f) / f);
    r->next_gaussian = e * g;
    r->have_gaussian = true;
    return d * g;
}

/* ---- Positional factories ------------------------------------------------ */

JrPositional jr_fork_positional(JrRandom *r)
{
    JrPositional f;
    f.kind = r->kind;
    if (r->kind == JR_LEGACY) {
        f.a = (uint64_t)jr_next_long(r);
        f.b = 0;
    } else {
        f.a = (uint64_t)jr_next_long(r);
        f.b = (uint64_t)jr_next_long(r);
    }
    return f;
}

int64_t jr_mth_get_seed(int32_t x, int32_t y, int32_t z)
{
    uint64_t xs = (uint64_t)(int64_t)(int32_t)((uint32_t)x * 3129871u);
    uint64_t zs = (uint64_t)(int64_t)z * 116129781ULL;
    uint64_t l = xs ^ zs ^ (uint64_t)(int64_t)y;
    l = l * l * 42317861ULL + l * 11ULL;
    return (int64_t)l >> 16;
}

int32_t jr_java_string_hash(const char *s)
{
    uint32_t h = 0;
    for (; *s != '\0'; ++s) {
        h = h * 31u + (uint32_t)(unsigned char)*s;
    }
    return (int32_t)h;
}

/* ---- MD5 (RandomSupport.seedFromHashOf uses Guava Hashing.md5()) ---------- */

typedef struct Md5 {
    uint32_t s[4];
    uint8_t buf[64];
    uint64_t len;
} Md5;

static uint32_t rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void md5_block(Md5 *m, const uint8_t *p)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t w[16];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) | ((uint32_t)p[i * 4 + 2] << 16) |
               ((uint32_t)p[i * 4 + 3] << 24);
    }
    uint32_t a = m->s[0], b = m->s[1], c = m->s[2], d = m->s[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + K[i] + w[g], R[i]);
        a = t;
    }
    m->s[0] += a;
    m->s[1] += b;
    m->s[2] += c;
    m->s[3] += d;
}

static void md5_ascii(const char *text, uint8_t out[16])
{
    Md5 m;
    m.s[0] = 0x67452301;
    m.s[1] = 0xefcdab89;
    m.s[2] = 0x98badcfe;
    m.s[3] = 0x10325476;
    size_t n = strlen(text);
    uint64_t bits = (uint64_t)n * 8u;
    size_t off = 0;
    while (n - off >= 64) {
        md5_block(&m, (const uint8_t *)text + off);
        off += 64;
    }
    uint8_t tail[128];
    size_t rem = n - off;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, text + off, rem);
    tail[rem] = 0x80;
    size_t total = rem + 1 + 8 <= 64 ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        tail[total - 8 + (size_t)i] = (uint8_t)(bits >> (8 * i));
    }
    md5_block(&m, tail);
    if (total == 128) {
        md5_block(&m, tail + 64);
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = (uint8_t)(m.s[i] >> (8 * j));
        }
    }
}

/* RandomSupport.seedFromHashOf: MD5 digest, two big-endian longs. */
static void seed_from_hash_of(const char *name, uint64_t *lo, uint64_t *hi)
{
    uint8_t d[16];
    md5_ascii(name, d);
    uint64_t a = 0, b = 0;
    for (int i = 0; i < 8; ++i) {
        a = (a << 8) | d[i];
        b = (b << 8) | d[8 + i];
    }
    *lo = a;
    *hi = b;
}

JrRandom jr_pos_at(const JrPositional *f, int32_t x, int32_t y, int32_t z)
{
    JrRandom r;
    uint64_t s = (uint64_t)jr_mth_get_seed(x, y, z);
    if (f->kind == JR_LEGACY) {
        jr_legacy_init(&r, (int64_t)(s ^ f->a));
    } else {
        jr_xoroshiro_init128(&r, (int64_t)(s ^ f->a), (int64_t)f->b);
    }
    return r;
}

JrRandom jr_pos_from_hash_of(const JrPositional *f, const char *name)
{
    JrRandom r;
    if (f->kind == JR_LEGACY) {
        uint64_t h = (uint64_t)(int64_t)jr_java_string_hash(name);
        jr_legacy_init(&r, (int64_t)(h ^ f->a));
    } else {
        uint64_t lo, hi;
        seed_from_hash_of(name, &lo, &hi);
        jr_xoroshiro_init128(&r, (int64_t)(lo ^ f->a), (int64_t)(hi ^ f->b));
    }
    return r;
}

JrRandom jr_pos_from_seed(const JrPositional *f, int64_t seed)
{
    JrRandom r;
    if (f->kind == JR_LEGACY) {
        jr_legacy_init(&r, seed);
    } else {
        /* XoroshiroPositionalRandomFactory.fromSeed: new Xoroshiro(seed ^ lo, seed ^ hi),
         * read from the 1.21.11 bytecode (no seed upgrade). */
        jr_xoroshiro_init128(&r, (int64_t)((uint64_t)seed ^ f->a), (int64_t)((uint64_t)seed ^ f->b));
    }
    return r;
}
