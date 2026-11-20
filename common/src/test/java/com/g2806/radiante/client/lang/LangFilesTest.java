package com.g2806.radiante.client.lang;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

/**
 * The lang files say the same things: every text is in all of them, a text that takes values ({@code %s}) takes as
 * many in each language, and every key the code names in full is there. A key added to one file only shows as its
 * raw name in the others (English text in the Spanish menu), and a missing {@code %s} drops a value the player was
 * meant to see. Every file in the lang folder is checked, so a language added later is held to the same.
 */
class LangFilesTest {

    private static final java.nio.file.Path LANG_FOLDER = java.nio.file.Path.of("src/main/resources/assets/radiante/lang");
    private static final List<String> LANGUAGES = languages();

    private static List<String> languages() {
        try (var files = java.nio.file.Files.list(LANG_FOLDER)) {
            return files.map(file -> file.getFileName().toString()).filter(name -> name.endsWith(".json"))
                .map(name -> name.substring(0, name.length() - ".json".length())).sorted().toList();
        } catch (IOException e) {
            throw new java.io.UncheckedIOException(e);
        }
    }
    private static final Pattern PLACEHOLDER = Pattern.compile("%(\\d+\\$)?[sd]");

    private static JsonObject load(String language) throws IOException {
        String path = "/assets/radiante/lang/" + language + ".json";
        try (InputStream in = LangFilesTest.class.getResourceAsStream(path)) {
            assertNotNull(in, path);
            return JsonParser.parseReader(new InputStreamReader(in, StandardCharsets.UTF_8)).getAsJsonObject();
        }
    }

    private static int placeholders(String text) {
        int count = 0;
        Matcher matcher = PLACEHOLDER.matcher(text.replace("%%", ""));
        while (matcher.find()) {
            count++;
        }
        return count;
    }

    @Test
    void everyLanguageHasTheSameKeys() throws IOException {
        Set<String> english = new TreeSet<>(load("en_us").keySet());
        for (String language : LANGUAGES) {
            Set<String> keys = new TreeSet<>(load(language).keySet());
            Set<String> missing = new TreeSet<>(english);
            missing.removeAll(keys);
            Set<String> extra = new TreeSet<>(keys);
            extra.removeAll(english);
            assertEquals(Set.of(), missing, language + " lacks texts en_us has");
            assertEquals(Set.of(), extra, language + " has texts en_us lacks");
        }
    }

    @Test
    void theLanguagesAreFound() {
        org.junit.jupiter.api.Assertions.assertTrue(LANGUAGES.contains("en_us") && LANGUAGES.size() >= 2,
            "lang files found: " + LANGUAGES);
    }

    /**
     * Every key the code writes out in full ("options.radiante.light_style", not a prefix the code adds to) is in
     * en_us, as a text or as the start of texts (a group of values under it). A key typed in the code and never added
     * shows as its raw name in every language.
     */
    @Test
    void everyKeyTheCodeNamesExists() throws IOException {
        Set<String> english = load("en_us").keySet();
        Pattern literal = Pattern.compile("\"((?:options|gui)\\.radiante\\.[a-z0-9_.]+)\"(\\s*\\+)?");
        Set<String> missing = new TreeSet<>();
        try (var sources = java.nio.file.Files.walk(java.nio.file.Path.of("src/main/java"))) {
            for (java.nio.file.Path file : sources.filter(path -> path.toString().endsWith(".java")).toList()) {
                Matcher matcher = literal.matcher(java.nio.file.Files.readString(file));
                while (matcher.find()) {
                    String key = matcher.group(1);
                    if (matcher.group(2) != null || key.endsWith(".")) {
                        continue;
                    }
                    if (!english.contains(key) && english.stream().noneMatch(text -> text.startsWith(key + "."))) {
                        missing.add(key + " (" + file.getFileName() + ")");
                    }
                }
            }
        }
        assertEquals(Set.of(), missing, "keys the code names that en_us lacks");
    }

    @Test
    void everyLanguageTakesTheSameValues() throws IOException {
        JsonObject english = load("en_us");
        List<String> different = new ArrayList<>();
        for (String language : LANGUAGES) {
            JsonObject texts = load(language);
            for (Map.Entry<String, com.google.gson.JsonElement> entry : english.entrySet()) {
                if (texts.has(entry.getKey()) && placeholders(entry.getValue().getAsString())
                    != placeholders(texts.get(entry.getKey()).getAsString())) {
                    different.add(language + ": " + entry.getKey());
                }
            }
        }
        assertEquals(List.of(), different, "texts whose %s count differs from en_us");
    }
}
