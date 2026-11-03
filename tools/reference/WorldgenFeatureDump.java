package tcref;

import java.io.PrintStream;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;

/**
 * Stage-6 reference dumper: runs the REAL 1.21.11 biome decoration for chosen feature types.
 *
 * For every (seed, target chunk) the 3 x 3 chunk region is generated through the same pipeline as
 * WorldgenSurfaceDump (fill, surface, carvers, per-chunk stored biomes); then
 * ChunkGenerator.applyBiomeDecoration's loop is replayed verbatim - decoration seed, candidate
 * biomes of the 3 x 3 region, FeatureSorter step lists, per-feature setFeatureSeed, real
 * PlacedFeature.placeWithBiomeCheck with the real placement modifiers - but only for placed features
 * whose configured feature type is listed in FEATURES (default "ore,scattered_ore"). A stand-in
 * WorldGenLevel exposes the region (unsupported calls fail loudly).
 *
 * Output:
 *   feature_order.csv   step,index,placed_feature         (FeatureSorter result for the overworld)
 *   features.csv        seed,target_x,target_z,dx,dz,section,name_hash   (after decoration, 9 chunks)
 *   feature_stats.csv   seed,target_x,target_z,placed_feature,calls,placed
 */
public class WorldgenFeatureDump {
    static final long[] SEEDS = {0L, 42L, 123456789012345L};
    static java.util.Set<String> NAMES = null;
    static final int[][] TARGETS = {{0, 0}, {-1, -1}, {5, -7}, {31, 12}, {-20, 3}, {100, 100}};

    static final Map<String, Object> T_TREES = new java.util.TreeMap<>();
    static Method T_PLACE, T_HEIGHT, T_GETSTATE, T_SEED, T_SETBLOCK;
    static Object T_OFWG;
    static java.lang.reflect.Constructor<?> T_BP;

    /** Sort key of a block position: lexicographic (x, y, z). */
    static long touchKey(int x, int y, int z) {
        return ((long) (x + (1 << 20)) << 31) | ((long) (y + 512) << 21) | (long) (z + (1 << 20));
    }

