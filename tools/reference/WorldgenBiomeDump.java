package tcref;

import java.io.PrintStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Random;
import java.util.function.Function;

/**
 * Stage-3 reference dumper: climate sampling and multi-noise biome lookup
 * from the REAL 1.21.11 server classes (RandomState.sampler(),
 * MultiNoiseBiomeSourceParameterList.Preset.OVERWORLD, Climate.RTree).
 *
 * Output (hex/decimal exact):
 *   biome_list.csv    one row: count and an order-sensitive hash of every leaf
 *   biome_climate.csv seed,qx,qy,qz,t,h,c,e,d,w,biome   (sampler -> RTree)
 *   biome_random.csv  t,h,c,e,d,w,biome,brute_force_biome (RTree vs brute force)
 */
public class WorldgenBiomeDump {
    static final long[] SEEDS = WorldgenRouterDump.SEEDS;

    static String biomeName(Object resourceKey) {
        String s = String.valueOf(resourceKey); // ResourceKey[minecraft:worldgen/biome / minecraft:plains]
        int i = s.lastIndexOf(" / ");
        return s.substring(i + 3, s.length() - 1);
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);

        WorldgenRefDump.method("net.minecraft.SharedConstants", "tryDetectVersion", "").invoke(null);
        WorldgenRefDump.method("net.minecraft.server.Bootstrap", "bootStrap", "").invoke(null);

        // Preset.OVERWORLD.provider().apply(identity) -> ParameterList<ResourceKey<Biome>>
        String PRESET = "net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterList$Preset";
        Class<?> presetC = WorldgenRefDump.cls(PRESET);
        Field ow = presetC.getDeclaredField("b");
        ow.setAccessible(true);
        Object preset = ow.get(null);
        Object provider = WorldgenRefDump.method(PRESET, "provider", "").invoke(preset);
        Method apply = WorldgenRefDump.method(PRESET + "$SourceProvider", "apply", "java.util.function.Function");
        String PL = "net.minecraft.world.level.biome.Climate$ParameterList";
        Method findValue = WorldgenRefDump.method(PL, "findValue", "net.minecraft.world.level.biome.Climate$TargetPoint");
        Method brute = WorldgenRefDump.method(PL, "findValueBruteForce", "net.minecraft.world.level.biome.Climate$TargetPoint");
        Method values = WorldgenRefDump.method(PL, "values", "");

        Object list = apply.invoke(provider, Function.identity());

        // ---- leaf list hash --------------------------------------------------------
        String PAIR = "com.mojang.datafixers.util.Pair";
        Class<?> pairC = Class.forName("com.mojang.datafixers.util.Pair");
        Method getFirst = pairC.getMethod("getFirst");
        Method getSecond = pairC.getMethod("getSecond");
        String PP = "net.minecraft.world.level.biome.Climate$ParameterPoint";
        String PA = "net.minecraft.world.level.biome.Climate$Parameter";
        String[] pnames = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness"};
        Method[] pget = new Method[6];
        for (int i = 0; i < 6; i++) pget[i] = WorldgenRefDump.method(PP, pnames[i], "");
        Method offset = WorldgenRefDump.method(PP, "offset", "");
        Method pmin = WorldgenRefDump.method(PA, "min", "");
        Method pmax = WorldgenRefDump.method(PA, "max", "");
        List<?> leaves = (List<?>) values.invoke(list);
        long hash = 1125899906842597L;
        for (Object pair : leaves) {
            Object pt = getFirst.invoke(pair);
            String name = biomeName(getSecond.invoke(pair));
            hash = 31 * hash + name.hashCode();
            for (int i = 0; i < 6; i++) {
                Object p = pget[i].invoke(pt);
                hash = 31 * hash + (Long) pmin.invoke(p);
                hash = 31 * hash + (Long) pmax.invoke(p);
            }
            hash = 31 * hash + (Long) offset.invoke(pt);
        }
        try (PrintStream out = new PrintStream(outDir.resolve("biome_list.csv").toFile())) {
            out.println("count,hash");
            out.printf("%d,%d%n", leaves.size(), hash);
        }

