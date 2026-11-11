package com.g2806.radiante.client.pipeline;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.config.ImageConfig;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Stream;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * Each preset is put together by hand in {@link PresetAssembly}: which modules, and which image of one goes into
 * which image of the next, by name. A name that no longer exists in a module's YAML, two sources for one input or an
 * input left without one only showed when the game built that pipeline. This assembles every preset from the modules'
 * shipped YAML, with a renderer that has every module, and checks the result holds together.
 */
class PresetAssemblyTest {

    @TempDir
    static Path folder;

    private static Path folderBefore;

    @BeforeAll
    static void aRendererWithEveryModule() throws IOException {
        Path modules = Files.createDirectories(folder.resolve("modules"));
        try (Stream<Path> shipped = Files.list(Path.of("src/main/resources/modules"))) {
            for (Path file : shipped.toList()) {
                Files.copy(file, modules.resolve(file.getFileName().toString()));
            }
        }
        folderBefore = RadianteClient.radianceDir;
        RadianteClient.radianceDir = folder;
        Pipeline.host = new Pipeline.Host() {
            @Override
            public boolean hasModule(String name) {
                return true;
            }

            @Override
            public String attributes(String name, String[] attributes, String language) {
                // No attributes of its own beyond the YAML's: what the modules without a shader pack answer.
                return null;
            }

            @Override
            public boolean rayReconstruction() {
                return true;
            }

            @Override
            public String language() {
                return "en_us";
            }
        };
        Pipeline.reloadAllModuleEntries();
    }

    @AfterAll
    static void theRealRendererAgain() {
        Pipeline.host = Pipeline.RENDERER;
        RadianteClient.radianceDir = folderBefore;
        Pipeline.clear();
    }

    @Test
    void everyPresetIsOfferedWhenTheRendererHasItsModules() {
        for (Presets preset : Presets.values()) {
            assertTrue(Pipeline.isPresetAvailable(preset.key), preset.name + " is not offered");
        }
    }

    @Test
    void everyPresetHoldsTogether() {
        for (Presets preset : Presets.values()) {
            PresetAssembly.assemblePresetByKeyInternal(preset.key);
            Pipeline pipeline = Pipeline.INSTANCE;
            String where = preset.name + ": ";
            assertEquals(preset.key, pipeline.activePresetName, where + "the preset it says it is");
            assertFalse(pipeline.modules.isEmpty(), where + "no modules");

            Set<ImageConfig> outputs = new HashSet<>();
            Set<ImageConfig> inputs = new HashSet<>();
            int finalOutputs = 0;
            for (Module module : pipeline.modules) {
                outputs.addAll(module.outputImageConfigs);
                inputs.addAll(module.inputImageConfigs);
                for (ImageConfig output : module.outputImageConfigs) {
                    finalOutputs += output.finalOutput ? 1 : 0;
                }
            }
            assertEquals(1, finalOutputs, where + "one image is the picture the pipeline ends with");

            Map<ImageConfig, ImageConfig> sourceOf = new HashMap<>();
            for (Map.Entry<ImageConfig, List<ImageConfig>> connection : pipeline.moduleConnections.entrySet()) {
                ImageConfig source = connection.getKey();
                assertNotNull(source, where + "a connection from an image its module does not have");
                assertTrue(outputs.contains(source), where + source.name + " is not an output of a module in it");
                for (ImageConfig target : connection.getValue()) {
                    assertNotNull(target, where + source.name + " goes to an image its module does not have");
                    assertTrue(inputs.contains(target), where + target.name + " is not an input of a module in it");
                    assertEquals(source.format, target.format,
                        where + source.name + " -> " + target.name + " join images of different formats");
                    assertTrue(sourceOf.put(target, source) == null, where + target.name + " has two sources");
                }
            }
            for (Module module : pipeline.modules) {
                if (module.inputImageConfigs.isEmpty()) {
                    continue;
                }
                boolean fed = module.inputImageConfigs.stream().anyMatch(sourceOf::containsKey);
                assertTrue(fed, where + module.name + " takes no image from another module");
            }
        }
    }
}
