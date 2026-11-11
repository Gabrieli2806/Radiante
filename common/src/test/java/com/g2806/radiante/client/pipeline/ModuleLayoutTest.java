package com.g2806.radiante.client.pipeline;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;
import org.yaml.snakeyaml.Yaml;

/**
 * A render module is described twice: its YAML in {@code resources/modules} tells Java which images it reads and
 * writes, and its C++ class says how many it takes ({@code inputImageNum}, {@code outputImageNum}). The renderer reads
 * that many indices from the arrays Java filled from the YAML (WorldPipelineBlueprint), so a module whose two counts
 * differ makes it read past the end of them. This keeps the two the same without a build of the renderer.
 */
class ModuleLayoutTest {

    private static final Path MODULES = Path.of("src/main/resources/modules");
    private static final Path NATIVE_MODULES = Path.of("../native/src/prism/render/modules");

    private static final Pattern NAME = Pattern.compile("\\bNAME\\s*=\\s*\"([^\"]+)\"");
    private static final Pattern INPUTS = Pattern.compile("\\binputImageNum\\s*=\\s*(\\d+)");
    private static final Pattern OUTPUTS = Pattern.compile("\\boutputImageNum\\s*=\\s*(\\d+)");

    private record Counts(int inputs, int outputs) {
    }

    @Test
    void everyModuleTakesAsManyImagesAsItsYamlLists() throws IOException {
        Map<String, Counts> described = described();
        Map<String, Counts> built = built();
        assertFalse(described.isEmpty(), "no module YAML found in " + MODULES.toAbsolutePath());
        assertEquals(described.keySet(), built.keySet(),
            "each module YAML needs a C++ module of the same NAME, and each C++ module a YAML");
        for (Map.Entry<String, Counts> module : described.entrySet()) {
            assertEquals(module.getValue(), built.get(module.getKey()),
                module.getKey() + ": the YAML's image lists and the C++ inputImageNum/outputImageNum differ");
        }
    }

    private static Map<String, Counts> described() throws IOException {
        Map<String, Counts> modules = new TreeMap<>();
        try (Stream<Path> files = Files.list(MODULES)) {
            for (Path file : files.filter(path -> path.toString().endsWith(".yaml")).toList()) {
                Map<String, Object> yaml = new Yaml().load(Files.readString(file));
                String name = (String) yaml.get("name");
                assertNotNull(name, file + " has no name");
                modules.put(name, new Counts(size(yaml.get("inputImageConfigs")),
                    size(yaml.get("outputImageConfigs"))));
            }
        }
        return modules;
    }

    private static int size(Object list) {
        return list instanceof List<?> entries ? entries.size() : 0;
    }

    private static Map<String, Counts> built() throws IOException {
        Map<String, Counts> modules = new TreeMap<>();
        try (Stream<Path> files = Files.walk(NATIVE_MODULES)) {
            for (Path file : files.filter(path -> path.toString().endsWith(".hpp")).toList()) {
                String text = Files.readString(file);
                Matcher name = NAME.matcher(text);
                Matcher inputs = INPUTS.matcher(text);
                Matcher outputs = OUTPUTS.matcher(text);
                if (name.find() && inputs.find() && outputs.find()) {
                    modules.put(name.group(1),
                        new Counts(Integer.parseInt(inputs.group(1)), Integer.parseInt(outputs.group(1))));
                }
            }
        }
        return modules;
    }
}
