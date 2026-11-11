package com.g2806.radiante.client.gui;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;

/**
 * The settings screen keeps a plain option's choice until Apply under the option's field name, as text
 * ({@code stage("heldItemLight", value)}), and Apply writes it to the field of that name in {@code Options}. A name
 * with a typo, or one left behind when a field is renamed, compiles and is only a warning in the log when the player
 * applies. This reads the names out of the screen's sources and looks each one up.
 */
class StagedOptionsTest {

    private static final Path SOURCES = Path.of("src/main/java/com/g2806/radiante/client/gui");
    private static final Pattern STAGED = Pattern.compile("\\bstaged?\\(\\s*\"([A-Za-z0-9_]+)\"");

    @Test
    void everyStagedNameIsAnOptionsField() throws IOException, ClassNotFoundException {
        Set<String> names = new TreeSet<>();
        try (Stream<Path> files = Files.list(SOURCES)) {
            for (Path file : files.filter(path -> path.toString().endsWith(".java")).toList()) {
                Matcher matcher = STAGED.matcher(Files.readString(file));
                while (matcher.find()) {
                    names.add(matcher.group(1));
                }
            }
        }
        assertTrue(names.size() > 10, "only " + names.size() + " staged options found in " + SOURCES);
        // Not initialised: only the fields' names are read.
        Class<?> options = Class.forName("com.g2806.radiante.client.option.Options", false,
            StagedOptionsTest.class.getClassLoader());
        List<String> missing = new ArrayList<>();
        for (String name : names) {
            if (name.equals("renderDistance")) {
                // Minecraft's own option, which Apply writes by its own way (SettingsChoices.applyStaged).
                continue;
            }
            try {
                options.getField(name);
            } catch (NoSuchFieldException e) {
                missing.add(name);
            }
        }
        assertTrue(missing.isEmpty(), "staged in the settings screen but not a public field of Options: " + missing);
    }
}
