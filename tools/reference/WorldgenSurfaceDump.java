package tcref;

import java.io.PrintStream;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.function.Function;

/**
 * Stage-5 reference dumper: runs the REAL 1.21.11 surface stage.
 *
 * For every (seed, chunk): the NoiseChunk fill of WorldgenChunkDump is stored in a real
 * ProtoChunk with the generation heightmaps primed like NoiseBasedChunkGenerator.doFill,
 * SurfaceSystem.buildSurface runs with a real BiomeManager (hashed 8-corner zoom over the
 * multi-noise sampler, y clamped to the chunk like ChunkAccess.getNoiseBiome), and then
 * NoiseBasedChunkGenerator.applyCarvers' loop is replayed on the surfaced chunk with the
 * real BiomeManager as the carver biome getter (so CarvingContext.topMaterial runs).
 * Besides the CHUNKS of the other dumpers, extra chunks are picked per seed so that every
 * special surface-rule biome (badlands, frozen ocean, mushroom, swamp, peaks...) is hit.
 *
 * Output:
 *   surface.csv           seed,chunk_x,chunk_z,section,name_hash            (after buildSurface)
 *   surface_heights.csv   seed,chunk_x,chunk_z,world_surface_hash,ocean_floor_hash
 *   pipeline.csv          seed,chunk_x,chunk_z,section,name_hash            (after surface + carvers)
 *   pipeline_heights.csv  seed,chunk_x,chunk_z,world_surface_hash,ocean_floor_hash
 *   biome_zoom.csv        seed,x,y,z,biome                                  (BiomeManager.getBiome)
 *   temperature.csv       biome,x,y,z,float_bits_hex                        (Biome.getTemperature)
 *   simplex.csv           noise,x,y,double_bits_hex                         (PerlinSimplexNoise)
 */
public class WorldgenSurfaceDump {
    static final long[] SEEDS = WorldgenRouterDump.SEEDS;
    static final int[][] CHUNKS = WorldgenChunkDump.CHUNKS;
    static final long[] SPECIAL_SEEDS = {0L, 42L, 123456789012345L};
    static final String[] SPECIAL_BIOMES = {
            "eroded_badlands", "badlands", "wooded_badlands", "frozen_ocean", "deep_frozen_ocean", "ice_spikes",
            "snowy_plains", "jagged_peaks", "frozen_peaks", "snowy_slopes", "grove", "swamp", "mangrove_swamp",
            "desert", "windswept_gravelly_hills", "stony_shore", "beach", "old_growth_pine_taiga",
            "old_growth_spruce_taiga", "mushroom_fields", "meadow", "stony_peaks", "cherry_grove", "snowy_taiga"};

    static Object call(String cls, String name, String args, Object target, Object... a) throws Exception {
        return WorldgenRefDump.method(cls, name, args).invoke(target, a);
    }

    static Object field(String cls, String name, Object target) throws Exception {
        return WorldgenRefDump.field(cls, name, target);
    }

    static long hashInit() {
        return 1125899906842597L;
    }

    /**
     * ChunkAccess biome storage as ChunkAccess.fillBiomesFromNoise leaves it: every quart of a chunk is
     * written once (sections bottom to top, quarts in x, y, z order) and BiomeManager only reads stored
     * values. Uses its own Climate.ParameterList so the R-tree's last-result shortcut sees the same query
     * sequence as the C port.
     */
    static class BiomeStore {
        final Object list, sampler, biomeGetter;
        final Method findValue, sampleM, getOrThrow;
        final Map<Object, Object> holders = new HashMap<>();
        final Map<Long, Object[]> chunks = new HashMap<>();

        BiomeStore(Object list, Object sampler, Object biomeGetter, Method findValue, Method sampleM, Method getOrThrow) {
            this.list = list;
            this.sampler = sampler;
            this.biomeGetter = biomeGetter;
            this.findValue = findValue;
            this.sampleM = sampleM;
            this.getOrThrow = getOrThrow;
        }

