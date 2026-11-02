package tcref;

import java.io.IOException;
import java.io.PrintStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Reference dumper for TerraCraft's world-generation parity harness (stage 1:
 * random sources and noise primitives).
 *
 * Runs the *vanilla 1.21.11 server jar's own classes* by reflection, resolving
 * names through Mojang's official mapping file, and writes exact (hex-encoded)
 * results to CSV. Nothing from the jar is copied into the repository; only the
 * numeric results (small fixtures) are committed.
 *
 * Usage:
 *   java -cp tools-out;server-1.21.11.jar;libs/* WorldgenRefDump
 *        <server_mappings.txt> <output-dir>
 */
public class WorldgenRefDump {
    // ---- Mojang mapping lookup ------------------------------------------------
    static final Map<String, String> CLASS_OBF = new HashMap<>();
    /** key: mojClass + "#" + mojName + "(" + args + ")" -> obf member name */
    static final Map<String, String> METHOD_OBF = new HashMap<>();

    static void loadMappings(Path file) throws IOException {
        String cls = null;
        for (String line : Files.readAllLines(file)) {
            if (line.startsWith("#") || line.isBlank()) continue;
            if (!line.startsWith(" ")) {
                int arrow = line.indexOf(" -> ");
                cls = line.substring(0, arrow);
                CLASS_OBF.put(cls, line.substring(arrow + 4, line.length() - 1));
            } else if (line.contains("(")) {
                String s = line.trim();
                // optional "12:34:" line-number prefix
                while (!s.isEmpty() && Character.isDigit(s.charAt(0))) {
                    int c = s.indexOf(':');
                    if (c < 0) break;
                    s = s.substring(c + 1);
                }
                int arrow = s.indexOf(" -> ");
                String sig = s.substring(0, arrow);
                String obf = s.substring(arrow + 4);
                int sp = sig.indexOf(' ');
                String nameAndArgs = sig.substring(sp + 1);
                METHOD_OBF.put(cls + "#" + nameAndArgs, obf);
            }
        }
    }

    static Class<?> cls(String moj) throws Exception {
        return Class.forName(CLASS_OBF.get(moj));
    }

    static Class<?> paramType(String t) throws Exception {
        switch (t) {
            case "int": return int.class;
            case "long": return long.class;
            case "double": return double.class;
            case "float": return float.class;
            case "boolean": return boolean.class;
            case "double[]": return double[].class;
            case "java.lang.String": return String.class;
            default: return CLASS_OBF.containsKey(t) ? cls(t) : Class.forName(t);
        }
    }

    static Method method(String mojClass, String mojName, String args) throws Exception {
        String obf = METHOD_OBF.get(mojClass + "#" + mojName + "(" + args + ")");
        if (obf == null) throw new IllegalStateException("no mapping " + mojClass + "." + mojName + "(" + args + ")");
        List<Class<?>> ps = new ArrayList<>();
        if (!args.isEmpty()) for (String a : args.split(",")) ps.add(paramType(a));
        Class<?>[] pa = ps.toArray(new Class<?>[0]);
        Method m;
        try {
            m = cls(mojClass).getMethod(obf, pa);
        } catch (NoSuchMethodException e) {
            m = cls(mojClass).getDeclaredMethod(obf, pa);
        }
        m.setAccessible(true);
        return m;
    }

    static Constructor<?> ctor(String mojClass, Class<?>... ps) throws Exception {
        Constructor<?> c = cls(mojClass).getDeclaredConstructor(ps);
        c.setAccessible(true);
        return c;
    }

    // ---- Fixed test inputs -------------------------------------------------------
    static final long[] SEEDS = {0L, 1L, 42L, -1L, 123456789012345L, Long.MIN_VALUE, Long.MAX_VALUE, -8760239871234567L};
    static final int[] BOUNDS = {1, 2, 3, 7, 10, 16, 100, 255, 256, 1000, 65536, 1000000, 1 << 30, 2147483647};
    static final double[][] POINTS = {
        {0, 0, 0}, {0.5, 0.5, 0.5}, {1, 2, 3}, {-1, -2, -3}, {13.37, 64.25, -7.125},
        {-250.75, 12.5, 999.125}, {1234.5678, -64.0, -8765.4321}, {100000.1, 255.0, 100000.9},
        {3.3554432E7 + 0.5, 1.0, -3.3554432E7 + 0.25}, {-0.0000001, 0.0000001, 0.3333333},
    };
    // (name, firstOctave, amplitudes)
    static final Object[][] NOISES = {
        {"continentalness", -9, new double[]{1, 1, 2, 2, 2, 1, 1, 1, 1}},
        {"temperature", -10, new double[]{1.5, 0, 1, 0, 0, 0}},
        {"erosion", -9, new double[]{1, 1, 0, 1, 1}},
        {"single", 0, new double[]{1}},
        {"sparse", -4, new double[]{0, 1, 0, 0, 2}},
    };

