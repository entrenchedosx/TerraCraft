#!/usr/bin/env python3
"""Convert owner-supplied mcassets into engine-ready generated assets.

Reads:  mcassets/assets/minecraft/{textures,sounds}/... (owner's local files)
Writes: mcassets/generated/tiles/<stem>.bmp   (16x16 32-bit BMP, alpha kept)
        mcassets/generated/tiles/player_{sleeve,skin}.bmp (crops from the
            Steve skin for the first-person arm, same BMP layout)
        mcassets/generated/sounds/<name>.wav  (22050 Hz mono 16-bit WAV)
        mcassets/generated/mobs/<name>.bmp    (native-size 32-bit BMP: mob
            skins keep their 64x32/64x64 layout so part UVs map 1:1)
        mcassets/generated/menu/menu_font.bmp (Minecraft ASCII bitmap font)
        mcassets/generated/menu/menu_panorama.bmp (stitched title panorama)

The engine loads generated/ with its existing BMP/WAV parsers (no PNG or
Vorbis code in the C build); missing outputs fall back to procedural art
and synthesized sound. Idempotent: outputs rewrite deterministically.

Tile stems match texture_atlas_tile_file(); sound names match the audio
bank convention: {break,place,step}_{stone,grass,gravel,wood,sand,glass}
numbered 1..N, plus pickup, hurt1..3, die, click, tool_break, eat1..3,
craft. Mob skin names match mob_skin_file(). Requires: pillow,
soundfile, numpy.
"""

import os
import sys
import wave

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MC = os.path.join(REPO, "mcassets", "assets", "minecraft")
TEX = os.path.join(MC, "textures")
SND = os.path.join(MC, "sounds")
GEN_TILES = os.path.join(REPO, "mcassets", "generated", "tiles")
GEN_SOUNDS = os.path.join(REPO, "mcassets", "generated", "sounds")
GEN_MOBS = os.path.join(REPO, "mcassets", "generated", "mobs")
GEN_MENU = os.path.join(REPO, "mcassets", "generated", "menu")
DOWNLOADED_MENU = os.path.join(REPO, "mcassets", "downloaded", "menu")

TILE_SIZE = 16
AUDIO_RATE = 22050

# TerraCraft tile stem -> mcassets texture path (relative to textures/).
TILES = {
    "grass_top": "block/grass_block_top.png",
    "grass_side": "block/grass_block_side.png",
    "grass_bottom": "block/dirt.png",
    "dirt": "block/dirt.png",
    "stone": "block/stone.png",
    "sand": "block/sand.png",
    "wood": "block/oak_log.png",
    "wood_top": "block/oak_log_top.png",
    "leaves": "block/oak_leaves.png",
    "glass": "block/glass.png",
    "water": "block/water_still.png",
    "bedrock": "block/bedrock.png",
    "coal_ore": "block/coal_ore.png",
    "iron_ore": "block/iron_ore.png",
    "gold_ore": "block/gold_ore.png",
    "diamond_ore": "block/diamond_ore.png",
    "snow": "block/snow.png",
    "plant": "block/short_grass.png",
    "flower": "block/poppy.png",
    "torch": "block/torch.png",
    "workbench": "block/crafting_table_front.png",
    "workbench_top": "block/crafting_table_top.png",
    "workbench_side": "block/crafting_table_side.png",
    "planks": "block/oak_planks.png",
    "apple": "item/apple.png",
    "stick": "item/stick.png",
    "coal": "item/coal.png",
    "wood_pickaxe": "item/wooden_pickaxe.png",
    "stone_pickaxe": "item/stone_pickaxe.png",
    "wood_axe": "item/wooden_axe.png",
    "stone_axe": "item/stone_axe.png",
    "wood_shovel": "item/wooden_shovel.png",
    "stone_shovel": "item/stone_shovel.png",
    "bow": "item/bow.png",
    "arrow": "item/arrow.png",
    "bone": "item/bone.png",
    "beef": "item/beef.png",
    "leather": "item/leather.png",
    "flesh": "item/rotten_flesh.png",
    "wood_sword": "item/wooden_sword.png",
    "stone_sword": "item/stone_sword.png",
    "iron_pickaxe": "item/iron_pickaxe.png",
    "iron_axe": "item/iron_axe.png",
    "iron_shovel": "item/iron_shovel.png",
    "iron_sword": "item/iron_sword.png",
    "diamond_pickaxe": "item/diamond_pickaxe.png",
    "diamond_axe": "item/diamond_axe.png",
    "diamond_shovel": "item/diamond_shovel.png",
    "diamond_sword": "item/diamond_sword.png",
    "iron_ingot": "item/iron_ingot.png",
    "diamond": "item/diamond.png",
    "tool_pickaxe": "item/wooden_pickaxe.png",
    "tool_axe": "item/wooden_axe.png",
    "tool_shovel": "item/wooden_shovel.png",
    "crack0": "block/destroy_stage_0.png",
    "crack1": "block/destroy_stage_2.png",
    "crack2": "block/destroy_stage_4.png",
    "crack3": "block/destroy_stage_6.png",
    "crack4": "block/destroy_stage_8.png",
    "gravel": "block/gravel.png",
    "sandstone": "block/sandstone.png",
    "deepslate": "block/deepslate.png",
    "copper_ore": "block/copper_ore.png",
    "lapis_ore": "block/lapis_ore.png",
    "redstone_ore": "block/redstone_ore.png",
    "emerald_ore": "block/emerald_ore.png",
    "tuff": "block/tuff.png",
    "granite": "block/granite.png",
    "raw_iron": "block/raw_iron_block.png",
    "raw_copper": "block/raw_copper_block.png",
    "lava": "block/lava_still.png",
}

