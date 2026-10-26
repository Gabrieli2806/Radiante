package com.g2806.radiante.client.pipeline;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.config.ImageConfig;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.Constructor;

public class ModuleEntry {

    public String name;
    public String resourcePath;

    public ModuleEntry(String name, String resourcePath) {
        this.name = name;
        this.resourcePath = resourcePath;
    }

    /**
     * The modules in radiante/modules, where RadianteClient.ensureNativeLoaded copies them, by the name each declares.
     * The folder is always on disk; the module itself is then read from the mod's resources (loadModule).
     */
    public static Map<String, ModuleEntry> loadAllModuleEntries() throws Exception {
        Map<String, ModuleEntry> entries = new HashMap<>();
        String folderName = "modules";
        File[] files = RadianteClient.radianceDir.resolve(folderName).toFile().listFiles(
            (d, name) -> name.endsWith(".yaml") || name.endsWith(".yml"));
        if (files != null) {
            for (File file : files) {
                String relativePath = folderName + "/" + file.getName();
                try (InputStream is = new FileInputStream(file)) {
                    String yamlName = extractNameFromYaml(is);
                    if (yamlName != null) {
                        entries.put(yamlName, new ModuleEntry(yamlName, relativePath));
                    }
                } catch (IOException e) {
                    throw new RuntimeException(e);
                }
            }
        }
        return entries;
    }

    private static String extractNameFromYaml(InputStream inputStream) {
        Yaml yaml = new Yaml();
        Map<String, Object> data = yaml.load(inputStream);
        if (data != null && data.containsKey("name")) {
            return String.valueOf(data.get("name"));
        } else {
            throw new RuntimeException("Failed to extract module name");
        }
    }

    public Module loadModule() {
        LoaderOptions options = new LoaderOptions();
        Yaml yaml = new Yaml(new Constructor(Module.class, options));

        try (InputStream inputStream = getClass().getClassLoader()
            .getResourceAsStream(this.resourcePath)) {
            if (inputStream == null) {
                throw new RuntimeException("Module not found in resource: " + this.resourcePath);
            }
            Module module = yaml.load(inputStream);
            for (ImageConfig imageConfig : module.inputImageConfigs) {
                imageConfig.owner = module;
            }
            for (ImageConfig imageConfig : module.outputImageConfigs) {
                imageConfig.owner = module;
            }
            module.staticAttributeConfigs = Module.copyAttributeConfigs(module.attributeConfigs);
            return module;
        } catch (Exception e) {
            throw new RuntimeException("Failed to load module from: " + this.resourcePath, e);
        }
    }
}
