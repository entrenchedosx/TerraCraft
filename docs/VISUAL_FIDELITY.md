# Standard Java 1.21.11 visual pass

The selected target is standard Minecraft Java Edition 1.21.11 graphics.
The supplied reference screenshots also contain enhanced shader effects;
this pass follows the standard renderer's visual rules.

## Implemented

- A 16 x 16 lightmap using the local release's `lightmap.fsh` formula,
  default brightness 50%, stable block-light factor 1.5, and the release's
  independent skylight color/factor timelines. Face shading is fixed by
  orientation, with separate entity diffuse lighting for skins and hands.
- Sky, cloud, fog and star timing from `data/minecraft/timeline/day.json`.
  The celestial curve uses its cubic Bezier control points. Local native
  sun/moon/cloud art replaces the smooth procedural disks/cloud noise.
- Fancy cutout leaves preserve interior faces. Leaves attenuate propagated
  light without acting as solid ambient-occlusion walls. Plant quads use
  the reference cross-model width and deterministic Java-style X/Z offsets.
- Five-by-five biome color blending, using colormap samples for the seven
  existing biome categories, and a separate tinted grass-side overlay.
- Blue water with the default `#3f76e4` tint and 20 Hz fluid animation.
  Animation respects source frame durations/interpolation, custom static
  pack tiles, and single-player pause. Fallback fluid art also animates.
  Surface faces render from below, with the native underwater screen overlay.
- Four gamma-correct, alpha-weighted texture mip levels with nearest pixel
  magnification. Each material keeps its own texels through the last mip.
- Linear fog near the selected render distance and 0.75-second chunk
  fade-in. Block edits preserve an already-visible chunk's fade state.
- Native owner-local HUD sprites, standard hotbar spacing and crosshair
  inversion, and separately rendered isometric block icons. Custom packs
  retain their own icons and already-colored water.
- First-person light is sampled at the eye rather than at the held model's
  vertices, fixing the snow block that became black beside a wall.

The converter prefers the owner's exact release jar when available.
All extracted images and generated assets remain ignored by Git.

## Verification

Build with `cmake --build build --config Release` and run
`ctest --test-dir build -C Release --output-on-failure`.
The headless checks cover lightmap bounds/day independence, biome colors,
fog distances, alpha-safe mipmaps and tile isolation, Fancy leaf faces,
environment timeline endpoints, and the existing engine regression suite.

`visual_probe` uses the real OpenGL renderer with fixed cameras and no
player save I/O. It exports its own framebuffer and fails on OpenGL errors:

```text
visual_probe snow OUTPUT.bmp
visual_probe forest OUTPUT.bmp
visual_probe water OUTPUT.bmp 0.5 4.0
visual_probe water OUTPUT.bmp 0.5 5.0
visual_probe water OUTPUT.bmp 0.0
visual_probe water OUTPUT.bmp 0.25
visual_probe underwater OUTPUT.bmp
visual_probe cave OUTPUT.bmp
visual_probe cave-dark OUTPUT.bmp
```

The final optional argument selects a pack for override verification after
the day-progress and animation-seconds arguments. GPU evidence is kept
under ignored `build/visual-evidence/`; the checked configuration is 1280 x
720, FOV 70, terrain profile 5, five-chunk radius, on an RTX 5060.

Final validation: all 289 headless checks passed. Day, dawn, night, forest,
snow, underwater, lit/unlit cave, animation, custom-pack and asset-free GPU
exports completed without OpenGL errors. Between animation seconds 4 and 5,
46.6% of the sampled water region changed. The held snow region averaged
RGB (211, 214, 214); the torch-lit cave region averaged 30.9 versus 9.4
without the torch. Custom colored water and icon overrides were also
checked from exported pixels. These measurements establish the targeted
fixes, not a comparison against a running Java client.

## Remaining differences

This is a rendering pass, not a claim of pixel or full-game parity.
Terrain profile 5 and the seven-biome map remain custom, including the
existing tree placement and canopy templates. Saves and generated blocks
are not migrated. The runtime does not yet use the separate worldgen
parity tools as its terrain generator.

Water still uses the engine's existing flat per-cell heights and shared
still-texture UVs rather than vanilla's corner slopes and flow UVs.
Transparency is sorted per chunk. Stars have a deterministic replacement
pattern; the moon currently shows its full phase. Held sprites still lack
vanilla's per-pixel extruded rims and all specialized item models.
The native experience bar displays an empty bar because this prototype has
no experience system. Missing owner assets use the original procedural
fallbacks, whose pixels are not Minecraft artwork.

Same-seed Java-client frame comparisons, every biome, specialized models,
and prolonged traversal remain outside the evidence collected here.
