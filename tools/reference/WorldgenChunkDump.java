package tcref;

import java.io.PrintStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Stage-4 reference dumper: runs the REAL 1.21.11 NoiseChunk (cell
 * interpolation, flat caches, aquifers, ore veins) with the same loop as
 * NoiseBasedChunkGenerator.doFill, and records the block state of every block
 * of a chunk (before surface rules, features and carvers).
 *
 * Output:
 *   chunks.csv  seed,chunkX,chunkZ,section,solid_hash,name_hash  (24 sections)
 *   chunk_counts.csv  seed,chunkX,chunkZ,blockname,count
 * Optional 3rd arg: directory for full per-block RLE dumps (debugging only).
 */
public class WorldgenChunkDump {
    static final long[] SEEDS = WorldgenRouterDump.SEEDS;
    static final int[][] CHUNKS = {{0, 0}, {-1, -1}, {5, -7}, {31, 12}, {-20, 3}, {100, 100}, {-64, 17}, {7, -130},
            {250, -33}, {-200, -200}, {13, 13}, {-5, 40}, {64, -64}, {3, 99}, {-99, -3}, {150, 1}};

    static Object call(String cls, String name, String args, Object target, Object... a) throws Exception {
        return WorldgenRefDump.method(cls, name, args).invoke(target, a);
    }

