package tcref;

import java.io.PrintStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;

/**
 * Stage-4c reference dumper: runs the REAL 1.21.11 carvers.
 *
 * For every (seed, chunk): the NoiseChunk fill of WorldgenChunkDump is stored in
 * a real ProtoChunk, then NoiseBasedChunkGenerator.applyCarvers' loop is replayed
 * verbatim (17 x 17 neighbour chunks, carver biome from the multi-noise sampler
 * at quart (minX, 0, minZ), WorldgenRandom.setLargeFeatureSeed(seed + index, x, z),
 * ConfiguredWorldCarver.isStartChunk / carve with the NoiseChunk's own aquifer).
 * Surface rules are not run (their effect on carving is limited to the top
 * material swap of grass/mycelium columns).
 *
 * Output:
 *   carvers.csv        seed,chunk_x,chunk_z,section,name_hash,mask_hash   (24 sections)
 *   carver_counts.csv  seed,chunk_x,chunk_z,block,count
 *   carver_starts.csv  seed,chunk_x,chunk_z,carver_index,starts           (neighbour start chunks)
 */
public class WorldgenCarverDump {
    static final long[] SEEDS = WorldgenRouterDump.SEEDS;
    static final int[][] CHUNKS = WorldgenChunkDump.CHUNKS;

    static Object call(String cls, String name, String args, Object target, Object... a) throws Exception {
        return WorldgenRefDump.method(cls, name, args).invoke(target, a);
    }

    static Object field(String cls, String name, Object target) throws Exception {
        return WorldgenRefDump.field(cls, name, target);
    }

    static Object construct(String cls, int params, Object... a) throws Exception {
        for (Constructor<?> c : WorldgenRefDump.cls(cls).getDeclaredConstructors()) {
            if (c.getParameterCount() == params) {
                c.setAccessible(true);
                return c.newInstance(a);
            }
        }
        throw new IllegalStateException("no ctor " + cls + "/" + params);
    }

    static String blockName(Object state) {
        String s = String.valueOf(state);
        return s.substring(s.indexOf('{') + 1, s.indexOf('}'));
    }

    static Object proxy(String iface, InvocationHandler h) throws Exception {
        Class<?> c = WorldgenRefDump.cls(iface);
        return Proxy.newProxyInstance(c.getClassLoader(), new Class<?>[]{c}, h);
    }

