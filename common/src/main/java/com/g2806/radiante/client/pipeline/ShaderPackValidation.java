package com.g2806.radiante.client.pipeline;

import com.g2806.radiante.client.RadianteClient;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * A shader pack loaded and compiled without being used ({@link Pipeline#validateShaderPackNative}): what
 * {@code /radiante pack validate} prints, and what decides whether an edited folder pack is reloaded
 * ({@code ShaderPackHotReload}). {@code errors} holds the loader's reason or each shader's compiler output, with file
 * and line; {@code shaders} counts the distinct compiles and {@code compiled} those the SPIR-V cache did not have.
 */
public record ShaderPackValidation(String packPath, boolean ok, List<String> errors, int shaders, int compiled) {

    /** One check at a time: they share the renderer's folder for checks, and each keeps every core busy anyway. */
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "Radiante shader pack check");
        thread.setDaemon(true);
        return thread;
    });

    /**
     * Checks a pack on a background thread. The attributes are read here, so call it on the client thread; the result
     * comes back on the worker thread (hand it to the client thread before touching the game).
     */
    public static CompletableFuture<ShaderPackValidation> start(String packPath) {
        String[] attributes = Pipeline.shaderPackValidationAttributes(packPath);
        String language = Pipeline.languageCode();
        return CompletableFuture.supplyAsync(() -> run(packPath, attributes, language), WORKER);
    }

    private static ShaderPackValidation run(String packPath, String[] attributes, String language) {
        try {
            return parse(packPath, Pipeline.validateShaderPackNative(packPath, attributes, language));
        } catch (UnsatisfiedLinkError olderRenderer) {
            return new ShaderPackValidation(packPath, false,
                List.of("this build of the renderer cannot check packs (" + olderRenderer.getMessage() + ")"), 0, 0);
        } catch (RuntimeException e) {
            RadianteClient.LOGGER.warn("Checking shader pack {} failed", packPath, e);
            return new ShaderPackValidation(packPath, false, List.of(String.valueOf(e.getMessage())), 0, 0);
        }
    }

    static ShaderPackValidation parse(String packPath, String json) {
        if (json == null || json.isBlank()) {
            return new ShaderPackValidation(packPath, false, List.of("the renderer gave no report"), 0, 0);
        }
        JsonObject root = JsonParser.parseString(json).getAsJsonObject();
        List<String> errors = new ArrayList<>();
        if (root.has("error") && root.get("error").isJsonPrimitive()) {
            String error = root.get("error").getAsString();
            if (!error.isBlank()) {
                errors.add(error);
            }
        }
        if (root.has("errors") && root.get("errors").isJsonArray()) {
            JsonArray list = root.getAsJsonArray("errors");
            for (JsonElement entry : list) {
                if (entry.isJsonPrimitive()) {
                    errors.add(entry.getAsString());
                }
            }
        }
        boolean ok = root.has("ok") && root.get("ok").getAsBoolean() && errors.isEmpty();
        int shaders = root.has("shaders") ? root.get("shaders").getAsInt() : 0;
        int compiled = root.has("compiled") ? root.get("compiled").getAsInt() : 0;
        return new ShaderPackValidation(packPath, ok, List.copyOf(errors), shaders, compiled);
    }

    /** The report as lines, each error's own lines kept: the Shader Packs screen and the log show it whole. */
    public List<String> reportLines() {
        List<String> lines = new ArrayList<>();
        for (String error : this.errors) {
            for (String line : error.split("\\R")) {
                if (!line.isBlank()) {
                    lines.add(line);
                }
            }
        }
        return lines;
    }
}