# Mob skins: generated name -> (source, keep-native-size). Skins keep
# their exact pixel layout (64x32 legacy or 64x64) so engine part UVs
# map 1:1 onto Mojang's regions. "player" stages Steve for the
# first-person arm tiles and the third-person model.
SKINS = {
    "skeleton": "entity/skeleton/skeleton.png",
    "cow": "entity/cow/temperate_cow.png",
    "zombie": "entity/zombie/zombie.png",
    "player": "entity/player/wide/steve.png",
}

# First-person arm tiles: (output stem, crop box on the player skin).
# Classic 64x64 layout, origin top-left: the right-arm front face carries
# the cyan sleeve over a skin hand (bottom rows are bare hand pixels,
# verified flat skin tone with light noise shading).
PLAYER_SKIN_SRC = "entity/player/wide/steve.png"
PLAYER_ARM_CROPS = {
    "player_sleeve": (44, 20, 48, 32),
    "player_skin": (44, 28, 48, 32),
}


def _dig(mat, variants):
    return [("dig/%s%d.ogg" % (mat, i)) for i in variants]

SOUNDS = {}
for _ev in ("break", "place", "step"):
    for _mat, _stem, _n in (("stone", "stone", 4), ("grass", "grass", 4),
                            ("gravel", "gravel", 4), ("wood", "wood", 4),
                            ("sand", "sand", 4), ("glass", "glass", 3),
                            ("snow", "snow", 4)):
        for _i, _src in zip(range(1, _n + 1), _dig(_stem, range(1, _n + 1))):
            if _stem == "glass":
                _src = "random/glass%d.ogg" % _i
            SOUNDS["%s_%s%d" % (_ev, _mat, _i)] = _src
SOUNDS["pickup"] = "random/pop.ogg"
for _i in range(1, 4):
    SOUNDS["hurt%d" % _i] = "damage/hit%d.ogg" % _i
SOUNDS["die"] = "random/classic_hurt.ogg"
SOUNDS["click"] = "random/click.ogg"
SOUNDS["tool_break"] = "random/break.ogg"
for _i in range(1, 4):
    SOUNDS["eat%d" % _i] = "random/eat%d.ogg" % _i
SOUNDS["craft"] = "random/wood_click.ogg"
for _i in range(1, 4):
    SOUNDS["cow_hurt%d" % _i] = "mob/cow/hurt%d.ogg" % _i
# No dedicated cow death file in the set: a hurt moo doubles for death.
SOUNDS["cow_die"] = "mob/cow/hurt1.ogg"
SOUNDS["splash"] = "random/splash.ogg"


# Biome tint bake (M7.1): these tiles ship grayscale and real Minecraft
# multiplies them by biome colors at render time. Our renderer has no
# tint stage, so the converter bakes MC plains colors in once:
# grass green #91BD59 for grass tops/plants, foliage #77AB2F for leaves.
# (User pack BMPs bypass conversion and carry their own colors.)
TINTS = {
    "grass_top": (145, 189, 89),
    "plant": (145, 189, 89),
    "leaves": (119, 171, 47),
}


