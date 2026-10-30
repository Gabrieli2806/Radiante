package com.g2806.radiante.client.pipeline;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

import java.util.Locale;

/**
 * The checks of a shader pack's manifest that can be made without loading it: the pack it {@code extends}, the
 * {@code api_version} it is written for, and the Radiante version it {@code requires}. A pack that fails one is
 * listed with the reason and not loaded. No game classes here, so the rules are unit tested
 * ({@code ShaderPackManifestTest}).
 *
 * <p>The base id and major must match the native loader's ({@code shader_pack_extends.hpp}:
 * {@code BUILT_IN_BASE_ID}, {@code BUILT_IN_BASE_MAJOR}), which refuses the same packs when they are loaded anyway.
 */
public final class ShaderPackManifest {

    /** The one pack others can build on today. */
    public static final String BASE_ID = "radiante-pt";
    /** The major version of radiante-pt's pack interface (its hook points): {@code "extends": "radiante-pt@1"}. */
    public static final int BASE_MAJOR = 1;
    /**
     * The newest {@code api_version} this Radiante offers. 0 is "built on Radiante PT's files" (every pack today);
     * the versioned {@code radiante/v1} headers are phase 2 of the shader pack roadmap.
     */
    public static final int API_VERSION = 0;

    /** Why a pack cannot be used: a translation key and its argument. */
    public record Problem(String key, String detail) {
    }

    private ShaderPackManifest() {
    }

    /** The first problem of a manifest (null when there is none or no manifest), checked in the order above. */
    public static Problem check(JsonObject manifest, String modVersion) {
        if (manifest == null) {
            return null;
        }
        Problem problem = extendsProblem(manifest.get("extends"));
        if (problem == null) {
            problem = apiVersionProblem(manifest.get("api_version"));
        }
        if (problem == null) {
            problem = requiresProblem(manifest.get("requires"), modVersion);
        }
        return problem;
    }

    /** {@code "radiante-pt"} or {@code "radiante-pt@1"}; anything else is a pack Radiante cannot build on. */
    static Problem extendsProblem(JsonElement value) {
        if (value == null || value.isJsonNull()) {
            return null;
        }
        if (!value.isJsonPrimitive() || !value.getAsJsonPrimitive().isString()) {
            return new Problem("screen.radiante.shader_packs.problem.base", value.toString());
        }
        String text = value.getAsString().trim();
        int at = text.indexOf('@');
        String id = (at < 0 ? text : text.substring(0, at)).toLowerCase(Locale.ROOT);
        if (!id.equals(BASE_ID)) {
            return new Problem("screen.radiante.shader_packs.problem.base", text);
        }
        if (at >= 0) {
            String major = text.substring(at + 1);
            if (!major.matches("\\d{1,4}")) {
                return new Problem("screen.radiante.shader_packs.problem.base", text);
            }
            if (Integer.parseInt(major) != BASE_MAJOR) {
                return new Problem("screen.radiante.shader_packs.problem.base_version", text);
            }
        }
        return null;
    }

    /** A pack written for a newer interface than this Radiante offers. */
    static Problem apiVersionProblem(JsonElement value) {
        if (value == null || value.isJsonNull() || !value.isJsonPrimitive()) {
            return null;
        }
        int version;
        try {
            version = (int) Math.floor(Double.parseDouble(value.getAsString().trim()));
        } catch (NumberFormatException e) {
            return null; // Shown as written; only a number can be checked.
        }
        return version > API_VERSION
            ? new Problem("screen.radiante.shader_packs.problem.api_version", Integer.toString(version)) : null;
    }

    /**
     * {@code "requires": {"radiante": ">=1.2"}}: the Radiante version the pack needs ({@code >=}, {@code >},
     * {@code =}, {@code <=}, {@code <}, or a bare version meaning {@code >=}). {@code "features"} is not checked
     * yet.
     */
    static Problem requiresProblem(JsonElement value, String modVersion) {
        if (value == null || !value.isJsonObject()) {
            return null;
        }
        JsonElement radiante = value.getAsJsonObject().get("radiante");
        if (radiante == null || !radiante.isJsonPrimitive() || modVersion == null || modVersion.isBlank()) {
            return null;
        }
        String wanted = radiante.getAsString().trim();
        String operator = ">=";
        for (String candidate : new String[] {">=", "<=", "==", ">", "<", "="}) {
            if (wanted.startsWith(candidate)) {
                operator = candidate;
                wanted = wanted.substring(candidate.length()).trim();
                break;
            }
        }
        if (!wanted.matches("\\d+(\\.\\d+)*")) {
            return new Problem("screen.radiante.shader_packs.problem.requires", radiante.getAsString());
        }
        int order = compareVersions(modVersion, wanted);
        boolean met = switch (operator) {
            case ">" -> order > 0;
            case "<=" -> order <= 0;
            case "<" -> order < 0;
            case "=", "==" -> order == 0;
            default -> order >= 0;
        };
        return met ? null : new Problem("screen.radiante.shader_packs.problem.requires", radiante.getAsString());
    }

    /** Dotted versions compared number by number ("1.10" after "1.9"); "+26.3" or "-beta" suffixes are ignored. */
    static int compareVersions(String a, String b) {
        String[] left = plain(a).split("\\.");
        String[] right = plain(b).split("\\.");
        for (int i = 0; i < Math.max(left.length, right.length); i++) {
            int l = i < left.length ? number(left[i]) : 0;
            int r = i < right.length ? number(right[i]) : 0;
            if (l != r) {
                return Integer.compare(l, r);
            }
        }
        return 0;
    }

    private static String plain(String version) {
        String text = version.trim();
        int cut = text.length();
        for (char separator : new char[] {'+', '-', ' '}) {
            int index = text.indexOf(separator);
            if (index >= 0) {
                cut = Math.min(cut, index);
            }
        }
        return text.substring(0, cut);
    }

    private static int number(String part) {
        try {
            return Integer.parseInt(part);
        } catch (NumberFormatException e) {
            return 0;
        }
    }
}