    static String blockName(Object state) {
        String s = String.valueOf(state);
        int i = s.indexOf('{');
        int j = s.indexOf('}');
        return s.substring(i + 1, j);
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Path fullDir = args.length > 2 ? Paths.get(args[2]) : null;
        Files.createDirectories(outDir);
        if (fullDir != null) Files.createDirectories(fullDir);

        call("net.minecraft.SharedConstants", "tryDetectVersion", "", null);
        call("net.minecraft.server.Bootstrap", "bootStrap", "", null);
        Object provider = call("net.minecraft.data.registries.VanillaRegistries", "createLookup", "", null);

        // NoiseGeneratorSettings for the overworld.
        Class<?> regC = WorldgenRefDump.cls("net.minecraft.core.registries.Registries");
        Field nsf = regC.getDeclaredField("bl");
        nsf.setAccessible(true);
        Object noiseSettingsRegKey = nsf.get(null);
        Object getter = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, noiseSettingsRegKey);
        Class<?> ngsC = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings");
        Field ow = ngsC.getDeclaredField("c");
        ow.setAccessible(true);
        Object owKey = ow.get(null);
        Object holder = call("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey", getter, owKey);
        Object settings = call("net.minecraft.core.Holder", "value", "", holder);
        Object noiseSettings = call("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "noiseSettings", "", settings);

        String NBCG = "net.minecraft.world.level.levelgen.NoiseBasedChunkGenerator";
        Method createFluidPicker = WorldgenRefDump.cls(NBCG).getDeclaredMethod("a", ngsC);
        createFluidPicker.setAccessible(true);
        Object fluidPicker = createFluidPicker.invoke(null, settings);

        Class<?> beardC = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.DensityFunctions$BeardifierMarker");
        Field bi = beardC.getDeclaredField("a");
        bi.setAccessible(true);
        Object beardifier = bi.get(null);
        Object blender = call("net.minecraft.world.level.levelgen.blending.Blender", "empty", "", null);

        String RS = "net.minecraft.world.level.levelgen.RandomState";
        Method create = WorldgenRefDump.method(RS, "create", "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");

        String NC = "net.minecraft.world.level.levelgen.NoiseChunk";
        Class<?> ncC = WorldgenRefDump.cls(NC);
        Constructor<?> ncCtor = null;
        for (Constructor<?> c : ncC.getDeclaredConstructors()) {
            if (c.getParameterCount() == 9) ncCtor = c;
        }
        ncCtor.setAccessible(true);
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
        Object defaultBlock = call("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "defaultBlock", "", settings);

        Field ccf = ncC.getDeclaredField("i");
        ccf.setAccessible(true);
        Method dfCompute = WorldgenRefDump.method("net.minecraft.world.level.levelgen.DensityFunction", "compute",
                "net.minecraft.world.level.levelgen.DensityFunction$FunctionContext");
        final int minY = -64, height = 384;
        try (PrintStream out = new PrintStream(outDir.resolve("chunks.csv").toFile());
             PrintStream cnt = new PrintStream(outDir.resolve("chunk_counts.csv").toFile())) {
            out.println("seed,chunk_x,chunk_z,section,solid_hash,name_hash,density_hash");
            cnt.println("seed,chunk_x,chunk_z,block,count");
            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider, owKey, seed);
                for (int[] cc : CHUNKS) {
                    int cx = cc[0], cz = cc[1];
                    int minX = cx * 16, minZ = cz * 16;
                    Object nc = ncCtor.newInstance(16 / 4, state, minX, minZ, noiseSettings, beardifier, settings, fluidPicker, blender);
                    int k = (Integer) cw.invoke(nc);
                    int l = (Integer) ch.invoke(nc);
                    int minCellY = Math.floorDiv(minY, l);
                    int cellCountY = Math.floorDiv(height, l);
                    String[][][] names = new String[16][height][16]; // [x][y-minY][z]
                    double[][][] dens = new double[16][height][16];
                    Object cellCache0 = ((List<?>) ccf.get(nc)).get(0);
                    IdentityHashMap<Object, String> nameCache = new IdentityHashMap<>();
                    initFirst.invoke(nc);
                    for (int o = 0; o < 16 / k; o++) {
                        advance.invoke(nc, o);
                        for (int p = 0; p < 16 / k; p++) {
                            for (int q = cellCountY - 1; q >= 0; q--) {
                                selectYZ.invoke(nc, q, p);
                                for (int r = l - 1; r >= 0; r--) {
                                    int s = (minCellY + q) * l + r;
                                    double d = (double) r / l;
                                    updY.invoke(nc, s, d);
                                    for (int v = 0; v < k; v++) {
                                        int w = minX + o * k + v;
                                        double e = (double) v / k;
                                        updX.invoke(nc, w, e);
                                        for (int y = 0; y < k; y++) {
                                            int z = minZ + p * k + y;
                                            double f = (double) y / k;
                                            updZ.invoke(nc, z, f);
                                            Object bs = interp.invoke(nc);
                                            if (bs == null) bs = defaultBlock;
                                            String nm = nameCache.computeIfAbsent(bs, WorldgenChunkDump::blockName);
                                            names[w & 15][s - minY][z & 15] = nm;
                                            dens[w & 15][s - minY][z & 15] = (Double) dfCompute.invoke(cellCache0, nc);
                                        }
                                    }
                                }
                            }
                        }
                        swap.invoke(nc);
                    }
                    stop.invoke(nc);

                    Map<String, Integer> counts = new LinkedHashMap<>();
                    for (int sec = 0; sec < height / 16; sec++) {
                        long hs = 1125899906842597L, hn = 1125899906842597L, hd = 1125899906842597L;
                        for (int yy = 0; yy < 16; yy++) {
                            int y = sec * 16 + yy;
                            for (int z = 0; z < 16; z++) {
                                for (int x = 0; x < 16; x++) {
                                    String nm = names[x][y][z];
                                    boolean solid = !(nm.equals("minecraft:air") || nm.equals("minecraft:water") || nm.equals("minecraft:lava"));
                                    hs = 31 * hs + (solid ? 1 : 0);
                                    hn = 31 * hn + nm.hashCode();
                                    hd = 31 * hd + Double.doubleToRawLongBits(dens[x][y][z]);
                                    counts.merge(nm, 1, Integer::sum);
                                }
                            }
                        }
                        out.printf("%d,%d,%d,%d,%d,%d,%d%n", seed, cx, cz, sec, hs, hn, hd);
                    }
                    for (Map.Entry<String, Integer> en : counts.entrySet())
                        cnt.printf("%d,%d,%d,%s,%d%n", seed, cx, cz, en.getKey(), en.getValue());
                    if (fullDir != null && (args.length < 4 || args[3].equals(seed + "_" + cx + "_" + cz))) {
                        try (PrintStream full = new PrintStream(fullDir.resolve("chunk_" + seed + "_" + cx + "_" + cz + ".csv").toFile())) {
                            full.println("x,z,y,block,density");
                            for (int x = 0; x < 16; x++)
                                for (int z = 0; z < 16; z++)
                                    for (int y = 0; y < height; y++)
                                        full.printf("%d,%d,%d,%s,%.17g%n", x, z, y + minY, names[x][y][z], dens[x][y][z]);
                        }
                    }
                }
            }
        }
        System.err.println("wrote chunk fixtures to " + outDir);
    }
}
