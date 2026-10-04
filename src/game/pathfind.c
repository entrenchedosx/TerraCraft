#include "game/pathfind.h"
#include "world/block.h"
#include "world/chunk.h"
#include "world/world.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/* Search lattice dimensions (Chebyshev radius 16, y band +-6). */
#define PF_RX (PATHFIND_RADIUS * 2 + 1)
#define PF_RY 13
#define PF_RZ (PATHFIND_RADIUS * 2 + 1)
#define PF_NODES ((size_t)PF_RX * PF_RY * PF_RZ)
#define PF_INF 1e30f

/* Heap entry (binary min-heap by f, ties broken by node index). */
typedef struct PfHeapItem {
    int node;
    float f;
} PfHeapItem;

/* Scratch workspace (one malloc per call, freed before return). */
typedef struct PfWork {
    float *g;
    int *came;
    unsigned char *closed;
    PfHeapItem *heap;
    int heap_n;
} PfWork;

/* Node index from lattice coords, or -1 when outside the lattice/world. */
static int pf_index(int lx, int ly, int lz)
{
    if (lx < 0 || lx >= PF_RX || ly < 0 || ly >= PF_RY || lz < 0 || lz >= PF_RZ) {
        return -1;
    }
    return (ly * PF_RZ + lz) * PF_RX + lx;
}

/* Standable feet cell: feet + head clear, solid ground directly below. */
static bool pf_standable(const World *w, int x, int y, int z)
{
    if (y < 1 || y + 1 >= CHUNK_Y) {
        return false;
    }
    if (block_is_solid(world_get_block(w, x, y, z))) {
        return false;
    }
    if (block_is_solid(world_get_block(w, x, y + 1, z))) {
        return false;
    }
    return block_is_solid(world_get_block(w, x, y - 1, z));
}

/* Heap push (min-heap on f, then node). */
static void pf_push(PfWork *wk, int node, float f)
{
    int i = wk->heap_n++;
    wk->heap[i].node = node;
    wk->heap[i].f = f;
    while (i > 0) {
        int parent = (i - 1) / 2;
        bool less = wk->heap[i].f < wk->heap[parent].f ||
                    (wk->heap[i].f == wk->heap[parent].f && wk->heap[i].node < wk->heap[parent].node);
        if (!less) {
            break;
        }
        PfHeapItem tmp = wk->heap[i];
        wk->heap[i] = wk->heap[parent];
        wk->heap[parent] = tmp;
        i = parent;
    }
}

/* Heap pop (returns -1 when empty). */
static int pf_pop(PfWork *wk)
{
    if (wk->heap_n <= 0) {
        return -1;
    }
    int top = wk->heap[0].node;
    wk->heap_n--;
    wk->heap[0] = wk->heap[wk->heap_n];
    int i = 0;
    for (;;) {
        int left = i * 2 + 1;
        int right = left + 1;
        int smallest = i;
        if (left < wk->heap_n &&
            (wk->heap[left].f < wk->heap[smallest].f ||
             (wk->heap[left].f == wk->heap[smallest].f && wk->heap[left].node < wk->heap[smallest].node))) {
            smallest = left;
        }
        if (right < wk->heap_n &&
            (wk->heap[right].f < wk->heap[smallest].f ||
             (wk->heap[right].f == wk->heap[smallest].f && wk->heap[right].node < wk->heap[smallest].node))) {
            smallest = right;
        }
        if (smallest == i) {
            break;
        }
        PfHeapItem tmp = wk->heap[i];
        wk->heap[i] = wk->heap[smallest];
        wk->heap[smallest] = tmp;
        i = smallest;
    }
    return top;
}

/* Manhattan distance (admissible: minimum edge cost is 1). */
static float pf_heuristic(int x0, int y0, int z0, int x1, int y1, int z1)
{
    int dx = x0 - x1;
    int dy = y0 - y1;
    int dz = z0 - z1;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    if (dz < 0) {
        dz = -dz;
    }
    return (float)(dx + dy + dz);
}

