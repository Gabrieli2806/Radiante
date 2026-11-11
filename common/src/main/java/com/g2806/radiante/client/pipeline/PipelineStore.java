package com.g2806.radiante.client.pipeline;

import static com.g2806.radiante.client.pipeline.ModuleAttributes.collectStaticAttributeNames;
import static com.g2806.radiante.client.pipeline.ModuleAttributes.getModuleAttributes;
import static com.g2806.radiante.client.pipeline.Pipeline.BUILT_IN_SHADER_PACK_PATH;
import static com.g2806.radiante.client.pipeline.Pipeline.INSTANCE;
import static com.g2806.radiante.client.pipeline.Pipeline.PIPELINE_CONFIG_PATH;
import static com.g2806.radiante.client.pipeline.Pipeline.RAY_TRACING_MODULE_NAME;
import static com.g2806.radiante.client.pipeline.Pipeline.RAY_TRACING_SHADER_PACK_PATH_ATTRIBUTE;
import static com.g2806.radiante.client.pipeline.Pipeline.addModule;
import static com.g2806.radiante.client.pipeline.Pipeline.assembleDefault;
import static com.g2806.radiante.client.pipeline.Pipeline.assembleNRDFSR;
import static com.g2806.radiante.client.pipeline.Pipeline.clear;
import static com.g2806.radiante.client.pipeline.Pipeline.connect;
import static com.g2806.radiante.client.pipeline.Pipeline.isModuleAvailable;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.SettingsFile;
import com.g2806.radiante.client.pipeline.Pipeline.PipelineConfigStorage;
import com.g2806.radiante.client.pipeline.Pipeline.PipelineMode;
import com.g2806.radiante.client.pipeline.Pipeline.PipelineStorage;
import com.g2806.radiante.client.pipeline.Pipeline.PresetStoredModule;
import com.g2806.radiante.client.pipeline.Pipeline.StoredAttribute;
import com.g2806.radiante.client.pipeline.Pipeline.StoredConnection;
import com.g2806.radiante.client.pipeline.Pipeline.StoredModule;
import com.g2806.radiante.client.pipeline.config.AttributeConfig;
import com.g2806.radiante.client.pipeline.config.ImageConfig;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import org.yaml.snakeyaml.DumperOptions;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.Constructor;
import org.yaml.snakeyaml.inspector.TagInspector;

/**
 * The pipeline's settings on disk: {@code pipeline.yaml} (the preset or the custom pipeline, with each module's
 * values) and the settings file beside each shader pack. Writes them from the running pipeline, reads them back into
 * it, and holds the one-time migrations of the files older versions left. The classes the yaml is made of stay nested
 * in {@link Pipeline}: the file names the root one by its class name.
 */
final class PipelineStore {

    private PipelineStore() {
    }

    /**
     * The built-in shader pack was called vanilla-pt before it was renamed radiante-pt. What a player stored under
     * the old name moves over: the pack's settings file (next to the zip), and the pipeline's pointer to the zip.
     * The old zip is left to go, or the shader pack list would offer a second copy.
     */
    static void migrateRenamedPack(Path folderPath) {
        try {
            Path packs = folderPath.resolve("shaders/world/ray_tracing");
            Path oldSettings = packs.resolve("vanilla-pt.zip.txt");
            Path newSettings = packs.resolve("radiante-pt.zip.txt");
            if (Files.isRegularFile(oldSettings)) {
                if (!Files.exists(newSettings)) {
                    Files.move(oldSettings, newSettings);
                } else {
                    Files.delete(oldSettings);
                }
            }
            Files.deleteIfExists(packs.resolve("vanilla-pt.zip"));
            if (PIPELINE_CONFIG_PATH != null && Files.isRegularFile(PIPELINE_CONFIG_PATH)) {
                String stored = Files.readString(PIPELINE_CONFIG_PATH);
                String moved = stored.replace("ray_tracing/vanilla-pt.zip", "ray_tracing/radiante-pt.zip");
                if (!moved.equals(stored)) {
                    SettingsFile.write(PIPELINE_CONFIG_PATH, moved);
                }
            }
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("Could not move the stored settings of the renamed shader pack", e);
        }
    }