    /**
     * Places every selected configured feature at K fixed columns of the target chunk (each with its own
     * feature seed) and writes one row per placement: the placement result and a hash of every block whose
     * name changed (sorted by x, y, z).
     */
    static void runTreeTest(PrintStream out, long seed, int tx, int tz, Object level, Object generator, Map<Long, Object> touched) throws Exception {
        int kCount = System.getenv("TREE_K") != null ? Integer.parseInt(System.getenv("TREE_K")) : 12;
        int ti = 0;
        for (Map.Entry<String, Object> e : T_TREES.entrySet()) {
            for (int k = 0; k < kCount; k++) {
                int x = tx * 16 + (k * 7 + 3) % 16;
                int z = tz * 16 + (k * 11 + 5) % 16;
                int y = (Integer) T_HEIGHT.invoke(level, T_OFWG, x, z);
                Object random = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.WorldgenRandom", 1,
                        WorldgenRefDump.cls("net.minecraft.world.level.levelgen.XoroshiroRandomSource").getConstructor(long.class).newInstance(0L));
                T_SEED.invoke(random, 777000L + ti * 1000L + k, 0, 0);
                touched.clear();
                boolean placed = (Boolean) T_PLACE.invoke(e.getValue(), level, generator, random, T_BP.newInstance(x, y, z));
                long hash = 1125899906842597L;
                int count = 0;
                for (Map.Entry<Long, Object> t : touched.entrySet()) {
                    long key = t.getKey();
                    int bx = (int) (key >> 31) - (1 << 20), by = (int) ((key >> 21) & 1023) - 512, bz = (int) (key & ((1 << 21) - 1)) - (1 << 20);
                    String now = WorldgenCarverDump.blockName(T_GETSTATE.invoke(level, T_BP.newInstance(bx, by, bz)));
                    if (now.equals(WorldgenCarverDump.blockName(t.getValue()))) continue;
                    hash = 31 * hash + bx;
                    hash = 31 * hash + by;
                    hash = 31 * hash + bz;
                    hash = 31 * hash + now.hashCode();
                    count++;
                }
                out.printf("%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d%n", seed, tx, tz, e.getKey(), k, x, y, z, placed ? 1 : 0, count, hash, 777000L + ti * 1000L + k);
                String dumpSpec = System.getenv("TREE_DUMP"); // seed,tx,tz,tree,k,file
                if (dumpSpec != null) {
                    String[] ds = dumpSpec.split(",");
                    if (Long.parseLong(ds[0]) == seed && Integer.parseInt(ds[1]) == tx && Integer.parseInt(ds[2]) == tz && ds[3].equals(e.getKey()) && Integer.parseInt(ds[4]) == k) {
                        try (PrintStream fo = new PrintStream(ds[5])) {
                            fo.println("x,y,z,block");
                            for (Map.Entry<Long, Object> t : touched.entrySet()) {
                                long key = t.getKey();
                                int bx = (int) (key >> 31) - (1 << 20), by = (int) ((key >> 21) & 1023) - 512, bz = (int) (key & ((1 << 21) - 1)) - (1 << 20);
                                String now = WorldgenCarverDump.blockName(T_GETSTATE.invoke(level, T_BP.newInstance(bx, by, bz)));
                                if (!now.equals(WorldgenCarverDump.blockName(t.getValue()))) fo.printf("%d,%d,%d,%s%n", bx, by, bz, now);
                            }
                        }
                        System.err.println("tree dump feature seed " + (777000L + ti * 1000L + k));
                    }
                }
                // restore the world so every placement is independent of the others
                java.util.List<Map.Entry<Long, Object>> undo = new java.util.ArrayList<>(touched.entrySet());
                touched.clear();
                for (Map.Entry<Long, Object> t : undo) {
                    long key = t.getKey();
                    int bx = (int) (key >> 31) - (1 << 20), by = (int) ((key >> 21) & 1023) - 512, bz = (int) (key & ((1 << 21) - 1)) - (1 << 20);
                    T_SETBLOCK.invoke(level, T_BP.newInstance(bx, by, bz), t.getValue(), 3);
                }
                touched.clear();
            }
            ti++;
        }
        out.flush();
    }

    static Object call(String cls, String name, String args, Object target, Object... a) throws Exception {
        return WorldgenRefDump.method(cls, name, args).invoke(target, a);
    }

    static Object field(String cls, String name, Object target) throws Exception {
        return WorldgenRefDump.field(cls, name, target);
    }

    static String mapType(String t) throws Exception {
        switch (t) {
            case "int": case "long": case "double": case "float": case "boolean": return t;
            default: return WorldgenRefDump.CLASS_OBF.containsKey(t) ? WorldgenRefDump.cls(t).getName() : t;
        }
    }


    /** Bind every block tag of the vanilla data pack into BuiltInRegistries.BLOCK (tags are not loaded outside a server). */
    static void bindBlockTags(Path tagRoot) throws Exception {
        Object blockRegistry = field("net.minecraft.core.registries.BuiltInRegistries", "BLOCK", null);
        Method regGet = WorldgenRefDump.method("net.minecraft.core.Registry", "get", "net.minecraft.resources.Identifier");
        Method identParse = WorldgenRefDump.method("net.minecraft.resources.Identifier", "parse", "java.lang.String");
        Object blockKey = field("net.minecraft.core.registries.Registries", "BLOCK", null);
        Method tagCreate = WorldgenRefDump.method("net.minecraft.tags.TagKey", "create", "net.minecraft.resources.ResourceKey,net.minecraft.resources.Identifier");
        Method bindTag = WorldgenRefDump.method("net.minecraft.core.MappedRegistry", "bindTag", "net.minecraft.tags.TagKey,java.util.List");
        Method refresh = WorldgenRefDump.method("net.minecraft.core.MappedRegistry", "refreshTagsInHolders", "");
        Map<String, java.util.LinkedHashSet<String>> tags = new LinkedHashMap<>();
        try (java.util.stream.Stream<Path> walk = Files.walk(tagRoot)) {
            for (Path f : (Iterable<Path>) walk.filter(x -> x.toString().endsWith(".json"))::iterator) {
                String rel = tagRoot.relativize(f).toString().replace(java.io.File.separatorChar, '/');
                String name = "minecraft:" + rel.substring(0, rel.length() - 5);
                java.util.LinkedHashSet<String> out = new java.util.LinkedHashSet<>();
                collectTag(tagRoot, name, out);
                tags.put(name, out);
            }
        }
        java.lang.reflect.Field frozenF = WorldgenRefDump.cls("net.minecraft.core.MappedRegistry").getDeclaredField(WorldgenRefDump.FIELD_OBF.get("net.minecraft.core.MappedRegistry#frozen"));
        frozenF.setAccessible(true);
        frozenF.set(blockRegistry, false);
        for (Map.Entry<String, java.util.LinkedHashSet<String>> e : tags.entrySet()) {
            List<Object> holders = new ArrayList<>();
            for (String n : e.getValue()) {
                java.util.Optional<?> h = (java.util.Optional<?>) regGet.invoke(blockRegistry, identParse.invoke(null, n));
                if (h.isPresent()) holders.add(h.get());
            }
            bindTag.invoke(blockRegistry, tagCreate.invoke(null, blockKey, identParse.invoke(null, e.getKey())), holders);
        }
        refresh.invoke(blockRegistry);
        frozenF.set(blockRegistry, true);
        System.err.println("bound " + tags.size() + " block tags");
    }

    static void collectTag(Path tagRoot, String name, java.util.LinkedHashSet<String> out) throws Exception {
        Path f = tagRoot.resolve(name.substring(name.indexOf(':') + 1) + ".json");
        if (!Files.exists(f)) return;
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"(#?minecraft:[a-z0-9_/]+)\"").matcher(Files.readString(f));
        while (m.find()) {
            String v = m.group(1);
            if (v.startsWith("#")) collectTag(tagRoot, v.substring(1), out);
            else out.add(v);
        }
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);
        java.util.Set<String> allowed = new java.util.HashSet<>(java.util.Arrays.asList(
                (System.getenv("FEATURES") != null ? System.getenv("FEATURES") : "ore,scattered_ore").split(",")));
        // FEATURE_NAMES limits the run to the listed placed features; FEATURE_TAG suffixes the output files.
        String namesEnv = System.getenv("FEATURE_NAMES");
        NAMES = namesEnv != null ? new java.util.HashSet<>(java.util.Arrays.asList(namesEnv.split(","))) : null;
        String tag = System.getenv("FEATURE_TAG") != null ? "_" + System.getenv("FEATURE_TAG") : "";

        call("net.minecraft.SharedConstants", "tryDetectVersion", "", null);
        call("net.minecraft.server.Bootstrap", "bootStrap", "", null);
        bindBlockTags(Paths.get(args[2]).resolve("data/minecraft/tags/block"));
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

        // Carver replaceables (tags are not bound in this lookup).
        Path tagDir = Paths.get(args[2]).resolve("data/minecraft/tags/block");
        java.util.LinkedHashSet<String> replaceNames = new java.util.LinkedHashSet<>();
        WorldgenCarverDump.resolveTag(tagDir, "minecraft:overworld_carver_replaceables", replaceNames);
        Object blockRegistry = field("net.minecraft.core.registries.BuiltInRegistries", "BLOCK", null);
        Method regGet = WorldgenRefDump.method("net.minecraft.core.Registry", "get", "net.minecraft.resources.Identifier");
        Method identParse = WorldgenRefDump.method("net.minecraft.resources.Identifier", "parse", "java.lang.String");
        List<Object> replaceHolders = new ArrayList<>();
        for (String n : replaceNames) replaceHolders.add(((java.util.Optional<?>) regGet.invoke(blockRegistry, identParse.invoke(null, n))).orElseThrow());
        Object replaceSet = call("net.minecraft.core.HolderSet", "direct", "java.util.List", null, replaceHolders);
        java.lang.reflect.Field replaceField = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.carver.CarverConfiguration")
                .getDeclaredField(WorldgenRefDump.FIELD_OBF.get("net.minecraft.world.level.levelgen.carver.CarverConfiguration#replaceable"));
        replaceField.setAccessible(true);
        Method cwcConfig = WorldgenRefDump.method("net.minecraft.world.level.levelgen.carver.ConfiguredWorldCarver", "config", "");
        java.util.Set<Object> patched = java.util.Collections.newSetFromMap(new IdentityHashMap<>());

        String PRESET = "net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList$Preset";
        Object preset = field(PRESET, "OVERWORLD", null);
        final Object presetProvider = call(PRESET, "provider", "", preset);
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
        Object carverList = freshList.get();

        // ---- chunk storage -----------------------------------------------------------------
        final int minY = -64, height = 384;
        String LHA = "net.minecraft.world.level.LevelHeightAccessor";
        String hGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getHeight()");
        String yGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getMinY()");
        Object heightAccessor = WorldgenCarverDump.proxy(LHA, (p, m, a) -> {
            if (m.getName().equals(hGet)) return height;
            if (m.getName().equals(yGet)) return minY;
            if (m.isDefault()) return InvocationHandler.invokeDefault(p, m, a);
            if (m.getName().equals("hashCode")) return 1;
            if (m.getName().equals("equals")) return p == a[0];
            return null;
        });
        String IDMAP = "net.minecraft.core.IdMap";
        String sizeN = WorldgenRefDump.METHOD_OBF.get(IDMAP + "#size()");
        Object oneIdMap = WorldgenCarverDump.proxy(IDMAP, (p, m, a) -> {
            if (m.getName().equals(sizeN)) return 1;
            if (m.isDefault()) return InvocationHandler.invokeDefault(p, m, a);
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
        String HM_TYPES = "net.minecraft.world.level.levelgen.Heightmap$Types";
        Object WSWG = field(HM_TYPES, "WORLD_SURFACE_WG", null);
        Object OFWG = field(HM_TYPES, "OCEAN_FLOOR_WG", null);
        Method getHmUnprimed = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getOrCreateHeightmapUnprimed", HM_TYPES);
        Method hmUpdate = WorldgenRefDump.method("net.minecraft.world.level.levelgen.Heightmap", "update", "int,int,int,net.minecraft.world.level.block.state.BlockState");
        Method chunkHeight = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getHeight", HM_TYPES + ",int,int");
        Method chunkGetState = WorldgenRefDump.method("net.minecraft.world.level.BlockGetter", "getBlockState", "net.minecraft.core.BlockPos");
        Method chunkSetState = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "setBlockState", "net.minecraft.core.BlockPos,net.minecraft.world.level.block.state.BlockState,int");

        // ---- noise fill machinery --------------------------------------------------------------
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
        Object genContext = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.WorldGenerationContext", 2, generator, heightAccessor);
        Object registryAccess = WorldgenCarverDump.proxy("net.minecraft.core.RegistryAccess", (p, m, a) -> {
            if (m.getName().equals("hashCode")) return 1;
            if (m.getName().equals("equals")) return p == a[0];
            if (m.getName().equals("toString")) return "registryAccess";
            return null;
        });
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
        Class<?> bpC = WorldgenRefDump.cls("net.minecraft.core.BlockPos");
        java.lang.reflect.Constructor<?> bpCtor = bpC.getConstructor(int.class, int.class, int.class);

        // ---- decoration machinery --------------------------------------------------------------------
        Object holderParamList = call(PRESET + "$SourceProvider", "apply", "java.util.function.Function", presetProvider,
                (Function<Object, Object>) key -> {
                    try {
                        return getOrThrow.invoke(biomeGetter, key);
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                });
        Object decoSource = call("net.minecraft.world.level.biome.MultiNoiseBiomeSource", "createFromList", "net.minecraft.world.level.biome.Climate$ParameterList", null, holderParamList);
        Object decoGenerator = WorldgenCarverDump.construct(NBCG, 2, decoSource, settingsHolder);
        Object stepData = ((java.util.function.Supplier<?>) field("net.minecraft.world.level.chunk.ChunkGenerator", "featuresPerStep", decoGenerator)).get();
        String SFD = "net.minecraft.world.level.biome.FeatureSorter$StepFeatureData";
        Method sfdFeatures = WorldgenRefDump.method(SFD, "features", "");
        Method sfdIndex = WorldgenRefDump.method(SFD, "indexMapping", "");
        Method toIntApply = java.util.function.ToIntFunction.class.getMethod("applyAsInt", Object.class);
        Method biomeGenSettings = WorldgenRefDump.method("net.minecraft.world.level.biome.Biome", "getGenerationSettings", "");
        Method bgsFeatures = WorldgenRefDump.method("net.minecraft.world.level.biome.BiomeGenerationSettings", "features", "");
        Method holderSetStream = WorldgenRefDump.method("net.minecraft.core.HolderSet", "stream", "");
        Method placeWithCheck = WorldgenRefDump.method("net.minecraft.world.level.levelgen.placement.PlacedFeature", "placeWithBiomeCheck",
                "net.minecraft.world.level.WorldGenLevel,net.minecraft.world.level.chunk.ChunkGenerator,net.minecraft.util.RandomSource,net.minecraft.core.BlockPos");
        Method pfFeature = WorldgenRefDump.method("net.minecraft.world.level.levelgen.placement.PlacedFeature", "feature", "");
        Method cfFeature = WorldgenRefDump.method("net.minecraft.world.level.levelgen.feature.ConfiguredFeature", "feature", "");
        Method setDecorationSeed = WorldgenRefDump.method("net.minecraft.world.level.levelgen.WorldgenRandom", "setDecorationSeed", "long,int,int");
        Method setFeatureSeed = WorldgenRefDump.method("net.minecraft.world.level.levelgen.WorldgenRandom", "setFeatureSeed", "long,int,int");
        Object featureRegistry = field("net.minecraft.core.registries.BuiltInRegistries", "FEATURE", null);
        Method regGetKey = WorldgenRefDump.method("net.minecraft.core.Registry", "getKey", "java.lang.Object");

        // placed feature -> id name
        Object pfReg = field("net.minecraft.core.registries.Registries", "PLACED_FEATURE", null);
        Object pfLookup = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, pfReg);
        Map<Object, String> pfNames = new IdentityHashMap<>();
        Method listElements = WorldgenRefDump.method("net.minecraft.core.HolderLookup", "listElements", "");
        Method refKey = WorldgenRefDump.method("net.minecraft.core.Holder$Reference", "key", "");
        Method keyLoc = WorldgenRefDump.method("net.minecraft.resources.ResourceKey", "identifier", "");
        ((java.util.stream.Stream<?>) listElements.invoke(pfLookup)).forEach(h -> {
            try {
                pfNames.put(biomeValue.invoke(h), String.valueOf(keyLoc.invoke(refKey.invoke(h))));
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        });

        if (System.getenv("TREE_TEST") != null) {
            // Direct per-configured-feature test: every configured feature of type TREE_TYPE (default tree).
            String wantType = System.getenv("TREE_TYPE") != null ? System.getenv("TREE_TYPE") : "minecraft:tree";
            String treeNames = System.getenv("TREE_NAMES");
            java.util.Set<String> wantNames = treeNames != null ? new java.util.HashSet<>(java.util.Arrays.asList(treeNames.split(","))) : null;
            Object cfReg = field("net.minecraft.core.registries.Registries", "CONFIGURED_FEATURE", null);
            Object cfLookup = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, cfReg);
            ((java.util.stream.Stream<?>) listElements.invoke(cfLookup)).forEach(h -> {
                try {
                    Object cfObj = biomeValue.invoke(h);
                    String type = String.valueOf(regGetKey.invoke(featureRegistry, cfFeature.invoke(cfObj)));
                    String id = String.valueOf(keyLoc.invoke(refKey.invoke(h)));
                    String skip = System.getenv("TREE_SKIP");
                    boolean skipped = skip != null && java.util.Arrays.asList(skip.split(",")).contains(id);
                    if (type.equals(wantType) && !skipped && (wantNames == null || wantNames.contains(id))) T_TREES.put(id, cfObj);
                } catch (Exception e) {
                    throw new RuntimeException(e);
                }
            });
            T_PLACE = WorldgenRefDump.method("net.minecraft.world.level.levelgen.feature.ConfiguredFeature", "place",
                    "net.minecraft.world.level.WorldGenLevel,net.minecraft.world.level.chunk.ChunkGenerator,net.minecraft.util.RandomSource,net.minecraft.core.BlockPos");
            T_HEIGHT = WorldgenRefDump.method("net.minecraft.world.level.LevelReader", "getHeight", HM_TYPES + ",int,int");
            T_GETSTATE = WorldgenRefDump.method("net.minecraft.world.level.BlockGetter", "getBlockState", "net.minecraft.core.BlockPos");
            T_SETBLOCK = WorldgenRefDump.method("net.minecraft.world.level.LevelWriter", "setBlock", "net.minecraft.core.BlockPos,net.minecraft.world.level.block.state.BlockState,int");
            T_OFWG = OFWG;
            T_BP = bpCtor;
            T_SEED = setFeatureSeed;
            System.err.println("tree test: " + T_TREES.size() + " configured features of type " + wantType);
        }

        final Class<?> entityBlockC = WorldgenRefDump.cls("net.minecraft.world.level.block.EntityBlock");
        final Method newBlockEntity = WorldgenRefDump.method("net.minecraft.world.level.block.EntityBlock", "newBlockEntity", "net.minecraft.core.BlockPos,net.minecraft.world.level.block.state.BlockState");
        final Method bsGetBlock = WorldgenRefDump.method("net.minecraft.world.level.block.state.BlockBehaviour$BlockStateBase", "getBlock", "");
        final Method bsFluidState = WorldgenRefDump.method("net.minecraft.world.level.block.state.BlockBehaviour$BlockStateBase", "getFluidState", "");
        // ---- WorldGenLevel stand-in --------------------------------------------------------------------------
        String WGL = "net.minecraft.world.level.WorldGenLevel";
        Map<String, String> sigToMoj = new HashMap<>();
        String[][] wanted = {
                {"net.minecraft.world.level.LevelReader", "getChunk", "int,int"},
                {"net.minecraft.world.level.BlockGetter", "getFluidState", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelAccessor", "getRandom", ""},
                {"net.minecraft.world.level.BlockGetter", "getBlockEntity", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelAccessor", "getLevelData", ""},
                {"net.minecraft.world.level.LevelAccessor", "nextSubTickCount", ""},
                {"net.minecraft.world.level.ScheduledTickAccess", "getFluidTicks", ""},
                {"net.minecraft.world.level.ScheduledTickAccess", "getBlockTicks", ""},
                {"net.minecraft.world.level.LevelSimulatedReader", "isStateAtPosition", "net.minecraft.core.BlockPos,java.util.function.Predicate"},
                {"net.minecraft.world.level.LevelSimulatedReader", "isFluidAtPosition", "net.minecraft.core.BlockPos,java.util.function.Predicate"},
                {"net.minecraft.world.level.BlockGetter", "getBlockState", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelWriter", "setBlock", "net.minecraft.core.BlockPos,net.minecraft.world.level.block.state.BlockState,int"},
                {"net.minecraft.world.level.LevelWriter", "setBlock", "net.minecraft.core.BlockPos,net.minecraft.world.level.block.state.BlockState,int,int"},
                {"net.minecraft.world.level.LevelReader", "getHeight", HM_TYPES + ",int,int"},
                {"net.minecraft.world.level.WorldGenLevel", "ensureCanWrite", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelHeightAccessor", "isOutsideBuildHeight", "int"},
                {"net.minecraft.world.level.LevelHeightAccessor", "getMinY", ""},
                {"net.minecraft.world.level.LevelHeightAccessor", "getHeight", ""},
                {"net.minecraft.world.level.LevelHeightAccessor", "getMaxY", ""},
                {"net.minecraft.world.level.LevelReader", "getSeaLevel", ""},
                {"net.minecraft.world.level.WorldGenLevel", "getSeed", ""},
                {"net.minecraft.world.level.LevelReader", "getBiome", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelReader", "isEmptyBlock", "net.minecraft.core.BlockPos"},
                {"net.minecraft.world.level.LevelReader", "hasChunk", "int,int"},
                {"net.minecraft.world.level.LevelHeightAccessor", "getMinSectionY", ""},
                {"net.minecraft.world.level.LevelHeightAccessor", "getMaxSectionY", ""},
                {"net.minecraft.world.level.LevelHeightAccessor", "getSectionsCount", ""},
                {"net.minecraft.world.level.LevelHeightAccessor", "getSectionIndex", "int"},
                {"net.minecraft.world.level.LevelHeightAccessor", "getSectionIndexFromSectionY", "int"},
                {"net.minecraft.world.level.LevelHeightAccessor", "isInsideBuildHeight", "int"},
        };
        for (String[] w : wanted) {
            String key = w[0] + "#" + w[1] + "(" + w[2] + ")";
            String obf = WorldgenRefDump.METHOD_OBF.get(key);
            if (obf == null) {
                System.err.println("WARN: no mapping for " + key);
                continue;
            }
            StringBuilder sb = new StringBuilder(obf).append('(');
            if (!w[2].isEmpty()) {
                String[] ps = w[2].split(",");
                for (int i = 0; i < ps.length; i++) sb.append(i > 0 ? "," : "").append(mapType(ps[i]));
            }
            sigToMoj.put(sb.append(')').toString(), w[1] + "/" + w[2].split(",").length);
        }

        try (PrintStream treeRows = System.getenv("TREE_TEST") != null ? new PrintStream(outDir.resolve("trees" + tag + ".csv").toFile()) : null;
             PrintStream order = new PrintStream(outDir.resolve("feature_order" + tag + ".csv").toFile());
             PrintStream rows = new PrintStream(outDir.resolve("features" + tag + ".csv").toFile());
             PrintStream stats = new PrintStream(outDir.resolve("feature_stats" + tag + ".csv").toFile())) {
            if (treeRows != null) treeRows.println("seed,target_x,target_z,tree,k,x,y,z,placed,count,hash,feature_seed");
            order.println("step,index,placed_feature");
            rows.println("seed,target_x,target_z,dx,dz,section,name_hash");
            stats.println("seed,target_x,target_z,placed_feature,calls,placed");

            List<?> steps = (List<?>) stepData;
            for (int s = 0; s < steps.size(); s++) {
                List<?> fl = (List<?>) sfdFeatures.invoke(steps.get(s));
                for (int i = 0; i < fl.size(); i++) order.printf("%d,%d,%s%n", s, i, pfNames.get(fl.get(i)));
            }

            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider, owKey, seed);
                Object sampler = samplerOf.invoke(state);
                Object surfaceSystem = surfaceSystemOf.invoke(state);
                WorldgenSurfaceDump.BiomeStore genStore = new WorldgenSurfaceDump.BiomeStore(freshList.get(), sampler, biomeGetter, findValue, sampleM, getOrThrow);
                Object source = WorldgenCarverDump.proxy(NBS, (p, m, a) -> {
                    if (m.getName().equals(nbsName)) return genStore.at((Integer) a[0], (Integer) a[1], (Integer) a[2]);
                    if (m.isDefault()) return InvocationHandler.invokeDefault(p, m, a);
                    return null;
                });
                long zoomSeed = (Long) obfuscateSeed.invoke(null, seed);
                Object biomeManager = WorldgenCarverDump.construct("net.minecraft.world.level.biome.BiomeManager", 2, source, zoomSeed);
                Function<Object, Object> managerGetter = pos -> {
                    try {
                        return getBiome.invoke(biomeManager, pos);
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                };
                Map<Long, Object> chunkCache = new HashMap<>();

                java.util.List<int[]> targetList = new ArrayList<>();
                if (System.getenv("FEATURE_TARGETS") != null) {
                    // FEATURE_TARGETS="seed:x,z;seed:x,z": decorate these targets instead of the default six
                    for (String t : System.getenv("FEATURE_TARGETS").split(";")) {
                        String[] sp = t.split(":");
                        String[] xz = sp[1].split(",");
                        if (Long.parseLong(sp[0]) == seed) targetList.add(new int[]{Integer.parseInt(xz[0]), Integer.parseInt(xz[1])});
                    }
                } else {
                    for (int[] tc0 : TARGETS) targetList.add(tc0);
                }
                for (int[] tc : targetList) {
                    int tx = tc[0], tz = tc[1];
                    // ---- region of 3 x 3 pipeline chunks ------------------------------------------------------------
                    Map<Long, Object> region = new HashMap<>();
                    for (int dz = -1; dz <= 1; dz++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            int cx = tx + dx, cz = tz + dz;
                            long key = ((long) cx << 32) ^ (cz & 0xffffffffL);
                            Object chunk = chunkCache.get(key);
                            if (chunk == null) {
                                int minX = cx * 16, minZ = cz * 16;
                                Object nc = WorldgenCarverDump.construct(NC, 9, 16 / 4, state, minX, minZ, noiseSettings, beardifier, settings, fluidPicker, blender);
                                int k = (Integer) cw.invoke(nc);
                                int l = (Integer) ch.invoke(nc);
                                int minCellY = Math.floorDiv(minY, l);
                                int cellCountY = Math.floorDiv(height, l);
                                Object chunkPos = WorldgenCarverDump.construct("net.minecraft.world.level.ChunkPos", 2, cx, cz);
                                chunk = WorldgenCarverDump.construct("net.minecraft.world.level.chunk.ProtoChunk", 5, chunkPos, field("net.minecraft.world.level.chunk.UpgradeData", "EMPTY", null), heightAccessor, factory, null);
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
                                                int sy = (minCellY + q) * l + r;
                                                updY.invoke(nc, sy, (double) r / l);
                                                for (int v = 0; v < k; v++) {
                                                    int w = minX + o * k + v;
                                                    updX.invoke(nc, w, (double) v / k);
                                                    for (int y = 0; y < k; y++) {
                                                        int z = minZ + p * k + y;
                                                        updZ.invoke(nc, z, (double) y / k);
                                                        Object bs = interp.invoke(nc);
                                                        if (bs == null) bs = defaultBlock;
                                                        int yy = sy - minY;
                                                        sectionSet.invoke(sections[yy >> 4], w & 15, yy & 15, z & 15, bs, false);
                                                        if (!nameCache.computeIfAbsent(bs, WorldgenCarverDump::blockName).equals("minecraft:air")) {
                                                            hmUpdate.invoke(hmOcean, w & 15, sy, z & 15, bs);
                                                            hmUpdate.invoke(hmSurface, w & 15, sy, z & 15, bs);
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    swap.invoke(nc);
                                }
                                stop.invoke(nc);
                                genStore.prepare(cx, cz);
                                buildSurface.invoke(surfaceSystem, state, biomeManager, null, false, genContext, chunk, nc, surfaceRule);
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
                                        Object bkey = findValue.invoke(carverList, sampleM.invoke(sampler, bx, 0, bz));
                                        Object holder = getOrThrow.invoke(biomeGetter, bkey);
                                        Object gs = genSettings.invoke(biomeValue.invoke(holder));
                                        int m = 0;
                                        for (Object carverHolder : (Iterable<?>) getCarvers.invoke(gs)) {
                                            Object cwc = biomeValue.invoke(carverHolder);
                                            Object cfg = cwcConfig.invoke(cwc);
                                            if (patched.add(cfg)) replaceField.set(cfg, replaceSet);
                                            setLarge.invoke(random, seed + (long) m, cx + j, cz + kk);
                                            if ((Boolean) isStart.invoke(cwc, random)) carve.invoke(cwc, ctx, chunk, managerGetter, random, aquifer, cp2, mask);
                                            m++;
                                        }
                                    }
                                }
                                chunkCache.put(key, chunk);
                                System.err.println("seed " + seed + " pipeline chunk " + cx + "," + cz);
                            }
                            region.put(key, chunk);
                        }
                    }
                    genStore.prepare(tx - 1, tz - 1);
                    genStore.prepare(tx + 1, tz + 1);
                    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) genStore.chunk(tx + dx, tz + dz);

                    // Work on copies of the region chunks is not possible (ProtoChunk has no clone), so the
                    // decoration of one target modifies the cache; targets are chosen far enough apart (and
                    // each target uses a fresh cache entry set) that this does not matter. Clear the cache.
                    final Map<Long, Object> regionF = region;
                    final int minXr = (tx - 1) * 16, maxXr = (tx + 2) * 16 - 1, minZr = (tz - 1) * 16, maxZr = (tz + 2) * 16 - 1;
                    final Map<Long, Object> touched = new java.util.TreeMap<>();
                    final Map<Long, Object> blockEntities = new HashMap<>();
                    final Function<Object, int[]> posOf = pos -> {
                        try {
                            return new int[]{(Integer) pos.getClass().getMethod(WorldgenRefDump.METHOD_OBF.get("net.minecraft.core.Vec3i#getX()")).invoke(pos),
                                    (Integer) pos.getClass().getMethod(WorldgenRefDump.METHOD_OBF.get("net.minecraft.core.Vec3i#getY()")).invoke(pos),
                                    (Integer) pos.getClass().getMethod(WorldgenRefDump.METHOD_OBF.get("net.minecraft.core.Vec3i#getZ()")).invoke(pos)};
                        } catch (Exception e) {
                            throw new RuntimeException(e);
                        }
                    };
                    Object level = Proxy.newProxyInstance(WorldgenRefDump.cls(WGL).getClassLoader(), new Class<?>[]{WorldgenRefDump.cls(WGL)}, (proxy, m, a) -> {
                        StringBuilder sb = new StringBuilder(m.getName()).append('(');
                        Class<?>[] pt = m.getParameterTypes();
                        for (int i = 0; i < pt.length; i++) sb.append(i > 0 ? "," : "").append(pt[i].getName());
                        String sig = sb.append(')').toString();
                        String moj = sigToMoj.get(sig);
                        if (moj == null) {
                            if (m.getName().equals("hashCode")) return 1;
                            if (m.getName().equals("equals")) return proxy == a[0];
                            if (m.getName().equals("toString")) return "levelProxy";
                            if (m.isDefault()) return InvocationHandler.invokeDefault(proxy, m, a);
                            StringBuilder cand = new StringBuilder();
                            for (Map.Entry<String, String> me : WorldgenRefDump.METHOD_OBF.entrySet())
                                if (me.getValue().equals(m.getName()) && me.getKey().contains("Level")) cand.append(' ').append(me.getKey());
                            throw new UnsupportedOperationException("WorldGenLevel." + sig + " candidates:" + cand);
                        }
                        String name = moj.substring(0, moj.indexOf('/'));
                        switch (name) {
                            case "getChunk": {
                                long key = (((long) (Integer) a[0]) << 32) ^ (((Integer) a[1]) & 0xffffffffL);
                                Object c = regionF.get(key);
                                if (c == null) throw new IllegalStateException("chunk outside region " + a[0] + "," + a[1]);
                                return c;
                            }
                            case "hasChunk": {
                                long key = (((long) (Integer) a[0]) << 32) ^ (((Integer) a[1]) & 0xffffffffL);
                                return regionF.containsKey(key);
                            }
                            case "getBlockState": {
                                int[] p = posOf.apply(a[0]);
                                if (p[1] < minY || p[1] >= minY + height) return airState;
                                return chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                            }
                            case "getFluidTicks":
                            case "getBlockTicks":
                                return WorldgenCarverDump.proxy("net.minecraft.world.ticks.LevelTickAccess", (tp, tm, ta) -> {
                                    if (tm.getReturnType() == boolean.class) return false;
                                    if (tm.getReturnType() == int.class) return 0;
                                    return null;
                                });
                            case "getLevelData":
                                return WorldgenCarverDump.proxy("net.minecraft.world.level.storage.LevelData", (tp, tm, ta) -> {
                                    Class<?> rt = tm.getReturnType();
                                    if (rt == boolean.class) return false;
                                    if (rt == int.class) return 0;
                                    if (rt == long.class) return 0L;
                                    if (rt == float.class) return 0f;
                                    if (rt == double.class) return 0d;
                                    return null;
                                });
                            case "nextSubTickCount": return 0L;
                            case "getBlockEntity": {
                                // WorldGenRegion.getBlockEntity: the ProtoChunk only stores a stub, the entity is created on demand
                                int[] p = posOf.apply(a[0]);
                                if (p[1] < minY || p[1] >= minY + height) return null;
                                Long bk = touchKey(p[0], p[1], p[2]);
                                if (blockEntities.containsKey(bk)) return blockEntities.get(bk);
                                Object st = chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                                Object blk = bsGetBlock.invoke(st);
                                if (!entityBlockC.isInstance(blk)) return null;
                                Object be = newBlockEntity.invoke(blk, a[0], st);
                                blockEntities.put(bk, be);
                                return be;
                            }
                            case "getRandom":
                                return WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.LegacyRandomSource", 1, 0L);
                            case "getFluidState": {
                                int[] p = posOf.apply(a[0]);
                                Object st = p[1] < minY || p[1] >= minY + height ? airState
                                        : chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                                return bsFluidState.invoke(st);
                            }
                            case "isStateAtPosition": {
                                int[] p = posOf.apply(a[0]);
                                Object st = p[1] < minY || p[1] >= minY + height ? airState
                                        : chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                                return ((java.util.function.Predicate<Object>) a[1]).test(st);
                            }
                            case "isFluidAtPosition": {
                                int[] p = posOf.apply(a[0]);
                                Object st = p[1] < minY || p[1] >= minY + height ? airState
                                        : chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                                return ((java.util.function.Predicate<Object>) a[1]).test(bsFluidState.invoke(st));
                            }
                            case "setBlock": {
                                int[] p = posOf.apply(a[0]);
                                if (p[1] < minY || p[1] >= minY + height) return false;
                                if (p[0] < minXr || p[0] > maxXr || p[2] < minZr || p[2] > maxZr) return false;
                                Object wch = regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL));
                                long tk = touchKey(p[0], p[1], p[2]);
                                if (!touched.containsKey(tk)) touched.put(tk, chunkGetState.invoke(wch, a[0]));
                                chunkSetState.invoke(wch, a[0], a[1], (Integer) a[2]);
                                return true;
                            }
                            case "getHeight": {
                                if (a == null || a.length == 0) return height;
                                int x = (Integer) a[1], z = (Integer) a[2];
                                return (Integer) chunkHeight.invoke(regionF.get((((long) (x >> 4)) << 32) ^ ((z >> 4) & 0xffffffffL)), a[0], x & 15, z & 15) + 1;
                            }
                            case "ensureCanWrite": {
                                int[] p = posOf.apply(a[0]);
                                return p[0] >= minXr && p[0] <= maxXr && p[2] >= minZr && p[2] <= maxZr;
                            }
                            case "isOutsideBuildHeight": return (Integer) a[0] < minY || (Integer) a[0] >= minY + height;
                            case "getMinY": return minY;
                            case "getMaxY": return minY + height - 1;
                            case "getMinSectionY": return minY >> 4;
                            case "getMaxSectionY": return (minY + height - 1) >> 4;
                            case "getSectionsCount": return height / 16;
                            case "getSectionIndex": return ((Integer) a[0] - minY) >> 4;
                            case "getSectionIndexFromSectionY": return (Integer) a[0] - (minY >> 4);
                            case "isInsideBuildHeight": return (Integer) a[0] >= minY && (Integer) a[0] < minY + height;
                            case "getSeaLevel": return 63;
                            case "getSeed": return seed;
                            case "getBiome": return managerGetter.apply(a[0]);
                            case "isEmptyBlock": {
                                int[] p = posOf.apply(a[0]);
                                if (p[1] < minY || p[1] >= minY + height) return true;
                                Object st = chunkGetState.invoke(regionF.get((((long) (p[0] >> 4)) << 32) ^ ((p[2] >> 4) & 0xffffffffL)), a[0]);
                                return String.valueOf(st).contains("minecraft:air");
                            }
                            default: throw new UnsupportedOperationException("WorldGenLevel." + sig + " -> " + name);
                        }
                    });

                    {
                        // ChunkStatusTasks.generateFeatures: Heightmap.primeHeightmaps(chunk, {MOTION_BLOCKING, MOTION_BLOCKING_NO_LEAVES, OCEAN_FLOOR, WORLD_SURFACE})
                        Class<?> typesC = WorldgenRefDump.cls(HM_TYPES);
                        @SuppressWarnings({"unchecked", "rawtypes"})
                        java.util.EnumSet postSet = java.util.EnumSet.noneOf((Class) typesC);
                        for (String tn : new String[]{"MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES", "OCEAN_FLOOR", "WORLD_SURFACE"}) postSet.add(field(HM_TYPES, tn, null));
                        WorldgenRefDump.method("net.minecraft.world.level.levelgen.Heightmap", "primeHeightmaps", "net.minecraft.world.level.chunk.ChunkAccess,java.util.Set")
                                .invoke(null, region.get(((long) tx << 32) ^ (tz & 0xffffffffL)), postSet);
                    }
                    String featFull = System.getenv("FEAT_FULL");
                    String[] ff = featFull != null ? featFull.split(",") : null;
                    boolean dumpThis = ff != null && Long.parseLong(ff[0]) == seed && Integer.parseInt(ff[1]) == tx && Integer.parseInt(ff[2]) == tz;
                    String[][] before = null;
                    IdentityHashMap<Object, String> nameCache0 = new IdentityHashMap<>();
                    if (dumpThis) {
                        before = new String[9][16 * 16 * 384];
                        for (int dz = -1; dz <= 1; dz++)
                            for (int dx = -1; dx <= 1; dx++) {
                                Object chunk = region.get(((long) (tx + dx) << 32) ^ ((tz + dz) & 0xffffffffL));
                                int slot = (dz + 1) * 3 + (dx + 1);
                                for (int sec = 0; sec < 24; sec++) {
                                    Object section = getSection.invoke(chunk, sec);
                                    for (int yy = 0; yy < 16; yy++)
                                        for (int z = 0; z < 16; z++)
                                            for (int x = 0; x < 16; x++)
                                                before[slot][(x * 16 + z) * 384 + sec * 16 + yy] = nameCache0.computeIfAbsent(sectionGet.invoke(section, x, yy, z), WorldgenCarverDump::blockName);
                                }
                            }
                    }
                    // ---- ChunkGenerator.applyBiomeDecoration (filtered) --------------------------------------------------
                    Object random = WorldgenCarverDump.construct("net.minecraft.world.level.levelgen.WorldgenRandom", 1,
                            WorldgenRefDump.cls("net.minecraft.world.level.levelgen.XoroshiroRandomSource").getConstructor(long.class).newInstance(0L));
                    long decorationSeed = (Long) setDecorationSeed.invoke(random, seed, tx * 16, tz * 16);
                    java.util.LinkedHashSet<Object> biomes = new java.util.LinkedHashSet<>();
                    for (int dz = -1; dz <= 1; dz++)
                        for (int dx = -1; dx <= 1; dx++) java.util.Collections.addAll(biomes, genStore.chunk(tx + dx, tz + dz));
                    Object origin = bpCtor.newInstance(tx * 16, minY, tz * 16);
                    Map<String, int[]> callStats = new LinkedHashMap<>();
                    if (treeRows != null) {
                        runTreeTest(treeRows, seed, tx, tz, level, decoGenerator, touched);
                    } else
                    decorate(steps, step -> step, biomes, bgsFeatures, biomeGenSettings, biomeValue, holderSetStream, sfdFeatures, sfdIndex, toIntApply, setFeatureSeed, placeWithCheck, pfFeature, cfFeature, featureRegistry, regGetKey, pfNames, allowed, random, decorationSeed, level, decoGenerator, origin, callStats);
                    for (Map.Entry<String, int[]> e : callStats.entrySet()) stats.printf("%d,%d,%d,%s,%d,%d%n", seed, tx, tz, e.getKey(), e.getValue()[0], e.getValue()[1]);

                    if (dumpThis) {
                        try (PrintStream fo = new PrintStream(ff[3])) {
                            fo.println("x,y,z,block");
                            for (int dz = -1; dz <= 1; dz++)
                                for (int dx = -1; dx <= 1; dx++) {
                                    Object chunk = region.get(((long) (tx + dx) << 32) ^ ((tz + dz) & 0xffffffffL));
                                    int slot = (dz + 1) * 3 + (dx + 1);
                                    for (int sec = 0; sec < 24; sec++) {
                                        Object section = getSection.invoke(chunk, sec);
                                        for (int yy = 0; yy < 16; yy++)
                                            for (int z = 0; z < 16; z++)
                                                for (int x = 0; x < 16; x++) {
                                                    String nm = nameCache0.computeIfAbsent(sectionGet.invoke(section, x, yy, z), WorldgenCarverDump::blockName);
                                                    if (!nm.equals(before[slot][(x * 16 + z) * 384 + sec * 16 + yy]))
                                                        fo.printf("%d,%d,%d,%s%n", (tx + dx) * 16 + x, sec * 16 + yy - 64, (tz + dz) * 16 + z, nm);
                                                }
                                    }
                                }
                        }
                    }
                    // ---- results ------------------------------------------------------------------------------------------
                    IdentityHashMap<Object, String> nameCache = new IdentityHashMap<>();
                    for (int dz = -1; dz <= 1; dz++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            Object chunk = region.get(((long) (tx + dx) << 32) ^ ((tz + dz) & 0xffffffffL));
                            for (int sec = 0; sec < height / 16; sec++) {
                                Object section = getSection.invoke(chunk, sec);
                                long hn = 1125899906842597L;
                                for (int yy = 0; yy < 16; yy++)
                                    for (int z = 0; z < 16; z++)
                                        for (int x = 0; x < 16; x++)
                                            hn = 31 * hn + nameCache.computeIfAbsent(sectionGet.invoke(section, x, yy, z), WorldgenCarverDump::blockName).hashCode();
                                rows.printf("%d,%d,%d,%d,%d,%d,%d%n", seed, tx, tz, dx, dz, sec, hn);
                            }
                        }
                    }
                    rows.flush();
                    stats.flush();
                    // The decorated chunks were modified: drop them from the cache so later targets rebuild them.
                    for (Long k : region.keySet()) chunkCache.remove(k);
                    System.err.println("seed " + seed + " target " + tx + "," + tz + " decorated");
                }
            }
        }
        System.err.println("wrote feature fixtures to " + outDir);
    }

    static void decorate(List<?> steps, Function<Integer, Integer> unused, java.util.Set<Object> biomes, Method bgsFeatures, Method biomeGenSettings,
                         Method biomeValue, Method holderSetStream, Method sfdFeatures, Method sfdIndex, Method toIntApply, Method setFeatureSeed,
                         Method placeWithCheck, Method pfFeature, Method cfFeature, Object featureRegistry, Method regGetKey, Map<Object, String> pfNames,
                         java.util.Set<String> allowed, Object random, long decorationSeed, Object level, Object generator, Object origin,
                         Map<String, int[]> callStats) throws Exception {
        for (int step = 0; step < steps.size(); step++) {
            java.util.TreeSet<Integer> indexes = new java.util.TreeSet<>();
            Object data = steps.get(step);
            Object mapping = sfdIndex.invoke(data);
            for (Object holder : biomes) {
                List<?> perStep = (List<?>) bgsFeatures.invoke(biomeGenSettings.invoke(biomeValue.invoke(holder)));
                if (step >= perStep.size()) continue;
                java.util.stream.Stream<?> st = (java.util.stream.Stream<?>) holderSetStream.invoke(perStep.get(step));
                java.util.Iterator<?> stIt = st.iterator();
                while (stIt.hasNext()) {
                    Object pf = biomeValue.invoke(stIt.next());
                    indexes.add((Integer) toIntApply.invoke(mapping, pf));
                }
            }
            List<?> features = (List<?>) sfdFeatures.invoke(data);
            for (int index : indexes) {
                Object pf = features.get(index);
                Object cf = biomeValue.invoke(pfFeature.invoke(pf));
                Object feature = cfFeature.invoke(cf);
                String type = String.valueOf(regGetKey.invoke(featureRegistry, feature));
                type = type.substring(type.indexOf(':') + 1);
                if (!allowed.contains(type)) continue;
                if (NAMES != null && !NAMES.contains(pfNames.get(pf))) continue;
                String only = System.getenv("JF_ONLY");
                if (only != null && !only.equals(pfNames.get(pf))) continue;
                setFeatureSeed.invoke(random, decorationSeed, index, step);
                boolean placed = (Boolean) placeWithCheck.invoke(pf, level, generator, random, origin);
                int[] c = callStats.computeIfAbsent(pfNames.get(pf), k -> new int[2]);
                c[0]++;
                if (placed) c[1]++;
            }
        }
    }
}