def convert_tiles():
    from PIL import Image
    import struct
    ok = 0
    missing = []
    for stem, rel in sorted(TILES.items()):
        src = os.path.join(TEX, *rel.split("/"))
        dst = os.path.join(GEN_TILES, stem + ".bmp")
        if not os.path.isfile(src):
            missing.append(rel)
            continue
        img = Image.open(src).convert("RGBA")
        if img.width != TILE_SIZE or img.height < TILE_SIZE or img.height % TILE_SIZE != 0:
            print("  bad size %s: %s" % (rel, img.size))
            missing.append(rel)
            continue
        if img.height != TILE_SIZE:
            # Animated sprite sheet (e.g. water 16x512): take the top frame.
            img = img.crop((0, 0, TILE_SIZE, TILE_SIZE))
        if img.size != (TILE_SIZE, TILE_SIZE):
            img = img.resize((TILE_SIZE, TILE_SIZE), Image.NEAREST)
        if stem in TINTS:
            tr, tg, tb = TINTS[stem]
            tinted = []
            for y in range(TILE_SIZE):
                for x in range(TILE_SIZE):
                    r, g, b, a = img.getpixel((x, y))
                    tinted.append((r * tr // 255, g * tg // 255, b * tb // 255, a))
            img.putdata(tinted)
        # Manual 32-bit BMP write (bottom-up BGRA): Pillow's BMP writer
        # forces alpha to 255, which would flatten water translucency.
        px = img.tobytes()
        body = bytearray()
        for y in range(TILE_SIZE - 1, -1, -1):
            row = px[y * TILE_SIZE * 4:(y + 1) * TILE_SIZE * 4]
            for i in range(0, len(row), 4):
                body += bytes((row[i + 2], row[i + 1], row[i], row[i + 3]))
        hdr = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
        dib = struct.pack("<IIIHHIIIIII", 40, TILE_SIZE, TILE_SIZE, 1, 32, 0,
                          len(body), 0, 0, 0, 0)
        with open(dst, "wb") as f:
            f.write(hdr + dib + bytes(body))
        ok += 1
    return ok, missing


def _write_rgba_bmp(img, dst):
    """Write top-down Pillow RGBA pixels as a bottom-up 32-bit BMP."""
    import struct
    import numpy as np
    rgba = np.asarray(img.convert("RGBA"), dtype=np.uint8)
    height, width, _ = rgba.shape
    bgra = rgba[:, :, [2, 1, 0, 3]]
    body = bgra[::-1].copy().tobytes()
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, width, height, 1, 32, 0,
                      len(body), 0, 0, 0, 0)
    with open(dst, "wb") as f:
        f.write(hdr + dib + body)


def convert_menu_font():
    """Stage the owner-local vanilla ASCII font sheet, preserving its mask."""
    from PIL import Image
    import numpy as np
    rel = "font/ascii.png"
    src = os.path.join(DOWNLOADED_MENU, "ascii.png")
    if not os.path.isfile(src):
        src = os.path.join(TEX, *rel.split("/"))
    if not os.path.isfile(src):
        return False
    img = Image.open(src).convert("RGBA")
    if img.size != (128, 128):
        print("  bad menu font size %s: %s" % (rel, img.size))
        return False
    # Mojangles' glyph alpha is the shape. White RGB allows the renderer to
    # tint it for normal, dimmed, warning, and highlighted menu text.
    pixels = np.asarray(img, dtype=np.uint8).copy()
    pixels[:, :, :3] = 255
    _write_rgba_bmp(Image.fromarray(pixels, "RGBA"),
                    os.path.join(GEN_MENU, "menu_font.bmp"))
    return True


def convert_menu_panorama():
    """Project the local six-face vanilla title cubemap into a 2:1 panorama."""
    from PIL import Image
    import numpy as np
    source_dir = os.path.join(TEX, "gui", "title", "background")
    faces = []
    downloaded_faces = [os.path.join(DOWNLOADED_MENU, "panorama_%d.png" % i) for i in range(6)]
    local_faces = [os.path.join(source_dir, "panorama_%d.png" % i) for i in range(6)]
    face_paths = downloaded_faces if all(os.path.isfile(p) for p in downloaded_faces) else local_faces
    for i in range(6):
        path = face_paths[i]
        if not os.path.isfile(path):
            return False
        img = Image.open(path).convert("RGB")
        if img.width != img.height or (faces and img.size != faces[0].shape[:2][::-1]):
            print("  bad panorama face size: %s" % path)
            return False
        faces.append(np.asarray(img, dtype=np.uint8))

    width, height = 1024, 512
    lon = ((np.arange(width, dtype=np.float32) + 0.5) / width) * (2.0 * np.pi) - np.pi
    lat = np.pi * 0.5 - ((np.arange(height, dtype=np.float32) + 0.5) / height) * np.pi
    cos_lat = np.cos(lat)[:, None]
    dx = cos_lat * np.cos(lon)[None, :]
    dy = np.broadcast_to(np.sin(lat)[:, None], dx.shape)
    dz = cos_lat * np.sin(lon)[None, :]
    ax, ay, az = np.abs(dx), np.abs(dy), np.abs(dz)
    face_index = np.empty(dx.shape, dtype=np.uint8)
    sc = np.empty(dx.shape, dtype=np.float32)
    tc = np.empty(dx.shape, dtype=np.float32)

    # Minecraft's title panorama face sequence runs around the horizon in
    # adjacent order 0,1,2,3, followed by zenith and nadir at 4 and 5.
    # With longitude increasing from -X through -Z, +X, and +Z, preserve
    # that order and mirror the horizontal cube coordinate to match the
    # left/right edges captured in the source images.
    x_major = (ax >= ay) & (ax >= az)
    y_major = (ay > ax) & (ay >= az)
    z_major = ~(x_major | y_major)
    pos_x = x_major & (dx >= 0)
    neg_x = x_major & (dx < 0)
    pos_y = y_major & (dy >= 0)
    neg_y = y_major & (dy < 0)
    pos_z = z_major & (dz >= 0)
    neg_z = z_major & (dz < 0)
    face_index[pos_x], sc[pos_x], tc[pos_x] = 2, dz[pos_x] / ax[pos_x], -dy[pos_x] / ax[pos_x]
    face_index[neg_x], sc[neg_x], tc[neg_x] = 0, -dz[neg_x] / ax[neg_x], -dy[neg_x] / ax[neg_x]
    face_index[pos_y], sc[pos_y], tc[pos_y] = 4, dx[pos_y] / ay[pos_y], dz[pos_y] / ay[pos_y]
    face_index[neg_y], sc[neg_y], tc[neg_y] = 5, dx[neg_y] / ay[neg_y], -dz[neg_y] / ay[neg_y]
    face_index[pos_z], sc[pos_z], tc[pos_z] = 3, -dx[pos_z] / az[pos_z], -dy[pos_z] / az[pos_z]
    face_index[neg_z], sc[neg_z], tc[neg_z] = 1, dx[neg_z] / az[neg_z], -dy[neg_z] / az[neg_z]

    face_size = faces[0].shape[0]
    px = np.clip(((sc + 1.0) * 0.5 * face_size).astype(np.int32), 0, face_size - 1)
    # Source PNG rows are top-down and panorama faces are rendered as camera
    # views (sky at row 0). In the cube projection tc decreases as the view
    # direction moves up, so map it directly to top-down rows here.
    py = np.clip((((tc + 1.0) * 0.5) * face_size).astype(np.int32), 0, face_size - 1)
    panorama = np.empty((height, width, 3), dtype=np.uint8)
    for i, face in enumerate(faces):
        mask = face_index == i
        panorama[mask] = face[py[mask], px[mask]]
    out = Image.fromarray(panorama, "RGB").convert("RGBA")
    _write_rgba_bmp(out, os.path.join(GEN_MENU, "menu_panorama.bmp"))
    return True


def convert_skins():
    """Convert mob skins at native size (no resize, no crop, no tint)."""
    from PIL import Image
    import struct
    ok = 0
    missing = []
    for stem, rel in sorted(SKINS.items()):
        src = os.path.join(TEX, *rel.split("/"))
        dst = os.path.join(GEN_MOBS, stem + ".bmp")
        if not os.path.isfile(src):
            missing.append(rel)
            continue
        img = Image.open(src).convert("RGBA")
        w, h = img.size
        if (w, h) not in ((64, 32), (64, 64)):
            print("  bad skin size %s: %s" % (rel, img.size))
            missing.append(rel)
            continue
        px = img.tobytes()
        body = bytearray()
        for y in range(h - 1, -1, -1):
            row = px[y * w * 4:(y + 1) * w * 4]
            for i in range(0, len(row), 4):
                body += bytes((row[i + 2], row[i + 1], row[i], row[i + 3]))
            # BMP rows pad to 4 bytes; 64 px * 4 B is already aligned.
        hdr = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
        dib = struct.pack("<IIIHHIIIIII", 40, w, h, 1, 32, 0,
                          len(body), 0, 0, 0, 0)
        with open(dst, "wb") as f:
            f.write(hdr + dib + bytes(body))
        ok += 1
    return ok, missing


def convert_player_arm():
    """Crop first-person arm tiles from the player skin (NEAREST upscale)."""
    from PIL import Image
    import struct
    src = os.path.join(TEX, *PLAYER_SKIN_SRC.split("/"))
    if not os.path.isfile(src):
        return 0, [PLAYER_SKIN_SRC]
    img = Image.open(src).convert("RGBA")
    if img.size != (64, 64):
        print("  bad player skin size %s: %s" % (PLAYER_SKIN_SRC, img.size))
        return 0, [PLAYER_SKIN_SRC]
    ok = 0
    missing = []
    for stem, box in sorted(PLAYER_ARM_CROPS.items()):
        crop = img.crop(box).resize((TILE_SIZE, TILE_SIZE), Image.NEAREST)
        px = crop.tobytes()
        body = bytearray()
        for y in range(TILE_SIZE - 1, -1, -1):
            row = px[y * TILE_SIZE * 4:(y + 1) * TILE_SIZE * 4]
            for i in range(0, len(row), 4):
                body += bytes((row[i + 2], row[i + 1], row[i], row[i + 3]))
        hdr = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
        dib = struct.pack("<IIIHHIIIIII", 40, TILE_SIZE, TILE_SIZE, 1, 32, 0,
                          len(body), 0, 0, 0, 0)
        with open(os.path.join(GEN_TILES, stem + ".bmp"), "wb") as f:
            f.write(hdr + dib + bytes(body))
        ok += 1
    return ok, missing


def convert_sounds():
    import numpy as np
    import soundfile as sf
    ok = 0
    missing = []
    for name, rel in sorted(SOUNDS.items()):
        src = os.path.join(SND, *rel.split("/"))
        dst = os.path.join(GEN_SOUNDS, name + ".wav")
        if not os.path.isfile(src):
            missing.append(rel)
            continue
        data, rate = sf.read(src, dtype="float32", always_2d=True)
        mono = data.mean(axis=1)
        if rate != AUDIO_RATE:
            ratio = float(rate) / float(AUDIO_RATE)
            want = max(1, int(len(mono) / ratio + 0.5))
            idx = np.arange(want) * ratio
            i0 = np.floor(idx).astype(np.int64)
            i0 = np.clip(i0, 0, len(mono) - 1)
            i1 = np.clip(i0 + 1, 0, len(mono) - 1)
            frac = idx - np.floor(idx)
            mono = mono[i0] * (1.0 - frac) + mono[i1] * frac
        pcm = np.clip(mono, -1.0, 1.0)
        pcm = (pcm * 32767.0).astype(np.int16)
        with wave.open(dst, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(AUDIO_RATE)
            w.writeframes(pcm.tobytes())
        ok += 1
    return ok, missing


def main():
    os.makedirs(GEN_TILES, exist_ok=True)
    os.makedirs(GEN_SOUNDS, exist_ok=True)
    os.makedirs(GEN_MOBS, exist_ok=True)
    os.makedirs(GEN_MENU, exist_ok=True)
    tiles_ok, tiles_missing = convert_tiles()
    arm_ok, arm_missing = convert_player_arm()
    sounds_ok, sounds_missing = convert_sounds()
    skins_ok, skins_missing = convert_skins()
    menu_font_ok = convert_menu_font()
    menu_panorama_ok = convert_menu_panorama()
    print("tiles: %d converted, %d missing" % (tiles_ok, len(tiles_missing)))
    for m in tiles_missing:
        print("  missing tile source: %s" % m)
    print("player arm: %d converted, %d missing" % (arm_ok, len(arm_missing)))
    for m in arm_missing:
        print("  missing player arm source: %s" % m)
    print("sounds: %d converted, %d missing" % (sounds_ok, len(sounds_missing)))
    for m in sounds_missing:
        print("  missing sound source: %s" % m)
    print("skins: %d converted, %d missing" % (skins_ok, len(skins_missing)))
    for m in skins_missing:
        print("  missing skin source: %s" % m)
    print("menu font: %s" % ("converted" if menu_font_ok else "optional source missing"))
    print("menu panorama: %s" % ("converted" if menu_panorama_ok else "optional source missing"))
    bad = tiles_missing + arm_missing + sounds_missing + skins_missing
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