    static String hex(double v) { return Long.toHexString(Double.doubleToRawLongBits(v)); }
    static String hex(float v) { return Integer.toHexString(Float.floatToRawIntBits(v)); }

    interface Rng { Object make(long seed) throws Exception; }

    static void rngSequence(PrintStream out, String gen, long seed, Object rnd, Method nextInt, Method nextIntB,
                            Method nextLong, Method nextBool, Method nextFloat, Method nextDouble,
                            Method nextGaussian) throws Exception {
        int idx = 0;
        for (int i = 0; i < 24; i++) {
            out.printf("%s,%d,nextInt,%d,%s%n", gen, seed, idx++, Integer.toHexString((Integer) nextInt.invoke(rnd)));
            int b = BOUNDS[i % BOUNDS.length];
            out.printf("%s,%d,nextIntB%d,%d,%s%n", gen, seed, b, idx++, Integer.toHexString((Integer) nextIntB.invoke(rnd, b)));
            out.printf("%s,%d,nextLong,%d,%s%n", gen, seed, idx++, Long.toHexString((Long) nextLong.invoke(rnd)));
            out.printf("%s,%d,nextBoolean,%d,%d%n", gen, seed, idx++, ((Boolean) nextBool.invoke(rnd)) ? 1 : 0);
            out.printf("%s,%d,nextFloat,%d,%s%n", gen, seed, idx++, hex((Float) nextFloat.invoke(rnd)));
            out.printf("%s,%d,nextDouble,%d,%s%n", gen, seed, idx++, hex((Double) nextDouble.invoke(rnd)));
            out.printf("%s,%d,nextGaussian,%d,%s%n", gen, seed, idx++, hex((Double) nextGaussian.invoke(rnd)));
        }
    }

    public static void main(String[] args) throws Exception {
        loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);

        String RS = "net.minecraft.util.RandomSource";
        Method nextInt = method(RS, "nextInt", "");
        Method nextIntB = method(RS, "nextInt", "int");
        Method nextLong = method(RS, "nextLong", "");
        Method nextBool = method(RS, "nextBoolean", "");
        Method nextFloat = method(RS, "nextFloat", "");
        Method nextDouble = method(RS, "nextDouble", "");
        Method nextGauss = method(RS, "nextGaussian", "");
        Method fork = method(RS, "forkPositional", "");
        String PRF = "net.minecraft.world.level.levelgen.PositionalRandomFactory";
        Method at = method(PRF, "at", "int,int,int");
        Method fromHash = method(PRF, "fromHashOf", "java.lang.String");
        Method fromSeed = method(PRF, "fromSeed", "long");

        Constructor<?> xoroCtor = ctor("net.minecraft.world.level.levelgen.XoroshiroRandomSource", long.class);
        Constructor<?> xoro2Ctor = ctor("net.minecraft.world.level.levelgen.XoroshiroRandomSource", long.class, long.class);
        Constructor<?> legacyCtor = ctor("net.minecraft.world.level.levelgen.LegacyRandomSource", long.class);

        // 1. Random sequences.
        try (PrintStream out = new PrintStream(outDir.resolve("rng.csv").toFile())) {
            out.println("gen,seed,op,index,value_hex");
            for (long seed : SEEDS) {
                rngSequence(out, "legacy", seed, legacyCtor.newInstance(seed), nextInt, nextIntB, nextLong, nextBool, nextFloat, nextDouble, nextGauss);
                rngSequence(out, "xoroshiro", seed, xoroCtor.newInstance(seed), nextInt, nextIntB, nextLong, nextBool, nextFloat, nextDouble, nextGauss);
            }
            // Raw 128-bit seeds (including the all-zero fix-up).
            long[][] raw = {{0, 0}, {1, 0}, {0, 1}, {-1, -1}, {0x123456789abcdefL, 0x0fedcba987654321L}};
            for (long[] r : raw) {
                Object rnd = xoro2Ctor.newInstance(r[0], r[1]);
                for (int i = 0; i < 8; i++) {
                    out.printf("xoroshiro128,%d_%d,nextLong,%d,%s%n", r[0], r[1], i, Long.toHexString((Long) nextLong.invoke(rnd)));
                }
            }
        }

