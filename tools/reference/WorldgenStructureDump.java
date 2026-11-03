package tcref;

import java.io.PrintStream;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

/**
 * Stage-6 reference dumper for structure placement: the real StructurePlacement.isStructureChunk
 * (random spread, frequency reduction, exclusion zones) of every random-spread structure set of the
 * overworld/nether/end data pack, evaluated through a real ChunkGeneratorStructureState.
 *
 * Output: structure_chunks.csv  seed,structure_set,chunk_x,chunk_z   (every chunk in the scanned square
 * for which isStructureChunk is true). Concentric-ring sets (strongholds) are not dumped.
 */
public class WorldgenStructureDump {
    static final long[] SEEDS = WorldgenRouterDump.SEEDS;
    static final int RANGE = 64;

    static Object call(String cls, String name, String args, Object target, Object... a) throws Exception {
        return WorldgenRefDump.method(cls, name, args).invoke(target, a);
    }

    public static void main(String[] args) throws Exception {
        WorldgenRefDump.loadMappings(Paths.get(args[0]));
        Path outDir = Paths.get(args[1]);
        Files.createDirectories(outDir);
        call("net.minecraft.SharedConstants", "tryDetectVersion", "", null);
        call("net.minecraft.server.Bootstrap", "bootStrap", "", null);
        Object provider = call("net.minecraft.data.registries.VanillaRegistries", "createLookup", "", null);

        Object nsReg = WorldgenRefDump.field("net.minecraft.core.registries.Registries", "NOISE_SETTINGS", null);
        Object nsGetter = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, nsReg);
        Object owKey = WorldgenRefDump.field("net.minecraft.world.level.levelgen.NoiseGeneratorSettings", "OVERWORLD", null);
        Object biomeReg = WorldgenRefDump.field("net.minecraft.core.registries.Registries", "BIOME", null);
        Object biomeGetter = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, biomeReg);
        Object plainsHolder = call("net.minecraft.core.HolderGetter", "getOrThrow", "net.minecraft.resources.ResourceKey", biomeGetter,
                WorldgenRefDump.field("net.minecraft.world.level.biome.Biomes", "PLAINS", null));
        Object biomeSource = WorldgenCarverDump.construct("net.minecraft.world.level.biome.FixedBiomeSource", 1, plainsHolder);

        Object setReg = WorldgenRefDump.field("net.minecraft.core.registries.Registries", "STRUCTURE_SET", null);
        Object setLookup = call("net.minecraft.core.HolderGetter$Provider", "lookupOrThrow", "net.minecraft.resources.ResourceKey", provider, setReg);
        Method listElements = WorldgenRefDump.method("net.minecraft.core.HolderLookup", "listElements", "");
        Method holderValue = WorldgenRefDump.method("net.minecraft.core.Holder", "value", "");
        Method refKey = WorldgenRefDump.method("net.minecraft.core.Holder$Reference", "key", "");
        Method keyLoc = WorldgenRefDump.method("net.minecraft.resources.ResourceKey", "identifier", "");
        Method placementOf = WorldgenRefDump.method("net.minecraft.world.level.levelgen.structure.StructureSet", "placement", "");
        Method isChunk = WorldgenRefDump.method("net.minecraft.world.level.levelgen.structure.placement.StructurePlacement", "isStructureChunk",
                "net.minecraft.world.level.chunk.ChunkGeneratorStructureState,int,int");
        Class<?> randomSpread = WorldgenRefDump.cls("net.minecraft.world.level.levelgen.structure.placement.RandomSpreadStructurePlacement");
        Method createState = WorldgenRefDump.method("net.minecraft.world.level.chunk.ChunkGeneratorStructureState", "createForNormal",
                "net.minecraft.world.level.levelgen.RandomState,long,net.minecraft.world.level.biome.BiomeSource,net.minecraft.core.HolderLookup");
        Method createRandomState = WorldgenRefDump.method("net.minecraft.world.level.levelgen.RandomState", "create",
                "net.minecraft.core.HolderGetter$Provider,net.minecraft.resources.ResourceKey,long");

        Method spacingOf = WorldgenRefDump.method("net.minecraft.world.level.levelgen.structure.placement.RandomSpreadStructurePlacement", "spacing", "");
        List<Object> holders = new ArrayList<>();
        ((java.util.stream.Stream<?>) listElements.invoke(setLookup)).forEach(holders::add);
        List<Object[]> sets = new ArrayList<>();
        ((java.util.stream.Stream<?>) listElements.invoke(setLookup)).forEach(h -> {
            try {
                Object placement = placementOf.invoke(holderValue.invoke(h));
                if (randomSpread.isInstance(placement)) sets.add(new Object[]{String.valueOf(keyLoc.invoke(refKey.invoke(h))), placement});
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        });
        sets.sort((a, b) -> ((String) a[0]).compareTo((String) b[0]));

        try (PrintStream out = new PrintStream(outDir.resolve("structure_chunks.csv").toFile())) {
            out.println("seed,structure_set,chunk_x,chunk_z");
            for (long seed : SEEDS) {
                Object randomState = createRandomState.invoke(null, provider, owKey, seed);
                Object state = WorldgenCarverDump.construct("net.minecraft.world.level.chunk.ChunkGeneratorStructureState", 5, randomState, biomeSource, seed, seed, holders);
                for (Object[] set : sets) {
                    int n = 0;
                    // Very dense sets (spacing 1-2) are only scanned over a small window to keep the fixture small.
                    int spacing = (Integer) spacingOf.invoke(set[1]);
                    int range = spacing <= 2 && !((String) set[0]).contains("buried") && !((String) set[0]).contains("mineshaft") ? 12 : RANGE;
                    for (int x = -range; x <= range; x++) {
                        for (int z = -range; z <= range; z++) {
                            if ((Boolean) isChunk.invoke(set[1], state, x, z)) {
                                out.printf("%d,%s,%d,%d%n", seed, set[0], x, z);
                                n++;
                            }
                        }
                    }
                    System.err.println("seed " + seed + " " + set[0] + " " + n);
                }
            }
        }
        System.err.println("wrote structure fixtures to " + outDir);
    }
}