        Object[] chunk(int cx, int cz) {
            long key = ((long) cx << 32) ^ (cz & 0xffffffffL);
            Object[] arr = chunks.get(key);
            if (arr == null) {
                arr = new Object[16 * 96];
                try {
                    for (int sec = 0; sec < 24; sec++)
                        for (int i = 0; i < 4; i++)
                            for (int j = 0; j < 4; j++)
                                for (int k = 0; k < 4; k++) {
                                    int qx = cx * 4 + i, qy = -16 + sec * 4 + j, qz = cz * 4 + k;
                                    Object biomeKey = findValue.invoke(list, sampleM.invoke(sampler, qx, qy, qz));
                                    Object h = holders.get(biomeKey);
                                    if (h == null) {
                                        h = getOrThrow.invoke(biomeGetter, biomeKey);
                                        holders.put(biomeKey, h);
                                    }
                                    arr[(qy + 16) * 16 + k * 4 + i] = h;
                                }
                } catch (Exception e) {
                    throw new RuntimeException(e);
                }
                chunks.put(key, arr);
            }
            return arr;
        }

        Object at(int qx, int qy, int qz) {
            int y = Math.max(-16, Math.min(79, qy));
            return chunk(qx >> 2, qz >> 2)[(y + 16) * 16 + (qz & 3) * 4 + (qx & 3)];
        }