        // 2. Positional factories.
        String[] names = {"minecraft:continentalness", "octave_-9", "octave_0", "", "minecraft:temperature"};
        int[][] coords = {{0, 0, 0}, {1, 2, 3}, {-1, -2, -3}, {12345, -64, -98765}, {Integer.MAX_VALUE, Integer.MIN_VALUE, 7}};
        try (PrintStream out = new PrintStream(outDir.resolve("positional.csv").toFile())) {
            out.println("gen,seed,kind,key,index,value_hex");
            for (long seed : SEEDS) {
                for (int g = 0; g < 2; g++) {
                    String gen = g == 0 ? "legacy" : "xoroshiro";
                    Object base = (g == 0 ? legacyCtor : xoroCtor).newInstance(seed);
                    Object fac = fork.invoke(base);
                    for (int[] c : coords) {
                        Object r = at.invoke(fac, c[0], c[1], c[2]);
                        for (int i = 0; i < 3; i++)
                            out.printf("%s,%d,at,%d_%d_%d,%d,%s%n", gen, seed, c[0], c[1], c[2], i, Long.toHexString((Long) nextLong.invoke(r)));
                    }
                    for (String n : names) {
                        Object r = fromHash.invoke(fac, n);
                        for (int i = 0; i < 3; i++)
                            out.printf("%s,%d,fromHashOf,%s,%d,%s%n", gen, seed, n, i, Long.toHexString((Long) nextLong.invoke(r)));
                    }
                    Object r = fromSeed.invoke(fac, seed ^ 0x5555L);
                    for (int i = 0; i < 3; i++)
                        out.printf("%s,%d,fromSeed,x,%d,%s%n", gen, seed, i, Long.toHexString((Long) nextLong.invoke(r)));
                }
            }
        }

        // 3. ImprovedNoise (Xoroshiro-seeded, as the density functions use).
        String IN = "net.minecraft.world.level.levelgen.synth.ImprovedNoise";
        Constructor<?> inCtor = ctor(IN, cls(RS));
        Method inNoise = method(IN, "noise", "double,double,double");
        Method inNoise5 = method(IN, "noise", "double,double,double,double,double");
        try (PrintStream out = new PrintStream(outDir.resolve("improved_noise.csv").toFile())) {
            out.println("seed,x,y,z,yScale,yFudge,value_hex");
            for (long seed : SEEDS) {
                Object n = inCtor.newInstance(xoroCtor.newInstance(seed));
                for (double[] p : POINTS)
                    out.printf("%d,%s,%s,%s,0,0,%s%n", seed, hex(p[0]), hex(p[1]), hex(p[2]), hex((Double) inNoise.invoke(n, p[0], p[1], p[2])));
                for (double[] p : POINTS)
                    out.printf("%d,%s,%s,%s,%s,%s,%s%n", seed, hex(p[0]), hex(p[1]), hex(p[2]), hex(0.5), hex(0.25),
                            hex((Double) inNoise5.invoke(n, p[0], p[1], p[2], 0.5, 0.25)));
            }
        }

        // 4. PerlinNoise and NormalNoise.
        String PN = "net.minecraft.world.level.levelgen.synth.PerlinNoise";
        String NN = "net.minecraft.world.level.levelgen.synth.NormalNoise";
        Method pCreate = method(PN, "create", "net.minecraft.util.RandomSource,int,double,double[]");
        Method pValue = method(PN, "getValue", "double,double,double");
        Method pMax = method(PN, "maxValue", "");
        Method nCreate = method(NN, "create", "net.minecraft.util.RandomSource,int,double[]");
        Method nValue = method(NN, "getValue", "double,double,double");
        Method nMax = method(NN, "maxValue", "");
        try (PrintStream out = new PrintStream(outDir.resolve("perlin_normal.csv").toFile())) {
            out.println("kind,seed,noise,x,y,z,value_hex");
            for (long seed : SEEDS) {
                for (Object[] nz : NOISES) {
                    String name = (String) nz[0];
                    int first = (Integer) nz[1];
                    double[] amps = (double[]) nz[2];
                    Object p = pCreate.invoke(null, xoroCtor.newInstance(seed), first, amps[0],
                            java.util.Arrays.copyOfRange(amps, 1, amps.length));
                    Object n = nCreate.invoke(null, xoroCtor.newInstance(seed), first, amps);
                    out.printf("perlin_max,%d,%s,0,0,0,%s%n", seed, name, hex((Double) pMax.invoke(p)));
                    out.printf("normal_max,%d,%s,0,0,0,%s%n", seed, name, hex((Double) nMax.invoke(n)));
                    for (double[] pt : POINTS) {
                        out.printf("perlin,%d,%s,%s,%s,%s,%s%n", seed, name, hex(pt[0]), hex(pt[1]), hex(pt[2]), hex((Double) pValue.invoke(p, pt[0], pt[1], pt[2])));
                        out.printf("normal,%d,%s,%s,%s,%s,%s%n", seed, name, hex(pt[0]), hex(pt[1]), hex(pt[2]), hex((Double) nValue.invoke(n, pt[0], pt[1], pt[2])));
                    }
                }
            }
        }
        System.err.println("wrote reference fixtures to " + outDir);
    }
}
