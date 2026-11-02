package tcref;

import java.io.PrintStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Stage-2 reference dumper: evaluates the REAL 1.21.11 overworld noise router
 * (RandomState + NoiseRouter density functions) at sample points, via
 * reflection on the server jar with Mojang's mappings. Output is exact
 * (hex-encoded doubles) CSV: seed,field,x,y,z,value_hex.
 *
 * Usage: java -cp ... tcref.WorldgenRouterDump <server_mappings.txt> <out-dir>
 */
public class WorldgenRouterDump {
    static final String[] FIELDS = {
        "barrierNoise", "fluidLevelFloodednessNoise", "fluidLevelSpreadNoise", "lavaNoise",
        "temperature", "vegetation", "continents", "erosion", "depth", "ridges",
        "preliminarySurfaceLevel", "finalDensity", "veinToggle", "veinRidged", "veinGap",
    };
    static final long[] SEEDS = {0L, 1L, 42L, -1L, 123456789012345L, Long.MIN_VALUE, Long.MAX_VALUE, -8760239871234567L};

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);

        WorldgenRefDump.method("net.minecraft.SharedConstants", "tryDetectVersion", "").invoke(null);
        WorldgenRefDump.method("net.minecraft.server.Bootstrap", "bootStrap", "").invoke(null);
        Object provider = WorldgenRefDump.method("net.minecraft.data.registries.VanillaRegistries", "createLookup", "").invoke(null);

        Class<?> ngs = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.NoiseGeneratorSettings");
        Field ow = ngs.getDeclaredField("c"); // OVERWORLD (ResourceKey)
        ow.setAccessible(true);
        Object key = ow.get(null);

        String RS = "net.minecraft.world.level.levelgen.RandomState";
        Method create = WorldgenRefDump.method(RS, "create", "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");
        Method routerM = WorldgenRefDump.method(RS, "router", "");
        String NR = "net.minecraft.world.level.levelgen.NoiseRouter";
        String DF = "net.minecraft.world.level.levelgen.DensityFunction";
        Method compute = WorldgenRefDump.method(DF, "compute", "net.minecraft.world.level.levelgen.DensityFunction$FunctionContext");
        Constructor<?> spc = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.DensityFunction$SinglePointContext")
                .getDeclaredConstructor(int.class, int.class, int.class);
        spc.setAccessible(true);

        int[] xs = {0, 1, -1, 16, -17, 100, -250, 1234, -8765, 100000};
        int[] zs = {0, 3, -5, 16, -33, 77, 999, -4321, 56789, -100000};
        int[] ys = {-64, -10, 0, 32, 63, 64, 100, 200, 319};

        try (PrintStream out = new PrintStream(outDir.resolve("router.csv").toFile())) {
            out.println("seed,field,x,y,z,value_hex");
            for (long seed : SEEDS) {
                Object state = create.invoke(null, provider, key, seed);
                Object router = routerM.invoke(state);
                for (String f : FIELDS) {
                    Method acc = WorldgenRefDump.method(NR, f, "");
                    Object df = acc.invoke(router);
                    // Climate fields are 2D-ish but are sampled at several heights too.
                    for (int i = 0; i < xs.length; i++) {
                        for (int y : ys) {
                            int x = xs[i], z = zs[(i * 7 + (y & 7)) % zs.length];
                            Object ctx = spc.newInstance(x, y, z);
                            double v = (Double) compute.invoke(df, ctx);
                            out.printf("%d,%s,%d,%d,%d,%s%n", seed, f, x, y, z, Long.toHexString(Double.doubleToRawLongBits(v)));
                        }
                    }
                }
            }
        }
        System.err.println("wrote router fixture to " + outDir);
    }
}
