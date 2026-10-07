# World fluids and falling blocks

This document describes the current fixed-step water and gravity systems.
It records concrete rules and limits so later work can extend the engine
without presenting unfinished behavior as Minecraft parity.

## Simulation ownership and timing

World updates use the existing 60 Hz `SimulationClock`; render frame rate
does not schedule fluid or gravity work. Menus that freeze world simulation
also freeze these systems. Fluid uses a 0.25-second scheduled interval, while
falling blocks integrate at 60 Hz with a bounded work budget. Queue entries
store integer world coordinates, never chunk pointers, so streaming cannot
leave a stale reference in either scheduler.

In LAN play the host advances canonical water and sends its normal block
changes. For falling blocks, the host also sends ordered start and landing
events. A client starts its local visual prediction when a start arrives,
and the START frame clears the source cell itself. A client pauses the
predicted block at its landing cell until the host's
landing event arrives. That single reliable frame contains the final block
cell as well as the lifecycle event, so queue pressure cannot split the
fall-complete notification from its canonical block update. Predicted motion
never echoes a canonical block change back to the host. Client saves and chunk
unloads write a local landing for a complete snapshot while keeping a tagged
pending prediction; if the host later reports a different landing, the client
removes its saved copy before applying the authoritative cell. This
synchronizes lifecycle events, not continuous position snapshots, so small
differences in the falling path can still be visible between those events.
If the host's bounded LAN outbox is full, it leaves a block at its source or
holds it at the landing until the corresponding event can be queued; it does
not silently commit an unreplicated transition.

## Water

Water uses stable block IDs for a full source, seven horizontal flow levels,
and a full-height falling column. A coordinate queue deduplicates pending
cells with per-chunk bits. It is capped at 32,768 records and processes at
most 1,024 cells per 0.25-second interval. When full, missed cells are marked
in a second per-chunk bitset and recovered incrementally (4,096 scan units
per interval). This keeps saturation recovery bounded and avoids sweeping
all loaded block arrays. Duplicate queue notifications do not trigger a
rescan.

At a fluid update, water first tries the cell below. If it cannot move down,
it spreads horizontally with a discrete level that loses one step per cell.
Unsupported horizontal levels retract when their source disappears. Two
sources over a solid floor can refill the middle cell. Only loaded chunks
participate; when a neighbor is adopted, water at the shared edge is woken
again. Chunk add/remove keeps the iteration array compact and marks the
remaining neighbor mesh dirty.

Meshing uses the water block's actual discrete height and exposes height
steps between unequal adjacent surfaces. Player, mob, and dropped-item
physics use AABB overlap with that surface to scale drag and buoyancy. Water
remains non-colliding. The movement responses are simple rather than a full
swimming controller.

## Falling blocks

`BlockInfo.gravity_affected` identifies gravity blocks; sand is currently the
only registered block with this property. New block types can opt in through
metadata instead of changing the physics loop. A bounded 8,192-cell queue
deduplicates updates with per-chunk bits. Block edits wake the changed cell
and the cell above it, and newly adopted chunks seed their gravity blocks.
When an unsupported block is processed, the engine allocates a slot in a
256-record world-owned pool before clearing its saved cell.

Each record stores the block ID, fixed X/Z column, continuous Y position,
and vertical velocity. Gravity integrates at 60 Hz, clamps terminal speed,
and checks every crossed voxel so catch-up steps cannot tunnel through full
solid blocks. Water and crossed-plant blocks are passable; a falling block
replaces the water/plant cell when it lands. Landing goes through the common
world setter, which updates meshes, persistence flags, water scheduling, and
LAN replication.

Falling records contain no chunk pointers and are not written as temporary
entities. Before a chunk unload or world save, each active record is safely
settled to the first valid landing position in its column. On a LAN client,
save/unload settling is local-only and cannot emit authoritative host events;
its pending record remains available to reconcile the saved position when the
host event arrives.
If a block cannot be placed, the chunk is retained or the save fails instead
of discarding it. The rendered falling cube uses preallocated renderer scratch
storage and remains visible while inventory or crafting is open;
it is separate from collectible item entities and has no pickup or despawn
timer.

## Current limits

- Water is the only fluid. Lava, water/lava reactions, waterlogged shapes,
  current forces, bubbles, drowning, air supply, and underwater post effects
  are not implemented.
- Fluid movement treats every non-solid block as replaceable; partial block
  shapes such as slabs, stairs, fences, and trapdoors do not exist in the
  current block registry.
- Water surfaces use discrete levels without waves or refraction. Transparent
  sorting retains the existing chunk-level ordering limitation.
- Sand is the only gravity block. Gravel, concrete powder, anvils, and their
  material-specific landing effects are not registered.
- Falling records use a bounded pool. If all 256 slots are occupied, remaining
  unsupported blocks stay canonical and wait in the bounded scheduler until
  a slot becomes available.
- Falling blocks have continuous position and collision but no dust/landing
  particles or material-specific landing sound yet.
- Client falling motion is prediction between host start/landing events; the
  host does not send continuous falling-entity position snapshots.

## Regression coverage

The headless suite covers source and horizontal flow, chunk-edge wakeup,
queue capacity/recovery, fractional water heights, save-version compatibility,
shallow versus source-water player response, and mob/drop damping. Gravity
tests cover registry metadata, supported blocks, falling and landing, stack
chain reactions, falling through water, observable deferred-queue recovery,
conversion to canonical chunk state, host/client prediction handoff including
correction of a saved client landing, pool-full client start handling,
retry-after-outbox-pressure, and chunk-array compaction with cardinal and
diagonal AO remeshing. These checks validate
CPU-side state and geometry contracts; they do not replace a human playtest of
water appearance, falling animation, or multiplayer timing.