/* Find a ground path (see header for the contract). */
int pathfind_ground(const World *w, int sx, int sy, int sz, int tx, int ty, int tz,
                    int out_xyz[][3], int cap)
{
    if (w == NULL || out_xyz == NULL || cap < 1 || cap > PATHFIND_MAX_LEN) {
        return -1;
    }
    if (sx == tx && sy == ty && sz == tz) {
        return 0;
    }
    /* Target must be inside the lattice (bounded search by construction). */
    int tlx = tx - (sx - PATHFIND_RADIUS);
    int tly = ty - (sy - 6);
    int tlz = tz - (sz - PATHFIND_RADIUS);
    if (pf_index(tlx, tly, tlz) < 0) {
        return -1;
    }
    if (!pf_standable(w, sx, sy, sz) || !pf_standable(w, tx, ty, tz)) {
        return -1;
    }
    PfWork wk;
    wk.g = (float *)malloc(PF_NODES * sizeof(float));
    wk.came = (int *)malloc(PF_NODES * sizeof(int));
    wk.closed = (unsigned char *)malloc(PF_NODES * sizeof(unsigned char));
    wk.heap = (PfHeapItem *)malloc(PF_NODES * sizeof(PfHeapItem));
    wk.heap_n = 0;
    if (wk.g == NULL || wk.came == NULL || wk.closed == NULL || wk.heap == NULL) {
        free(wk.g);
        free(wk.came);
        free(wk.closed);
        free(wk.heap);
        return -1;
    }
    for (size_t i = 0; i < PF_NODES; ++i) {
        wk.g[i] = PF_INF;
        wk.came[i] = -1;
        wk.closed[i] = 0;
    }
    int start = pf_index(PATHFIND_RADIUS, 6, PATHFIND_RADIUS);
    int goal = pf_index(tlx, tly, tlz);
    wk.g[start] = 0.0f;
    pf_push(&wk, start, pf_heuristic(sx, sy, sz, tx, ty, tz));
    /* Neighbor steps: 4 directions x {level, up, down} (fixed order). */
    static const int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int DYS[3] = {0, 1, -1};
    int expanded = 0;
    bool found = false;
    while (wk.heap_n > 0) {
        int cur = pf_pop(&wk);
        if (cur < 0 || wk.closed[cur]) {
            continue;
        }
        wk.closed[cur] = 1;
        if (++expanded > PATHFIND_MAX_EXPAND) {
            break;
        }
        if (cur == goal) {
            found = true;
            break;
        }
        int clx = cur % PF_RX;
        int clz = (cur / PF_RX) % PF_RZ;
        int cly = cur / (PF_RX * PF_RZ);
        int cx = sx - PATHFIND_RADIUS + clx;
        int cy = sy - 6 + cly;
        int cz = sz - PATHFIND_RADIUS + clz;
        for (int d = 0; d < 4; ++d) {
            for (int k = 0; k < 3; ++k) {
                int nx = cx + DIRS[d][0];
                int ny = cy + DYS[k];
                int nz = cz + DIRS[d][1];
                int nlx = nx - (sx - PATHFIND_RADIUS);
                int nly = ny - (sy - 6);
                int nlz = nz - (sz - PATHFIND_RADIUS);
                int ni = pf_index(nlx, nly, nlz);
                if (ni < 0 || wk.closed[ni]) {
                    continue;
                }
                if (!pf_standable(w, nx, ny, nz)) {
                    continue;
                }
                float step = (DYS[k] == 0) ? 1.0f : 1.5f;
                float ng = wk.g[cur] + step;
                if (ng < wk.g[ni]) {
                    wk.g[ni] = ng;
                    wk.came[ni] = cur;
                    pf_push(&wk, ni, ng + pf_heuristic(nx, ny, nz, tx, ty, tz));
                }
            }
        }
    }
    int len = -1;
    if (found) {
        /* Walk the came-chain into a temp buffer, then emit the
         * start-side prefix (a capped path still begins with the first
         * step to take; the mob repaths on cooldown for the rest). */
        int tmp[256][3];
        int total = 0;
        int cur = goal;
        while (cur != start && total < 256) {
            int clx = cur % PF_RX;
            int clz = (cur / PF_RX) % PF_RZ;
            int cly = cur / (PF_RX * PF_RZ);
            tmp[total][0] = sx - PATHFIND_RADIUS + clx;
            tmp[total][1] = sy - 6 + cly;
            tmp[total][2] = sz - PATHFIND_RADIUS + clz;
            ++total;
            cur = wk.came[cur];
            if (cur < 0) {
                break;
            }
        }
        if (cur == start) {
            len = total < cap ? total : cap;
            for (int i = 0; i < len; ++i) {
                out_xyz[i][0] = tmp[total - 1 - i][0];
                out_xyz[i][1] = tmp[total - 1 - i][1];
                out_xyz[i][2] = tmp[total - 1 - i][2];
            }
        }
    }
    free(wk.g);
    free(wk.came);
    free(wk.closed);
    free(wk.heap);
    return len;
}
