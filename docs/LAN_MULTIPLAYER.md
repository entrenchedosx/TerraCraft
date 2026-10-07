# LAN Multiplayer

TerraCraft's LAN mode connects game clients directly to a world hosted by one
player. The host runs the world simulation; no dedicated server, matchmaking
service, or internet relay is used.

## Start a session

1. Start TerraCraft and choose a username. On first launch, leave the field
   blank to receive a generated two-word name with a four-digit suffix.
2. Create or open a world and enter it.
3. Press `Esc` and choose **Open to LAN**. The host remains in the world.
4. On each joining computer, choose **Multiplayer (LAN)** from the title
   menu and enter the host computer's local IPv4 address.
5. Press `T` to open chat, type a message, and press `Enter` to send. Press
   `Esc` to close chat without sending.

All computers must be on the same local network and able to reach the host.
The game listens on TCP port `25566`. Enter the address assigned to the host
on that network, not `127.0.0.1` (that address refers to the joining computer
itself). The operating system's network settings show the host's IPv4 address.
The host accepts local IPv4 ranges (private, link-local, or loopback) and
rejects routed public addresses. The join screen applies the same check before
it starts a connection.

The transport supports up to eight joining peers. Each player's local profile
is saved in `config/profile.cfg`; it is not tied to a world save.

## What is shared

- Player position, facing direction, and simple walk/sneak state.
- Chat messages, sent with `T` and displayed to connected players.
- Block changes made while the players are connected. The host applies client
  edits and relays accepted changes, so the host's open world is authoritative.
- Falling-block start and landing events. Clients predict motion between those
  events. A LAND frame includes the authoritative destination cell so the
  client can reconcile and apply the landing as one reliable message. If the
  host's bounded outbox is full, it delays the transition and retries instead
  of dropping the event.
- The host's seed, terrain-generation version, game mode, player position,
  and time of day when a client joins. The client creates a local save copy
  and generates the same unmodified terrain.

## Current limits

- A joining client does not receive block edits made before it connected.
- Mobs, dropped items, player inventories, health, hunger, and other entity
  state are not synchronized. These remain local to each game.
- Chat currently accepts printable ASCII only and is limited to 160
  characters.
- The connection uses direct, unencrypted TCP intended for trusted local
  networks. There is no account authentication or protection from a hostile
  peer on the same local network.
- Falling blocks do not send continuous position snapshots; client animation
  can drift slightly from the host between lifecycle events.
- The host uses client-reported player positions for rendering and block-edit
  reach checks; it does not simulate or validate client movement. Treat LAN
  peers as trusted.
- If the host closes the world or disconnects, clients return to the title
  screen. Their local generated world copy remains saved under `saves/`.

These behaviors are the implemented LAN scope, not a claim of complete
Minecraft multiplayer compatibility.