    /** Block names of a (nested) block tag, read from the vanilla data pack tag JSON. */
    static void resolveTag(Path tagDir, String name, java.util.LinkedHashSet<String> out) throws Exception {
        String text = Files.readString(tagDir.resolve(name.substring(name.indexOf(':') + 1) + ".json"));
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("\"(#?minecraft:[a-z0-9_/]+)\"").matcher(text);
        while (m.find()) {
            String v = m.group(1);
            if (v.startsWith("#")) resolveTag(tagDir, v.substring(1), out);
            else out.add(v);
        }
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        // args[2]: vanilla data generator output (<root>/data/minecraft/tags/...), e.g. reference/1.21.11/run/out
        Files.createDirectories(outDir);

        call("net.minecraft.SharedConstants", "tryDetectVersion", "", null);
        call("net.minecraft.server.Bootstrap", "bootStrap", "", null);
        Object provider = call("net.minecraft.data.registries.VanillaRegistries", "createLookup", "", null);

        // ---- settings, biomes -----------------------------------------------------------
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

        // The registry lookup used here has no tags bound, so #overworld_carver_replaceables would
        // match nothing. Build the HolderSet from the data pack tag JSON instead.
        Path tagDir = Paths.get(args[2]).resolve("data/minecraft/tags/block");
        java.util.LinkedHashSet<String> replaceNames = new java.util.LinkedHashSet<>();
        resolveTag(tagDir, "minecraft:overworld_carver_replaceables", replaceNames);
        Object blockRegistry = field("net.minecraft.core.registries.BuiltInRegistries", "BLOCK", null);
        Method regGet = WorldgenRefDump.method("net.minecraft.core.Registry", "get", "net.minecraft.resources.Identifier");
        Method identParse = WorldgenRefDump.method("net.minecraft.resources.Identifier", "parse", "java.lang.String");
        java.util.List<Object> replaceHolders = new java.util.ArrayList<>();
        for (String n : replaceNames) {
            Object opt = regGet.invoke(blockRegistry, identParse.invoke(null, n));
            replaceHolders.add(((java.util.Optional<?>) opt).orElseThrow());
        }
        Object replaceSet = call("net.minecraft.core.HolderSet", "direct", "java.util.List", null, replaceHolders);
        java.lang.reflect.Field replaceField = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.carver.CarverConfiguration")
                .getDeclaredField(WorldgenRefDump.FIELD_OBF.get("net.minecraft.world.level.levelgen.carver.CarverConfiguration#replaceable"));
        replaceField.setAccessible(true);
        Method cwcConfig = WorldgenRefDump.method("net.minecraft.world.level.levelgen.carver.ConfiguredWorldCarver", "config", "");
        java.util.Set<Object> patched = java.util.Collections.newSetFromMap(new IdentityHashMap<>());
        System.err.println("carver replaceable blocks: " + replaceNames.size());

        // Preset.OVERWORLD parameter list (as in WorldgenBiomeDump).
        String PRESET = "net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList$Preset";
        Object preset = field(PRESET, "OVERWORLD", null);
        Object pprovider = call(PRESET, "provider", "", preset);
        Object paramList = call(PRESET + "$SourceProvider", "apply", "java.util.function.Function", pprovider, (Function<Object, Object>) Function.identity());
        Method findValue = WorldgenRefDump.method("net.minecraft.world.level.biome.Climate$ParameterList", "findValue", "net.minecraft.world.level.biome.Climate$TargetPoint");
        Method sampleM = WorldgenRefDump.method("net.minecraft.world.level.biome.Climate$Sampler", "sample", "int,int,int");

        // ---- chunk storage ---------------------------------------------------------------
        final int minY = -64, height = 384;
        String LHA = "net.minecraft.world.level.LevelHeightAccessor";
        String hGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getHeight()");
        String yGet = WorldgenRefDump.METHOD_OBF.get(LHA + "#getMinY()");
        Object heightAccessor = proxy(LHA, (p, m, a) -> {
            if (m.getName().equals(hGet)) return height;
            if (m.getName().equals(yGet)) return minY;
            if (m.isDefault()) return InvocationHandler.invokeDefault(p, m, a);
            if (m.getName().equals("hashCode")) return 1;
            if (m.getName().equals("equals")) return p == a[0];
            return null;
        });
        // A one-entry biome id map is enough: every section holds only the default biome.
        String IDMAP = "net.minecraft.core.IdMap";
        String sizeN = WorldgenRefDump.METHOD_OBF.get(IDMAP + "#size()");
        Object oneIdMap = proxy(IDMAP, (p, m, a) -> {
            if (m.getName().equals(sizeN)) return 1;
            if (m.isDefault()) return InvocationHandler.invokeDefault(p, m, a);
            return null;
        });
        String STRAT = "net.minecraft.world.level.chunk.Strategy";
        Object blockStrategy = call(STRAT, "createForBlockStates", "net.minecraft.core.IdMap", null,
                field("net.minecraft.world.level.block.Block", "BLOCK_STATE_REGISTRY", null));
        Object biomeStrategy = call(STRAT, "createForBiomes", "net.minecraft.core.IdMap", null, oneIdMap);
        Object airState = call("net.minecraft.world.level.block.Block", "defaultBlockState", "", field("net.minecraft.world.level.block.Blocks", "AIR", null));
        Object factory = construct("net.minecraft.world.level.chunk.PalettedContainerFactory", 6, blockStrategy, airState, null, biomeStrategy, plainsHolder, null);

        Method sectionSet = WorldgenRefDump.method("net.minecraft.world.level.chunk.LevelChunkSection", "setBlockState", "int,int,int,net.minecraft.world.level.block.state.BlockState,boolean");
        Method sectionGet = WorldgenRefDump.method("net.minecraft.world.level.chunk.LevelChunkSection", "getBlockState", "int,int,int");
        Method getSection = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkAccess", "getSection", "int");
        Method maskGet = WorldgenRefDump.method("net.minecraft.world.level.chunk.CarvingMask", "get", "int,int,int");

        // ---- noise fill machinery (same as WorldgenChunkDump) -----------------------------
        String NBCG = "net.minecraft.world.level.levelgen.NoiseBasedChunkGenerator";
        Class<?> ngsC = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings");
        Method createFluidPicker = WorldgenRefDump.cls(NBCG).getDeclaredMethod(WorldgenRefDump.METHOD_OBF.get(NBCG + "#createFluidPicker(net.minecraft.world.level.levelgen.NoiseGeneratorSettings)"), ngsC);
        createFluidPicker.setAccessible(true);
        Object fluidPicker = createFluidPicker.invoke(null, settings);
        Object beardifier = field("net.minecraft.world.level.levelgen.DensityFunctions$BeardifierMarker", "INSTANCE", null);
        Object blender = call("net.minecraft.world.level.levelgen.blending.Blender", "empty", "", null);
        Method create = WorldgenRefDump.method("net.minecraft.world.level.levelgen.RandomState", "create", "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");
        Method samplerOf = WorldgenRefDump.method("net.minecraft.world.level.levelgen.RandomState", "sampler", "");

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

        Object generator = construct(NBCG, 2, construct("net.minecraft.world.level.biome.FixedBiomeSource", 1, plainsHolder), settingsHolder);
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
        Function<Object, Object> plainsGetter = pos -> plainsHolder;

        final String AIR = "minecraft:air", WATER = "minecraft:water", LAVA = "minecraft:lava";
        try (PrintStream out = new PrintStream(outDir.resolve("carvers.csv").toFile());
             PrintStream cnt = new PrintStream(outDir.resolve("carver_counts.csv").toFile());
             PrintStream starts = new PrintStream(outDir.resolve("carver_starts.csv").toFile())) {
            out.println("seed,chunk_x,chunk_z,section,name_hash,mask_hash");
            cnt.println("seed,chunk_x,chunk_z,block,count");
            starts.println("seed,chunk_x,chunk_z,carver_index,starts");
            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider, owKey, seed);
                Object sampler = samplerOf.invoke(state);
                for (int[] cc : CHUNKS) {
                    int cx = cc[0], cz = cc[1];
                    int minX = cx * 16, minZ = cz * 16;
                    Object nc = construct(NC, 9, 16 / 4, state, minX, minZ, noiseSettings, beardifier, settings, fluidPicker, blender);
                    int k = (Integer) cw.invoke(nc);
                    int l = (Integer) ch.invoke(nc);
                    int minCellY = Math.floorDiv(minY, l);
                    int cellCountY = Math.floorDiv(height, l);

                    Object chunkPos = construct("net.minecraft.world.level.ChunkPos", 2, cx, cz);
                    Object chunk = construct("net.minecraft.world.level.chunk.ProtoChunk", 5, chunkPos, field("net.minecraft.world.level.chunk.UpgradeData", "EMPTY", null), heightAccessor, factory, null);
                    Object[] sections = new Object[height / 16];
                    for (int i = 0; i < sections.length; i++) sections[i] = getSection.invoke(chunk, i);

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
                                        }
                                    }
                                }
                            }
                        }
                        swap.invoke(nc);
                    }
                    stop.invoke(nc);

                    // ---- NoiseBasedChunkGenerator.applyCarvers ----------------------------------
                    Object random = construct("net.minecraft.world.level.levelgen.WorldgenRandom", 1,
                            construct("net.minecraft.world.level.levelgen.LegacyRandomSource", 1, 0L));
                    Object aquifer = aquiferOf.invoke(nc);
                    Object ctx = construct("net.minecraft.world.level.levelgen.carver.CarvingContext", 6,
                            generator, null, heightAccessor, nc, state, surfaceRule);
                    Object mask = getOrCreateMask.invoke(chunk);
                    int[] startCounts = new int[8];
                    for (int j = -8; j <= 8; j++) {
                        for (int kk = -8; kk <= 8; kk++) {
                            Object cp2 = construct("net.minecraft.world.level.ChunkPos", 2, cx + j, cz + kk);
                            int bx = (Integer) chunkPosMinX.invoke(cp2) >> 2;
                            int bz = (Integer) chunkPosMinZ.invoke(cp2) >> 2;
                            Object target = sampleM.invoke(sampler, bx, 0, bz);
                            Object biomeKey = findValue.invoke(paramList, target);
                            Object holder = call("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey", biomeGetter, biomeKey);
                            Object gs = genSettings.invoke(biomeValue.invoke(holder));
                            int m = 0;
                            for (Object carverHolder : (Iterable<?>) getCarvers.invoke(gs)) {
                                Object cwc = biomeValue.invoke(carverHolder);
                                Object cfg = cwcConfig.invoke(cwc);
                                if (patched.add(cfg)) replaceField.set(cfg, replaceSet);
                                setLarge.invoke(random, seed + (long) m, cx + j, cz + kk);
                                if ((Boolean) isStart.invoke(cwc, random)) {
                                    startCounts[Math.min(m, 7)]++;
                                    carve.invoke(cwc, ctx, chunk, plainsGetter, random, aquifer, cp2, mask);
                                }
                                m++;
                            }
                        }
                    }
                    for (int m = 0; m < 8; m++) if (startCounts[m] > 0) starts.printf("%d,%d,%d,%d,%d%n", seed, cx, cz, m, startCounts[m]);

                    // ---- read back --------------------------------------------------------------
                    IdentityHashMap<Object, String> nameCache = new IdentityHashMap<>();
                    Map<String, Integer> counts = new LinkedHashMap<>();
                    for (int sec = 0; sec < height / 16; sec++) {
                        long hn = 1125899906842597L, hm = 1125899906842597L;
                        for (int yy = 0; yy < 16; yy++) {
                            int y = sec * 16 + yy;
                            for (int z = 0; z < 16; z++) {
                                for (int x = 0; x < 16; x++) {
                                    Object bs = sectionGet.invoke(sections[sec], x, yy, z);
                                    String nm = nameCache.computeIfAbsent(bs, WorldgenCarverDump::blockName);
                                    hn = 31 * hn + nm.hashCode();
                                    boolean mk = (Boolean) maskGet.invoke(mask, x, y + minY, z);
                                    hm = 31 * hm + (mk ? 1 : 0);
                                    counts.merge(nm, 1, Integer::sum);
                                }
                            }
                        }
                        out.printf("%d,%d,%d,%d,%d,%d%n", seed, cx, cz, sec, hn, hm);
                    }
                    for (Map.Entry<String, Integer> en : counts.entrySet())
                        cnt.printf("%d,%d,%d,%s,%d%n", seed, cx, cz, en.getKey(), en.getValue());
                    out.flush();
                    System.err.println("seed " + seed + " chunk " + cx + "," + cz + " starts " + java.util.Arrays.toString(startCounts));
                }
            }
        }
        System.err.println("wrote carver fixtures to " + outDir);
    }
}