        void prepare(int cx, int cz) {
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) chunk(cx + dx, cz + dz);
        }
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);

        call("net.minecraft.SharedConstants", "tryDetectVersion", "", null);
        call("net.minecraft.server.Bootstrap", "bootStrap", "", null);
        Object provider = call("net.minecraft.data.registries.VanillaRegistries", "createLookup", "", null);

        Object nsReg = field("net.minecraft.core.registries.Registries", "NOISE_SETTINGS", null);
        Object nsGetter = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, nsReg);
        Object owKey = field("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "OVERWORLD", null);
        Object settingsHolder = call("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey", nsGetter, owKey);
        Object settings = call("net.minecraft.core.Holder", "value", "", settingsHolder);
        Object noiseSettings = call("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "noiseSettings", "", settings);
        Object surfaceRule = call("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "surfaceRule", "", settings);
        Object defaultBlock = call("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "defaultBlock", "", settings);

        Object biomeReg = field("net.minecraft.core.registries.Registries", "BIOME", null);
        Object biomeGetter = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, biomeReg);
        Object plainsKey = field("net.minecraft.world.level.biome.Biomes", "PLAINS", null);
        Object plainsHolder = call("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey", biomeGetter, plainsKey);

        // Carver replaceable tag (same workaround as WorldgenCarverDump).
        Path tagDir = Paths.get(args[2]).resolve("data/minecraft/tags/block");
        java.util.LinkedHashSet<String> replaceNames = new java.util.LinkedHashSet<>();
        WorldgenCarverDump.resolveTag(tagDir, "minecraft:overworld_carver_replaceables", replaceNames);
        Object blockRegistry = field("net.minecraft.core.registries.BuiltInRegistries", "BLOCK", null);
        Method regGet = WorldgenRefDump.method("net.minecraft.core.Registry", "get", "net.minecraft.resources.Identifier");
        Method identParse = WorldgenRefDump.method("net.minecraft.resources.Identifier", "parse", "java.lang.String");
        List<Object> replaceHolders = new ArrayList<>();
        for (String n : replaceNames) {
            replaceHolders.add(((java.util.Optional<?>) regGet.invoke(blockRegistry, identParse.invoke(null, n))).orElseThrow());
        }
        Object replaceSet = call("net.minecraft.core.HolderSet", "direct", "java.util.List", null, replaceHolders);
        java.lang.reflect.Field replaceField = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.carver.CarverConfiguration")
                .getDeclaredField(WorldgenRefDump.FIELD_OBF.get("net.minecraft.world.level.levelgen.carver.CarverConfiguration#replaceable"));
        replaceField.setAccessible(true);
        Method cwcConfig = WorldgenRefDump.method("net.minecraft.world.level.levelgen.carver.ConfiguredWorldCarver", "config", "");
        java.util.Set<Object> patched = java.util.Collections.newSetFromMap(new IdentityHashMap<>());

        String PRESET = "net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList$Preset";
        Object preset = field(PRESET, "OVERWORLD", null);
        Object pprovider = call(PRESET, "provider", "", preset);
        Object paramList = call(PRESET + "$SourceProvider", "apply", "java.util.function.Function", pprovider, (Function<Object, Object>) Function.identity());
        final Object presetProvider = pprovider;
        final java.util.function.Supplier<Object> freshList = () -> {
            try {
                return call(PRESET + "$SourceProvider", "apply", "java.util.function.Function", presetProvider, (Function<Object, Object>) Function.identity());
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        };
        Method findValue = WorldgenRefDump.method("net.minecraft.world.level.biome.Climate$ParameterList", "findValue", "net.minecraft.world.level.biome.Climate$TargetPoint");
        Method sampleM = WorldgenRefDump.method("net.minecraft.world.level.biome.Climate$Sampler", "sample", "int,int,int");
        Method getOrThrow = WorldgenRefDump.method("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey");

        // ---- chunk storage ---------------------------------------------------------------
        final int minY = -64, height = 384;
        String LHA = "net.minecraft.world.level.LevelHeightAccessor";
        String hGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getHeight()");
        String yGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getMinY()");
        Object heightAccessor = WorldgenCarverDump.proxy(LHA, (p, m, a) -> {
            if (m.getName().equals(hGet)) return height;
            if (m.getName().equals(yGet)) return minY;
            if (m.isDefault()) return java.lang.reflect.InvocationHandler.invokeDefault(p, m, a);
            if (m.getName().equals("hashCode")) return 1;
            if (m.getName().equals("equals")) return p == a[0];
            return null;
        });
        String IDMAP = "net.minecraft.core.IdMap";
        String sizeN = WorldgenRefDump.METHOD_OBF.get(IDMAP + "#size()");
        Object oneIdMap = WorldgenCarverDump.proxy(IDMAP, (p, m, a) -> {
            if (m.getName().equals(sizeN)) return 1;
            if (m.isDefault()) return java.lang.reflect.InvocationHandler.invokeDefault(p, m, a);
            return null;
        });
        String STRAT = "net.minecraft.world.level.chunk.Strategy";
        Object blockStrategy = call(STRAT, "createForBlockStates", "net.minecraft.core.IdMap", null,
                field("net.minecraft.world.level.block.Block", "BLOCK_STATE_REGISTRY", null));
        Object biomeStrategy = call(STRAT, "createForBiomes", "net.minecraft.core.IdMap", null, oneIdMap);
        Object airState = call("net.minecraft.world.level.block.Block", "defaultBlockState", "", field("net.minecraft.world.level.block.Blocks", "AIR", null));
        Object factory = WorldgenCarverDump.construct("net.minecraft.world.level.chunk.PalettedContainerFactory", 6, blockStrategy, airState, null, biomeStrategy, plainsHolder, null);

        Method sectionSet = WorldgenRefDump.method("net.minecraft.world.level.chunk.LevelChunkSection", "setBlockState", "int,int,int,net.minecraft.world.level.block.state.BlockState,boolean");
        Method sectionGet = WorldgenRefDump.method("net.minecraft.world.level.chunk.LevelChunkSection", "getBlockState", "int,int,int");
        Method getSection = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getSection", "int");
        Method maskGet = WorldgenRefDump.method("net.minecraft.world.level.chunk.CarvingMask", "get", "int,int,int");

        String HM_TYPES = "net.minecraft.world.level.levelgen.Heightmap$Types";
        Object WSWG = field(HM_TYPES, "WORLD_SURFACE_WG", null);
        Object OFWG = field(HM_TYPES, "OCEAN_FLOOR_WG", null);
        Method getHmUnprimed = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getOrCreateHeightmapUnprimed", HM_TYPES);
        Method hmUpdate = WorldgenRefDump.method("net.minecraft.world.level.levelgen.Heightmap", "update", "int,int,int,net.minecraft.world.level.block.state.BlockState");
        Method chunkHeight = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getHeight", HM_TYPES + ",int,int");

        // ---- noise fill machinery -----------------------------------------------------------
        String NBCG = "net.minecraft.world.level.levelgen.NoiseBasedChunkGenerator";
        Class<?> ngsC = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings");
        Method createFluidPicker = WorldgenRefDump.cls(NBCG).getDeclaredMethod(WorldgenRefDump.METHOD_OBF.get(NBCG + "#createFluidPicker(net.minecraft.world.level.levelgen.NoiseGeneratorSettings)"), ngsC);
        createFluidPicker.setAccessible(true);
        Object fluidPicker = createFluidPicker.invoke(null, settings);
        Object beardifier = field("net.minecraft.world.level.levelgen.DensityFunctions$BeardifierMarker", "INSTANCE", null);
        Object blender = call("net.minecraft.world.level.levelgen.blending.Blender", "empty", "", null);
        String RS = "net.minecraft.world.level.levelgen.RandomState";
        Method create = WorldgenRefDump.method(RS, "create", "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");
        Method samplerOf = WorldgenRefDump.method(RS, "sampler", "");
        Method surfaceSystemOf = WorldgenRefDump.method(RS, "surfaceSystem", "");
        Method buildSurface = WorldgenRefDump.method("net.minecraft.world.level.levelgen.SurfaceSystem", "buildSurface",
                RS + ",net.minecraft.world.level.biome.BiomeManager,net.minecraft.core.Registry,boolean,net.minecraft.world.level.levelgen.WorldGenerationContext,"
                        + "net.minecraft.world.level.chunk.ChunkAccess,net.minecraft.world.level.levelgen.NoiseChunk,net.minecraft.world.level.levelgen.SurfaceRules$RuleSource");
        Method obfuscateSeed = WorldgenRefDump.method("net.minecraft.world.level.biome.BiomeManager", "obfuscateSeed", "long");
        Method getBiome = WorldgenRefDump.method("net.minecraft.world.level.biome.BiomeManager", "getBiome", "net.minecraft.core.BlockPos");
        String NBS = "net.minecraft.world.level.biome.BiomeManager$NoiseBiomeSource";
        String nbsName = WorldgenRefDump.METHOD_OBF.get(NBS + "#getNoiseBiome(int,int,int)");

        String NC = "net.minecraft.world.level.levelgen.NoiseChunk";
        Method initFirst = WorldgenRefDump.method(NC, "initializeForFirstCellX", "");
        Method advance = WorldgenRefDump.method(NC, "advanceCellX", "int");
        Method selectYZ = WorldgenRefDump.method(NC, "selectCellYZ", "int,int");
        Method updY = WorldgenRefDump.method(NC, "updateForY", "int,double");
        Method updX = WorldgenRefDump.method(NC, "updateForX", "int,double");
        Method updZ = WorldgenRefDump.method(NC, "updateForZ", "int,double");
        Method interp = WorldgenRefDump.method(NC, "getInterpolatedState", "");
        Method swap = WorldgenRefDump.method(NC, "swapSlices", "");
        Method stop = WorldgenRefDump.method(NC, "stopInterpolation", "");
        Method cw = WorldgenRefDump.method(NC, "cellWidth", "");
        Method ch = WorldgenRefDump.method(NC, "cellHeight", "");
        Method aquiferOf = WorldgenRefDump.method(NC, "aquifer", "");

        Object generator = WorldgenCarverDump.construct(NBCG, 2, WorldgenCarverDump.construct("net.minecraft.world.level.biome.FixedBiomeSource", 1, plainsHolder), settingsHolder);
        // topMaterial only calls registryAccess().lookupOrThrow(BIOME) and passes the result to a
        // Context constructor that ignores it, so a stand-in returning null is enough.
        Object registryAccess = WorldgenCarverDump.proxy("net.minecraft.core.RegistryAccess", (p, m, a) -> {
            if (m.getName().equals("hashCode")) return 1;
            if (m.getName().equals("equals")) return p == a[0];
            if (m.getName().equals("toString")) return "registryAccess";
            return null;
        });
        Object genContext = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.WorldGenerationContext", 2, generator, heightAccessor);
        Method chunkPosMinX = WorldgenRefDump.method("net.minecraft.world.level.ChunkPos", "getMinBlockX", "");
        Method chunkPosMinZ = WorldgenRefDump.method("net.minecraft.world.level.ChunkPos", "getMinBlockZ", "");
        Method setLarge = WorldgenRefDump.method("net.minecraft.world.level.levelgen.WorldgenRandom", "setLargeFeatureSeed", "long,int,int");
        String CWC = "net.minecraft.world.level.levelgen.carver.ConfiguredWorldCarver";
        Method isStart = WorldgenRefDump.method(CWC, "isStartChunk", "net.minecraft.util.RandomSource");
        Method carve = WorldgenRefDump.method(CWC, "carve", "net.minecraft.world.level.levelgen.carver.CarvingContext,net.minecraft.world.level.chunk.ChunkAccess,java.util.function.Function,net.minecraft.util.RandomSource,net.minecraft.world.level.levelgen.Aquifer,net.minecraft.world.level.ChunkPos,net.minecraft.world.level.chunk.CarvingMask");
        Method biomeValue = WorldgenRefDump.method("net.minecraft.core.Holder", "value", "");
        Method genSettings = WorldgenRefDump.method("net.minecraft.world.level.biome.Biome", "getGenerationSettings", "");
        Method getCarvers = WorldgenRefDump.method("net.minecraft.world.level.biome.BiomeGenerationSettings", "getCarvers", "");
        Method getOrCreateMask = WorldgenRefDump.method("net.minecraft.world.level.chunk.ProtoChunk", "getOrCreateCarvingMask", "");

        try (PrintStream surf = new PrintStream(outDir.resolve("surface.csv").toFile());
             PrintStream surfH = new PrintStream(outDir.resolve("surface_heights.csv").toFile());
             PrintStream pipe = new PrintStream(outDir.resolve("pipeline.csv").toFile());
             PrintStream pipeH = new PrintStream(outDir.resolve("pipeline_heights.csv").toFile());
             PrintStream zoom = new PrintStream(outDir.resolve("biome_zoom.csv").toFile());
             PrintStream temp = new PrintStream(outDir.resolve("temperature.csv").toFile());
             PrintStream simp = new PrintStream(outDir.resolve("simplex.csv").toFile())) {
            surf.println("seed,chunk_x,chunk_z,section,name_hash");
            surfH.println("seed,chunk_x,chunk_z,world_surface_hash,ocean_floor_hash");
            pipe.println("seed,chunk_x,chunk_z,section,name_hash,mask_hash");
            pipeH.println("seed,chunk_x,chunk_z,world_surface_hash,ocean_floor_hash");
            zoom.println("seed,x,y,z,biome");
            temp.println("biome,x,y,z,float_bits");
            simp.println("noise,x,y,double_bits");

            // ---- simplex noise and biome temperature probes (seed independent) -----------
            String BIOME = "net.minecraft.world.level.biome.Biome";
            String[] noiseFields = {"TEMPERATURE_NOISE", "FROZEN_TEMPERATURE_NOISE", "BIOME_INFO_NOISE"};
            Method psnValue = WorldgenRefDump.method("net.minecraft.world.level.levelgen.synth.PerlinSimplexNoise", "getValue", "double,double,boolean");
            Random pr = new Random(7);
            for (int w = 0; w < noiseFields.length; w++) {
                Object noise = field(BIOME, noiseFields[w], null);
                for (int i = 0; i < 40; i++) {
                    double x = (pr.nextDouble() - 0.5) * 4000, y = (pr.nextDouble() - 0.5) * 4000;
                    simp.printf("%d,%s,%s,%x%n", w, Double.toString(x), Double.toString(y),
                            Double.doubleToRawLongBits((Double) psnValue.invoke(noise, x, y, false)));
                }
            }
            Class<?> bpC = WorldgenRefDump.cls("net.minecraft.core.BlockPos");
            java.lang.reflect.Constructor<?> bpCtor = bpC.getConstructor(int.class, int.class, int.class);
            Method getTemp = WorldgenRefDump.method(BIOME, "getTemperature", "net.minecraft.core.BlockPos,int");
            getTemp.setAccessible(true);
            String[] tempBiomes = {"plains", "frozen_ocean", "deep_frozen_ocean", "snowy_plains", "ice_spikes", "jagged_peaks",
                    "frozen_peaks", "desert", "taiga", "swamp", "river", "frozen_river", "forest", "old_growth_pine_taiga", "badlands"};
            for (String b : tempBiomes) {
                Object bk = call("net.minecraft.resources.ResourceKey", "create", "net.minecraft.resources.ResourceKey,net.minecraft.resources.Identifier", null,
                        biomeReg, identParse.invoke(null, "minecraft:" + b));
                Object bio = biomeValue.invoke(getOrThrow.invoke(biomeGetter, bk));
                for (int i = 0; i < 12; i++) {
                    int x = (int) ((pr.nextDouble() - 0.5) * 6000), z = (int) ((pr.nextDouble() - 0.5) * 6000);
                    int y = (i % 3 == 0) ? 64 : (i % 3 == 1 ? 90 + pr.nextInt(120) : -20 + pr.nextInt(80));
                    float t = (Float) getTemp.invoke(bio, bpCtor.newInstance(x, y, z), 63);
                    temp.printf("minecraft:%s,%d,%d,%d,%x%n", b, x, y, z, Float.floatToRawIntBits(t));
                }
            }

            // ---- chunk list (fixed ones plus special-biome ones per seed) ------------------
            Map<Long, List<int[]>> chunkLists = new LinkedHashMap<>();
            for (long seed : SEEDS) {
                List<int[]> list = new ArrayList<>(java.util.Arrays.asList(CHUNKS));
                chunkLists.put(seed, list);
            }
            for (long seed : SPECIAL_SEEDS) {
                Object state = create.invoke(null, provider, owKey, seed);
                Object sampler = samplerOf.invoke(state);
                List<int[]> list = chunkLists.get(seed);
                Map<String, int[]> found = new LinkedHashMap<>();
                for (int r = 0; r < 260 && found.size() < SPECIAL_BIOMES.length; r += 2) {
                    for (int a = -r; a <= r; a += 2) {
                        for (int b = -r; b <= r; b += 2) {
                            if (Math.abs(a) != r && Math.abs(b) != r) continue;
                            int cx = a * 4, cz = b * 4;
                            String name = String.valueOf(findValue.invoke(paramList, sampleM.invoke(sampler, cx * 4 + 2, 16, cz * 4 + 2)));
                            for (String target : SPECIAL_BIOMES) {
                                if (!found.containsKey(target) && name.contains(":" + target + "]")) found.put(target, new int[]{cx, cz});
                            }
                        }
                    }
                }
                System.err.println("seed " + seed + " special biomes found: " + found.keySet());
                list.addAll(found.values());
            }

            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider, owKey, seed);
                Object sampler = samplerOf.invoke(state);
                Object surfaceSystem = surfaceSystemOf.invoke(state);
                Function<BiomeStore, Object> makeManager = store -> {
                    try {
                        Object source = WorldgenCarverDump.proxy(NBS, (p, m, a) -> {
                            if (m.getName().equals(nbsName)) return store.at((Integer) a[0], (Integer) a[1], (Integer) a[2]);
                            if (m.isDefault()) return java.lang.reflect.InvocationHandler.invokeDefault(p, m, a);
                            return null;
                        });
                        long zoomSeed = (Long) obfuscateSeed.invoke(null, seed);
                        return WorldgenCarverDump.construct("net.minecraft.world.level.biome.BiomeManager", 2, source, zoomSeed);
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                };
                Function<Object, Function<Object, Object>> getterOf = manager -> pos -> {
                    try {
                        return getBiome.invoke(manager, pos);
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                };
                // Separate stores (own R-trees) for the getBiome probes and for chunk generation.
                BiomeStore zoomStore = new BiomeStore(freshList.get(), sampler, biomeGetter, findValue, sampleM, getOrThrow);
                BiomeStore genStore = new BiomeStore(freshList.get(), sampler, biomeGetter, findValue, sampleM, getOrThrow);
                Object zoomManager = makeManager.apply(zoomStore);
                Function<Object, Object> zoomGetter = getterOf.apply(zoomManager);
                Object biomeManager = makeManager.apply(genStore);
                Function<Object, Object> managerGetter = getterOf.apply(biomeManager);

                // SURF_PROBE=x,z : print the iceberg noise terms of the real SurfaceSystem and exit.
                String probe = System.getenv("SURF_PROBE");
                if (probe != null) {
                    String[] pp = probe.split(",");
                    int px = Integer.parseInt(pp[0]), pz = Integer.parseInt(pp[1]);
                    String SS = "net.minecraft.world.level.levelgen.SurfaceSystem";
                    String NN = "net.minecraft.world.level.levelgen.synth.NormalNoise";
                    Method nv = WorldgenRefDump.method(NN, "getValue", "double,double,double");
                    Object ice = field(SS, "icebergSurfaceNoise", surfaceSystem);
                    Object pil = field(SS, "icebergPillarNoise", surfaceSystem);
                    Object roof = field(SS, "icebergPillarRoofNoise", surfaceSystem);
                    double e = Math.min(Math.abs((Double) nv.invoke(ice, (double) px, 0.0, (double) pz) * 8.25),
                            (Double) nv.invoke(pil, px * 1.28, 0.0, pz * 1.28) * 15.0);
                    double h = Math.abs((Double) nv.invoke(roof, px * 1.17, 0.0, pz * 1.17) * 1.5);
                    System.err.println("PROBE e=" + e + " h=" + h + " surf=" + nv.invoke(ice, (double) px, 0.0, (double) pz)
                            + " pil=" + nv.invoke(pil, px * 1.28, 0.0, pz * 1.28));
                    continue;
                }
                // BiomeManager.getBiome probes.
                Random zr = new Random(seed ^ 0x5DEECE66DL);
                for (int i = 0; i < 48; i++) {
                    int x = zr.nextInt(8000) - 4000, y = zr.nextInt(400) - 80, z = zr.nextInt(8000) - 4000;
                    String nm = String.valueOf(call("net.minecraft.core.Holder", "getRegisteredName", "", zoomGetter.apply(bpCtor.newInstance(x, y, z))));
                    zoom.printf("%d,%d,%d,%d,%s%n", seed, x, y, z, nm);
                }

                // SURF_FULL=seed,cx,cz,file : dump that one chunk block by block (debugging) and skip the rest.
                String full = System.getenv("SURF_FULL");
                String[] fullParts = full != null ? full.split(",") : null;
                for (int[] cc : chunkLists.get(seed)) {
                    int cx = cc[0], cz = cc[1];
                    if (fullParts != null && !(Long.parseLong(fullParts[0]) == seed && Integer.parseInt(fullParts[1]) == cx && Integer.parseInt(fullParts[2]) == cz)) continue;
                    int minX = cx * 16, minZ = cz * 16;
                    Object nc = WorldgenCarverDump.construct(NC, 9, 16 / 4, state, minX, minZ, noiseSettings, beardifier, settings, fluidPicker, blender);
                    int k = (Integer) cw.invoke(nc);
                    int l = (Integer) ch.invoke(nc);
                    int minCellY = Math.floorDiv(minY, l);
                    int cellCountY = Math.floorDiv(height, l);

                    Object chunkPos = WorldgenCarverDump.construct("net.minecraft.world.level.ChunkPos", 2, cx, cz);
                    Object chunk = WorldgenCarverDump.construct("net.minecraft.world.level.chunk.ProtoChunk", 5, chunkPos, field("net.minecraft.world.level.chunk.UpgradeData", "EMPTY", null), heightAccessor, factory, null);
                    Object[] sections = new Object[height / 16];
                    for (int i = 0; i < sections.length; i++) sections[i] = getSection.invoke(chunk, i);
                    Object hmOcean = getHmUnprimed.invoke(chunk, OFWG);
                    Object hmSurface = getHmUnprimed.invoke(chunk, WSWG);
                    IdentityHashMap<Object, String> nameCache = new IdentityHashMap<>();

                    initFirst.invoke(nc);
                    for (int o = 0; o < 16 / k; o++) {
                        advance.invoke(nc, o);
                        for (int p = 0; p < 16 / k; p++) {
                            for (int q = cellCountY - 1; q >= 0; q--) {
                                selectYZ.invoke(nc, q, p);
                                for (int r = l - 1; r >= 0; r--) {
                                    int s = (minCellY + q) * l + r;
                                    updY.invoke(nc, s, (double) r / l);
                                    for (int v = 0; v < k; v++) {
                                        int w = minX + o * k + v;
                                        updX.invoke(nc, w, (double) v / k);
                                        for (int y = 0; y < k; y++) {
                                            int z = minZ + p * k + y;
                                            updZ.invoke(nc, z, (double) y / k);
                                            Object bs = interp.invoke(nc);
                                            if (bs == null) bs = defaultBlock;
                                            int yy = s - minY;
                                            sectionSet.invoke(sections[yy >> 4], w & 15, yy & 15, z & 15, bs, false);
                                            if (!nameCache.computeIfAbsent(bs, WorldgenCarverDump::blockName).equals("minecraft:air")) {
                                                hmUpdate.invoke(hmOcean, w & 15, s, z & 15, bs);
                                                hmUpdate.invoke(hmSurface, w & 15, s, z & 15, bs);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        swap.invoke(nc);
                    }
                    stop.invoke(nc);

                    // ---- SurfaceSystem.buildSurface -----------------------------------------
                    genStore.prepare(cx, cz);
                    buildSurface.invoke(surfaceSystem, state, biomeManager, null, false, genContext, chunk, nc, surfaceRule);
                    dumpChunk(surf, surfH, seed, cx, cz, sections, sectionGet, null, maskGet, chunk, chunkHeight, WSWG, OFWG, nameCache, minY, height);
                    if (fullParts != null) {
                        try (PrintStream fo = new PrintStream(fullParts[3])) {
                            fo.println("x,z,y,block");
                            for (int x = 0; x < 16; x++)
                                for (int z = 0; z < 16; z++)
                                    for (int y = 0; y < height; y++)
                                        fo.printf("%d,%d,%d,%s%n", x, z, y + minY, nameCache.computeIfAbsent(sectionGet.invoke(sections[y >> 4], x, y & 15, z), WorldgenCarverDump::blockName));
                        }
                        continue;
                    }

                    // ---- NoiseBasedChunkGenerator.applyCarvers on the surfaced chunk -----------
                    Object random = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.WorldgenRandom", 1,
                            WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.LegacyRandomSource", 1, 0L));
                    Object aquifer = aquiferOf.invoke(nc);
                    Object ctx = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.carver.CarvingContext", 6,
                            generator, registryAccess, heightAccessor, nc, state, surfaceRule);
                    Object mask = getOrCreateMask.invoke(chunk);
                    for (int j = -8; j <= 8; j++) {
                        for (int kk = -8; kk <= 8; kk++) {
                            Object cp2 = WorldgenCarverDump.construct("net.minecraft.world.level.ChunkPos", 2, cx + j, cz + kk);
                            int bx = (Integer) chunkPosMinX.invoke(cp2) >> 2;
                            int bz = (Integer) chunkPosMinZ.invoke(cp2) >> 2;
                            Object key = findValue.invoke(paramList, sampleM.invoke(sampler, bx, 0, bz));
                            Object holder = getOrThrow.invoke(biomeGetter, key);
                            Object gs = genSettings.invoke(biomeValue.invoke(holder));
                            int m = 0;
                            for (Object carverHolder : (Iterable<?>) getCarvers.invoke(gs)) {
                                Object cwc = biomeValue.invoke(carverHolder);
                                Object cfg = cwcConfig.invoke(cwc);
                                if (patched.add(cfg)) replaceField.set(cfg, replaceSet);
                                setLarge.invoke(random, seed + (long) m, cx + j, cz + kk);
                                if ((Boolean) isStart.invoke(cwc, random)) {
                                    carve.invoke(cwc, ctx, chunk, managerGetter, random, aquifer, cp2, mask);
                                }
                                m++;
                            }
                        }
                    }
                    dumpChunk(pipe, pipeH, seed, cx, cz, sections, sectionGet, mask, maskGet, chunk, chunkHeight, WSWG, OFWG, nameCache, minY, height);
                    System.err.println("seed " + seed + " chunk " + cx + "," + cz);
                }
            }
        }
        System.err.println("wrote surface fixtures to " + outDir);
    }

    static void dumpChunk(PrintStream rows, PrintStream heights, long seed, int cx, int cz, Object[] sections, Method sectionGet,
                          Object mask, Method maskGet, Object chunk, Method chunkHeight, Object WSWG, Object OFWG,
                          IdentityHashMap<Object, String> nameCache, int minY, int height) throws Exception {
        for (int sec = 0; sec < height / 16; sec++) {
            long hn = hashInit(), hm = hashInit();
            for (int yy = 0; yy < 16; yy++) {
                int y = sec * 16 + yy;
                for (int z = 0; z < 16; z++) {
                    for (int x = 0; x < 16; x++) {
                        Object bs = sectionGet.invoke(sections[sec], x, yy, z);
                        hn = 31 * hn + nameCache.computeIfAbsent(bs, WorldgenCarverDump::blockName).hashCode();
                        if (mask != null) hm = 31 * hm + ((Boolean) maskGet.invoke(mask, x, y + minY, z) ? 1 : 0);
                    }
                }
            }
            if (mask != null) rows.printf("%d,%d,%d,%d,%d,%d%n", seed, cx, cz, sec, hn, hm);
            else rows.printf("%d,%d,%d,%d,%d%n", seed, cx, cz, sec, hn);
        }
        long hw = hashInit(), ho = hashInit();
        for (int x = 0; x < 16; x++) {
            for (int z = 0; z < 16; z++) {
                hw = 31 * hw + (Integer) chunkHeight.invoke(chunk, WSWG, x, z);
                ho = 31 * ho + (Integer) chunkHeight.invoke(chunk, OFWG, x, z);
            }
        }
        heights.printf("%d,%d,%d,%d,%d%n", seed, cx, cz, hw, ho);
        rows.flush();
        heights.flush();
    }
}
