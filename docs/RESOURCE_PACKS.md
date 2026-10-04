# Resource packs

TerraCraft uses three texture and sound layers. Each layer replaces only
the entries it provides; missing or invalid files keep the lower layer.

1. Procedural textures and synthesized sounds, always available.
2. Optional owner-local converted assets under `mcassets/generated/`.
3. The selected user pack under `resourcepacks/<PackName>/`.

No source-game assets are included in the GitHub repository. The optional
converter operates only on files already supplied locally by the owner.
It requires Python packages Pillow, SoundFile, and NumPy; the game itself
does not need Python or image/audio codec libraries.

## Owner-local assets

When an owner supplies a local asset tree, run:

```sh
python tools/convert_mcassets.py
```

The converter writes engine-ready BMP/WAV files and mob skins under
`mcassets/generated/`. That directory is ignored by Git and is not required
for building or running a fresh checkout. The build copies generated files
beside the executable when the local directory exists. The game logs the
number of tiles loaded; missing assets use procedural/synthesized fallbacks.

## User pack layout

```text
resourcepacks/
  MyPack/
    tiles/
      grass_top.bmp
      stone.bmp
      apple.bmp
    sounds/
      break.wav
      bow_fire.wav
```

Choose a pack in **Settings → Resource Pack**. The selection is saved in
`config/settings.cfg`. `Default` means procedural textures plus any
owner-local layer.

### Texture tiles

The current atlas exposes 46 named tiles. Each tile is a 16×16 BMP; files
may be omitted individually.

```text
grass_top.bmp       grass_side.bmp      dirt.bmp             stone.bmp
sand.bmp            wood.bmp            leaves.bmp           glass.bmp
water.bmp           bedrock.bmp         coal_ore.bmp         iron_ore.bmp
gold_ore.bmp        diamond_ore.bmp     snow.bmp             grass_bottom.bmp
plant.bmp           flower.bmp          torch.bmp            tool_pickaxe.bmp
tool_axe.bmp        tool_shovel.bmp     coal.bmp             crack0.bmp
crack1.bmp          crack2.bmp          crack3.bmp            crack4.bmp
workbench.bmp       planks.bmp          apple.bmp            stick.bmp
wood_top.bmp        workbench_top.bmp  workbench_side.bmp   wood_pickaxe.bmp
stone_pickaxe.bmp   wood_axe.bmp        stone_axe.bmp        wood_shovel.bmp
stone_shovel.bmp    bow.bmp             arrow.bmp             bone.bmp
beef.bmp            leather.bmp
```

The source of truth for names and item mappings is
`texture_atlas_tile_file()` in `src/render/texture_atlas.c` and the item
registry in `src/game/item.c`.

### Sound events

Optional WAV overrides use these event names:

```text
break.wav       place.wav       pickup.wav       hurt.wav
die.wav         click.wav       tool_break.wav   eat.wav
craft.wav       step_stone.wav  step_dirt.wav    step_wood.wav
step_sand.wav   mob_hurt.wav    mob_die.wav      bow_draw.wav
bow_fire.wav    arrow_stick.wav
```

Each pack sound replaces that event. Break, place, and step events use the
pack WAV for every matching material. Without a WAV override, built-in
synthesized sounds remain.

## File requirements

Textures must be uncompressed Windows BMP files with a 40-byte
`BITMAPINFOHEADER`, 16×16 pixels, and 24-bit BGR or 32-bit BGRA data.
Bottom-up and top-down rows are supported. Alpha from 32-bit tiles is kept
for cutout and translucent textures. Invalid files are skipped and the
procedural tile remains.

Sounds must be RIFF/WAVE PCM, 8- or 16-bit, mono or stereo. Accepted files
are capped at 4 MiB and 10 seconds, then converted to the mixer's format.
Invalid files leave the built-in sound active.

The atlas uses nearest-neighbor filtering and half-texel inset UVs. This
keeps 16×16 tile edges crisp and prevents sampling neighboring atlas tiles.