    /**
     * The Pixel Light example pack became the built-in Pixelated Lighting. A pipeline that pointed at the example's
     * zip or folder in shaderpacks/ points at the built-in one instead. Its settings file is not carried over: the
     * example's defaults had the retro palette on, and the file holds every value, so the palette would have stayed.
     * The player's own copy is left where it is, listed as before.
     */
    static void migrateBuiltInPixelLight() {
        try {
            if (PIPELINE_CONFIG_PATH == null || !Files.isRegularFile(PIPELINE_CONFIG_PATH)) {
                return;
            }
            String stored = Files.readString(PIPELINE_CONFIG_PATH);
            // The value line of the pack path, quoted or not, the zip or the folder.
            String moved = stored.replaceAll("(?m)^([ \\t]*value: )['\"]?" + ShaderPackLibrary.FOLDER
                + "/pixel-light(\\.zip)?['\"]?[ \\t]*$", "$1" + ShaderPackLibrary.BUILT_IN_EXTRAS.get(0));
            if (!moved.equals(stored)) {
                SettingsFile.write(PIPELINE_CONFIG_PATH, moved);
                RadianteClient.LOGGER.info("The Pixel Light pack is built in now, as Pixelated Lighting; using it");
            }
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("Could not point the pipeline at the built-in Pixelated Lighting pack", e);
        }
    }

    /**
     * Deletes the stored pipeline (pipeline.yaml) and every shader pack's stored settings (the .txt beside each
     * pack): the next load starts from a fresh install's.
     */
    static void forgetStoredSettings() {
        try {
            if (PIPELINE_CONFIG_PATH != null) {
                Files.deleteIfExists(PIPELINE_CONFIG_PATH);
            }
            if (RadianteClient.radianceDir != null) {
                Path shaders = RadianteClient.radianceDir.resolve("shaders");
                if (Files.isDirectory(shaders)) {
                    try (var files = Files.walk(shaders)) {
                        for (Path file : files.filter(f -> f.getFileName().toString().endsWith(".zip.txt")).toList()) {
                            Files.deleteIfExists(file);
                        }
                    }
                }
            }
        } catch (IOException e) {
            RadianteClient.LOGGER.error("Could not forget the stored pipeline settings", e);
        }
    }

    static void applyPresetModuleOverrides(List<PresetStoredModule> storedModules) {
        if (storedModules == null || storedModules.isEmpty()) {
            return;
        }

        for (PresetStoredModule storedModule : storedModules) {
            if (storedModule == null || storedModule.entryName == null) {
                continue;
            }

            for (Module module : INSTANCE.modules) {
                if (!Objects.equals(module.name, storedModule.entryName)) {
                    continue;
                }

                applyStoredAttributes(module, storedModule.attributes);
            }
        }
    }

    private static void applyStoredAttributes(Module module, List<StoredAttribute> storedAttributes) {
        if (module == null) {
            return;
        }

        applyStoredAttributeValues(module, module.attributeConfigs, storedAttributes);
        getModuleAttributes(module);
        applyStoredAttributeValues(module, module.attributeConfigs, storedAttributes);
    }

    private static void applyStoredAttributeValues(Module module,
                                                   List<AttributeConfig> attributeConfigs,
                                                   List<StoredAttribute> storedAttributes) {
        if (attributeConfigs == null || storedAttributes == null) {
            return;
        }
        Set<String> staticAttributeNames = Objects.equals(module.name, RAY_TRACING_MODULE_NAME)
                ? collectStaticAttributeNames(module)
                : null;

        for (StoredAttribute storedAttribute : storedAttributes) {
            if (storedAttribute == null || storedAttribute.name == null) {
                continue;
            }
            if (staticAttributeNames != null && !staticAttributeNames.contains(storedAttribute.name)) {
                continue;
            }

            for (int i = 0; i < attributeConfigs.size(); i++) {
                var attributeConfig = attributeConfigs.get(i);

                if (!Objects.equals(attributeConfig.name, storedAttribute.name)) {
                    continue;
                }
                if (storedAttribute.type != null && !Objects.equals(attributeConfig.type, storedAttribute.type)) {
                    continue;
                }

                attributeConfig.value = storedAttribute.value;
                break;
            }
        }
    }

