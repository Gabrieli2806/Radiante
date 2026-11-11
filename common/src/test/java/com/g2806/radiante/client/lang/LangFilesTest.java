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
 * The three lang files say the same things: every text is in all of them, and a text that takes values ({@code %s})
 * takes as many in each language. A key added to one file only shows as its raw name in the others, and a missing
 * {@code %s} drops a value the player was meant to see.
 */
class LangFilesTest {

    private static final List<String> LANGUAGES = List.of("en_us", "es_es", "zh_cn");
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