        String TP = "net.minecraft.world.level.biome.Climate$TargetPoint";
        Constructor<?> tpCtor = WorldgenRefDump.cls(TP).getDeclaredConstructor(long.class, long.class, long.class, long.class, long.class, long.class);
        tpCtor.setAccessible(true);

        // ---- random targets (RTree vs brute force) ------------------------------------
        try (PrintStream out = new PrintStream(outDir.resolve("biome_random.csv").toFile())) {
            out.println("t,h,c,e,d,w,biome,brute");
            Random rnd = new Random(20261102L);
            for (int i = 0; i < 4000; i++) {
                long[] v = new long[6];
                for (int k = 0; k < 6; k++) {
                    int range = (k == 4) ? 14000 : 11000; // depth reaches further
                    v[k] = rnd.nextInt(2 * range + 1) - range;
                    if (rnd.nextInt(8) == 0) v[k] = (v[k] / 500) * 500; // boundary-aligned values
                }
                Object tp = tpCtor.newInstance(v[0], v[1], v[2], v[3], v[4], v[5]);
                String a = biomeName(findValue.invoke(list, tp));
                String b = biomeName(brute.invoke(list, tp));
                out.printf("%d,%d,%d,%d,%d,%d,%s,%s%n", v[0], v[1], v[2], v[3], v[4], v[5], a, b);
            }
        }

        // ---- sampler -> biome ------------------------------------------------------------
        WorldgenRefDump.method("net.minecraft.SharedConstants", "tryDetectVersion", "");
        Object provider2 = WorldgenRefDump.method("net.minecraft.data.registries.VanillaRegistries", "createLookup", "").invoke(null);
        Class<?> ngs = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings");
        Field owk = ngs.getDeclaredField("c");
        owk.setAccessible(true);
        Object key = owk.get(null);
        String RS = "net.minecraft.world.level.levelgen.RandomState";
        Method create = WorldgenRefDump.method(RS, "create", "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");
        Method samplerM = WorldgenRefDump.method(RS, "sampler", "");
        Method sample = WorldgenRefDump.method("net.minecraft.world.level.biome.Climate$Sampler", "sample", "int,int,int");
        Method[] tget = new Method[6];
        String[] tn = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness"};
        for (int i = 0; i < 6; i++) tget[i] = WorldgenRefDump.method(TP, tn[i], "");

        int[] qxs = {0, 1, -1, 25, -63, 250, -2000, 20000};
        int[] qzs = {0, 3, -5, 40, -77, 99, 1500, -20000};
        int[] qys = {-16, -8, 0, 8, 15, 31, 63};
        try (PrintStream out = new PrintStream(outDir.resolve("biome_climate.csv").toFile())) {
            out.println("seed,qx,qy,qz,t,h,c,e,d,w,biome");
            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider2, key, seed);
                Object sampler = samplerM.invoke(state);
                for (int i = 0; i < qxs.length; i++) {
                    for (int y : qys) {
                        int qx = qxs[i], qz = qzs[(i * 3 + (y & 7)) % qzs.length];
                        emit(out, seed, qx, y, qz, sampler, sample, tget, findValue, list);
                    }
                }
                for (int qx = -128; qx < 128; qx += 16)
                    for (int qz = -128; qz < 128; qz += 16)
                        emit(out, seed, qx, 16, qz, sampler, sample, tget, findValue, list);
            }
        }
        System.err.println("wrote biome fixtures to " + outDir);
    }

    static void emit(PrintStream out, long seed, int qx, int qy, int qz, Object sampler, Method sample, Method[] tget,
                     Method findValue, Object list) throws Exception {
        Object tp = sample.invoke(sampler, qx, qy, qz);
        long[] v = new long[6];
        for (int i = 0; i < 6; i++) v[i] = (Long) tget[i].invoke(tp);
        String b = biomeName(findValue.invoke(list, tp));
        out.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s%n", seed, qx, qy, qz, v[0], v[1], v[2], v[3], v[4], v[5], b);
    }
}