    static void savePipeline() {
        if (PIPELINE_CONFIG_PATH == null) {
            return;
        }

        PipelineConfigStorage storage = new PipelineConfigStorage();
        storage.mode = INSTANCE.mode.toString();
        storage.presetName = INSTANCE.activePresetName;

        if (INSTANCE.mode == PipelineMode.PIPELINE) {
            storage.pipeline = capturePipelineStorage();
        } else {
            storage.presetModules = capturePresetModules();
        }

        for (Module module : INSTANCE.modules) {
            if (module == null || !Objects.equals(module.name, RAY_TRACING_MODULE_NAME) || module.attributeConfigs == null) {
                continue;
            }

            Path storagePath = PipelineShaderPack.settingsFileOf(module.attributeConfigs);

            if (storagePath == null) {
                continue;
            }

            Set<String> staticAttributeNames = collectStaticAttributeNames(module);
            List<StoredAttribute> storedAttributes = new ArrayList<>();
            for (AttributeConfig attributeConfig : module.attributeConfigs) {
                if (attributeConfig == null || attributeConfig.name == null) {
                    continue;
                }
                if (staticAttributeNames.contains(attributeConfig.name)) {
                    continue;
                }

                StoredAttribute storedAttribute = new StoredAttribute();
                storedAttribute.name = attributeConfig.name;
                storedAttribute.value = attributeConfig.value;
                storedAttributes.add(storedAttribute);
            }

            storedAttributes.sort(Comparator.comparing(attribute -> attribute.name == null ? "" : attribute.name));

            StringBuilder builder = new StringBuilder();
            for (StoredAttribute storedAttribute : storedAttributes) {
                builder.append(storedAttribute.name)
                        .append('=')
                        .append(storedAttribute.value == null ? "" : storedAttribute.value)
                        .append('\n');
            }

            try {
                SettingsFile.write(storagePath, builder.toString());
                module.dynamicAttributeStoragePath = storagePath.toAbsolutePath().normalize().toString();
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
        }

        writeConfigStorage(storage);
    }

    private static PipelineStorage capturePipelineStorage() {
        PipelineStorage pipelineStorage = new PipelineStorage();
        pipelineStorage.modules = new ArrayList<>();
        pipelineStorage.moduleConnections = new ArrayList<>();

        Map<Module, String> moduleToId = new HashMap<>();
        for (int index = 0; index < INSTANCE.modules.size(); index++) {
            Module module = INSTANCE.modules.get(index);

            String moduleId = "module_" + index;
            moduleToId.put(module, moduleId);

            StoredModule storedModule = new StoredModule();
            storedModule.id = moduleId;
            storedModule.entryName = module.name;
            storedModule.x = module.x;
            storedModule.y = module.y;
            storedModule.attributes = captureAttributes(module);

            pipelineStorage.modules.add(storedModule);
        }

        List<StoredConnection> storedConnections = new ArrayList<>();
        for (Map.Entry<ImageConfig, List<ImageConfig>> entry : INSTANCE.moduleConnections.entrySet()) {
            ImageConfig srcImageConfig = entry.getKey();
            List<ImageConfig> dstImageConfigs = entry.getValue();

            if (srcImageConfig == null || srcImageConfig.owner == null) {
                continue;
            }
            if (dstImageConfigs == null) {
                continue;
            }

            String srcModuleId = moduleToId.get(srcImageConfig.owner);
            if (srcModuleId == null) {
                continue;
            }

            for (ImageConfig dstImageConfig : dstImageConfigs) {
                if (dstImageConfig == null || dstImageConfig.owner == null) {
                    continue;
                }

                String dstModuleId = moduleToId.get(dstImageConfig.owner);
                if (dstModuleId == null) {
                    continue;
                }

                StoredConnection storedConnection = new StoredConnection();
                storedConnection.srcModuleId = srcModuleId;
                storedConnection.srcImageName = srcImageConfig.name;
                storedConnection.dstModuleId = dstModuleId;
                storedConnection.dstImageName = dstImageConfig.name;

                storedConnections.add(storedConnection);
            }
        }

        storedConnections.sort(
                Comparator.comparing((StoredConnection connection) -> connection.srcModuleId)
                        .thenComparing(connection -> connection.srcImageName)
                        .thenComparing(connection -> connection.dstModuleId)
                        .thenComparing(connection -> connection.dstImageName));

        pipelineStorage.moduleConnections.addAll(storedConnections);

        String finalOutputModuleId = null;
        String finalOutputImageName = null;
        for (int index = 0; index < INSTANCE.modules.size(); index++) {
            Module module = INSTANCE.modules.get(index);
            for (ImageConfig conf : module.outputImageConfigs) {
                if (!conf.finalOutput) {
                    continue;
                }
                if (finalOutputModuleId != null) {
                    throw new RuntimeException("Multiple final outputs detected! Only one allows.");
                }
                finalOutputModuleId = "module_" + index;
                finalOutputImageName = conf.name;
            }
        }

        pipelineStorage.finalOutputModuleId = finalOutputModuleId;
        pipelineStorage.finalOutputImageName = finalOutputImageName;

        return pipelineStorage;
    }

    private static List<StoredAttribute> captureAttributes(Module module) {
        List<StoredAttribute> out = new ArrayList<>();
        if (module == null || module.attributeConfigs == null) {
            return out;
        }
        Set<String> staticAttributeNames = Objects.equals(module.name, RAY_TRACING_MODULE_NAME)
                ? collectStaticAttributeNames(module)
                : null;

        for (AttributeConfig attributeConfig : module.attributeConfigs) {
            if (attributeConfig == null || attributeConfig.name == null) {
                continue;
            }
            if (staticAttributeNames != null && !staticAttributeNames.contains(attributeConfig.name)) {
                continue;
            }
            StoredAttribute storedAttribute = new StoredAttribute();
            storedAttribute.type = attributeConfig.type;
            storedAttribute.name = attributeConfig.name;
            storedAttribute.value = attributeConfig.value;
            out.add(storedAttribute);
        }
        return out;
    }

    static List<PresetStoredModule> capturePresetModules() {
        List<PresetStoredModule> list = new ArrayList<>();

        for (Module module : INSTANCE.modules) {
            PresetStoredModule storedModule = new PresetStoredModule();
            storedModule.entryName = module.name;
            storedModule.attributes = captureAttributes(module);
            list.add(storedModule);
        }

        list.sort(Comparator.comparing(m -> m.entryName == null ? "" : m.entryName));
        return list;
    }

    private static void writeConfigStorage(PipelineConfigStorage storage) {
        DumperOptions dumperOptions = new DumperOptions();
        dumperOptions.setDefaultFlowStyle(DumperOptions.FlowStyle.BLOCK);
        dumperOptions.setPrettyFlow(true);
        dumperOptions.setIndent(2);

        Yaml yaml = new Yaml(dumperOptions);
        String yamlText = yaml.dump(storage);

        try {
            SettingsFile.write(PIPELINE_CONFIG_PATH, yamlText);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    static PipelineConfigStorage loadConfigStorage() {
        if (PIPELINE_CONFIG_PATH == null || !Files.exists(PIPELINE_CONFIG_PATH)) {
            return null;
        }

        try {
            String yamlText = Files.readString(PIPELINE_CONFIG_PATH, StandardCharsets.UTF_8);

            LoaderOptions loaderOptions = new LoaderOptions();
            TagInspector tagInspector = tag -> tag.getClassName().startsWith("com.g2806.radiante.client.pipeline");
            loaderOptions.setTagInspector(tagInspector);

            Constructor constructor = new Constructor(PipelineConfigStorage.class, loaderOptions);
            Yaml yaml = new Yaml(constructor);

            PipelineConfigStorage storage = yaml.load(yamlText);
            if (storage != null) {
                storage.migrateLegacyFields();
            }
            return storage;
        } catch (Exception e) {
            // The caller takes null for "no file yet" and writes the defaults over it: what could not be read is
            // kept beside it first, so a pipeline the player set up is not lost to one bad line.
            Path kept = SettingsFile.keepDamaged(PIPELINE_CONFIG_PATH);
            RadianteClient.LOGGER.error("Could not read {}; starting from the defaults. The file was kept as {}.",
                PIPELINE_CONFIG_PATH, kept, e);
            return null;
        }
    }

    private static boolean hasUnavailableStoredModules(PipelineStorage pipelineStorage) {
        if (pipelineStorage == null || pipelineStorage.modules == null) {
            return true;
        }

        for (StoredModule storedModule : pipelineStorage.modules) {
            if (storedModule == null || storedModule.entryName == null) {
                continue;
            }
            if (!isModuleAvailable(storedModule.entryName)) {
                return true;
            }
        }

        return false;
    }

    static void applyPipelineStorage(PipelineStorage pipelineStorage) {
        if (pipelineStorage == null) {
            assembleDefault();
            return;
        }

        if (hasUnavailableStoredModules(pipelineStorage)) {
            RadianteClient.LOGGER.warn("Stored pipeline contains unavailable modules. Falling back to NRD+FSR.");
            assembleNRDFSR();
            return;
        }

        clear();

        if (pipelineStorage.modules == null || pipelineStorage.modules.isEmpty()
                || pipelineStorage.finalOutputModuleId == null || pipelineStorage.finalOutputImageName == null) {
            assembleDefault();
            return;
        }

        Map<String, Module> idToModule = new HashMap<>();

        for (StoredModule storedModule : pipelineStorage.modules) {
            if (storedModule == null || storedModule.id == null || storedModule.entryName == null) {
                continue;
            }

            Module module = addModule(storedModule.entryName);
            module.x = storedModule.x;
            module.y = storedModule.y;

            applyStoredAttributes(module, storedModule.attributes);

            idToModule.put(storedModule.id, module);
        }

        for (Module module : INSTANCE.modules) {
            for (ImageConfig conf : module.outputImageConfigs) {
                conf.finalOutput = false;
            }
        }

        Module finalModule = idToModule.get(pipelineStorage.finalOutputModuleId);
        if (finalModule == null) {
            assembleDefault();
            return;
        }

        ImageConfig finalImageConfig = finalModule.getOutputImageConfig(pipelineStorage.finalOutputImageName);
        if (finalImageConfig == null) {
            assembleDefault();
            return;
        }

        finalImageConfig.finalOutput = true;

        if (pipelineStorage.moduleConnections != null) {
            for (StoredConnection storedConnection : pipelineStorage.moduleConnections) {
                if (storedConnection == null) {
                    continue;
                }
                if (storedConnection.srcModuleId == null || storedConnection.dstModuleId == null) {
                    continue;
                }
                if (storedConnection.srcImageName == null || storedConnection.dstImageName == null) {
                    continue;
                }

                Module srcModule = idToModule.get(storedConnection.srcModuleId);
                Module dstModule = idToModule.get(storedConnection.dstModuleId);

                if (srcModule == null || dstModule == null) {
                    continue;
                }

                ImageConfig srcImageConfig = srcModule.getOutputImageConfig(storedConnection.srcImageName);
                ImageConfig dstImageConfig = dstModule.getInputImageConfig(storedConnection.dstImageName);

                if (srcImageConfig == null || dstImageConfig == null) {
                    continue;
                }

                connect(srcImageConfig, dstImageConfig);
            }
        }
    }
}
