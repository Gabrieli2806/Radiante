package com.g2806.radiante.client;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

/**
 * The Java runtime's own crash file (hs_err_pid*.log, written when native code crashes the game) copies the whole
 * command line the launcher started the game with, and with it the player's sign-in token (--accessToken). Bug
 * reports ask for that file, so the token went out with it. On the next start the token in each such file in the
 * game folder is blanked; the rest of the file is left as it is.
 */
final class CrashLogScrub {

    private static final Pattern TOKEN = Pattern.compile("(--accessToken[ =]+)([^\s\"]+)");
    private static final String REMOVED = "<removed by Radiante>";

    private CrashLogScrub() {
    }

    static void run(Path gameDir) {
        try (Stream<Path> files = Files.list(gameDir)) {
            for (Path file : files.filter(CrashLogScrub::isCrashLog).toList()) {
                scrub(file);
            }
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.debug("Could not look for crash files to scrub", e);
        }
    }

    private static boolean isCrashLog(Path file) {
        String name = file.getFileName().toString();
        return name.startsWith("hs_err_pid") && name.endsWith(".log") && Files.isRegularFile(file);
    }

    private static void scrub(Path file) {
        try {
            String text = new String(Files.readAllBytes(file), StandardCharsets.ISO_8859_1);
            Matcher matcher = TOKEN.matcher(text);
            StringBuilder out = new StringBuilder(text.length());
            boolean changed = false;
            while (matcher.find()) {
                if (matcher.group(2).startsWith("<removed")) {
                    // Already blanked on an earlier start.
                    matcher.appendReplacement(out, Matcher.quoteReplacement(matcher.group()));
                    continue;
                }
                changed = true;
                matcher.appendReplacement(out, Matcher.quoteReplacement(matcher.group(1) + REMOVED));
            }
            matcher.appendTail(out);
            if (changed) {
                Files.write(file, out.toString().getBytes(StandardCharsets.ISO_8859_1));
                RadianteClient.LOGGER.info("Removed the sign-in token from {}", file.getFileName());
            }
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.debug("Could not scrub {}", file, e);
        }
    }
}
