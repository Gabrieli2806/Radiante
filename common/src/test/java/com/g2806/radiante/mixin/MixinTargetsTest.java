package com.g2806.radiante.mixin;

import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import org.junit.jupiter.api.Test;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.Type;
import org.objectweb.asm.tree.AnnotationNode;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.FieldNode;
import org.objectweb.asm.tree.MethodNode;

/**
 * A mixin names what it changes in strings (the method it injects into, the call it wraps, the field it shadows), so
 * one that names something this Minecraft version does not have still compiles, and only fails once the game starts.
 * This looks every such name up in the Minecraft the build compiles against: after a port to another version, or a
 * merge between the version branches, it says which mixins no longer match before anyone starts a client.
 *
 * <p>It checks names, not descriptors: that a method, field or class of that name is there. A mixin for another mod
 * (a class that is not on the build's class path) is left out.
 */
class MixinTargetsTest {

    private static final Path CONFIG = Path.of("src/main/resources/radiante.mixins.json");

    // The mixin annotations stay in the class file but are not kept for reflection, so the mixins are read as class
    // files; what they name is then looked up in Minecraft's classes by reflection.
    private static final String MIXIN = "Lorg/spongepowered/asm/mixin/Mixin;";
    private static final String PSEUDO = "Lorg/spongepowered/asm/mixin/Pseudo;";
    private static final String SHADOW = "Lorg/spongepowered/asm/mixin/Shadow;";
    private static final String ACCESSOR = "Lorg/spongepowered/asm/mixin/gen/Accessor;";
    private static final String INVOKER = "Lorg/spongepowered/asm/mixin/gen/Invoker;";

    @Test
    void everyMixinNamesThingsThisMinecraftHas() throws IOException {
        JsonObject config = JsonParser.parseString(Files.readString(CONFIG)).getAsJsonObject();
        String base = config.get("package").getAsString();
        List<String> problems = new ArrayList<>();
        int checked = 0;
        for (String list : List.of("mixins", "client", "server")) {
            if (!config.has(list)) {
                continue;
            }
            for (JsonElement entry : config.getAsJsonArray(list)) {
                checked += check(base + "." + entry.getAsString(), problems);
            }
        }
        assertTrue(problems.isEmpty(), "mixins that name things this Minecraft version does not have:\n  "
            + String.join("\n  ", problems));
        // Fewer than this and the test has stopped seeing the mixins (their annotations, or Minecraft's classes).
        assertTrue(checked > 100, "only " + checked + " names were looked up");
    }

    /** Returns how many names of this mixin were looked up. */
    private static int check(String mixinName, List<String> problems) throws IOException {
        ClassNode mixin = new ClassNode();
        try (InputStream in = MixinTargetsTest.class.getClassLoader()
            .getResourceAsStream(mixinName.replace('.', '/') + ".class")) {
            if (in == null) {
                problems.add(mixinName + ": listed in " + CONFIG.getFileName() + " but not a class");
                return 1;
            }
            new ClassReader(in).accept(mixin, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG);
        }
        String simple = mixinName.substring(mixinName.lastIndexOf('.') + 1);
        AnnotationNode annotation = annotation(mixin.visibleAnnotations, mixin.invisibleAnnotations, MIXIN);
        if (annotation == null) {
            problems.add(mixinName + ": no @Mixin");
            return 1;
        }
        boolean otherMod = annotation(mixin.visibleAnnotations, mixin.invisibleAnnotations, PSEUDO) != null;
        List<String> targetNames = new ArrayList<>();
        for (Object type : list(value(annotation, "value"))) {
            targetNames.add(((Type) type).getClassName());
        }
        for (Object name : list(value(annotation, "targets"))) {
            targetNames.add(((String) name).replace('/', '.'));
        }
        int checked = 0;
        for (String targetName : targetNames) {
            Class<?> target;
            Set<String> methods = new HashSet<>();
            Set<String> fields = new HashSet<>();
            try {
                target = load(targetName);
                members(target, methods, fields);
            } catch (ClassNotFoundException | LinkageError e) {
                // Another mod's class, which the build does not have, is not this check's to judge.
                if (!otherMod) {
                    checked++;
                    problems.add(simple + ": its target " + targetName + " is not in this Minecraft (" + e + ")");
                }
                continue;
            }
            checked++;
            for (MethodNode method : mixin.methods) {
                String where = simple + "." + method.name + " -> " + target.getSimpleName();
                List<AnnotationNode> annotations = new ArrayList<>();
                if (method.visibleAnnotations != null) {
                    annotations.addAll(method.visibleAnnotations);
                }
                if (method.invisibleAnnotations != null) {
                    annotations.addAll(method.invisibleAnnotations);
                }
                for (AnnotationNode found : annotations) {
                    if (!found.desc.startsWith("Lorg/spongepowered/") && !found.desc.startsWith("Lcom/llamalad7/")) {
                        continue;
                    }
                    if (found.desc.equals(SHADOW)) {
                        checked++;
                        String name = strip(method.name, value(found, "prefix"));
                        if (!methods.contains(name) && !anyOf(methods, list(value(found, "aliases")))) {
                            problems.add(where + ": no method " + name + " to shadow");
                        }
                    } else if (found.desc.equals(ACCESSOR)) {
                        checked++;
                        String name = value(found, "value") instanceof String given && !given.isEmpty() ? given
                            : property(method.name, "get", "set", "is");
                        if (!fields.contains(name)) {
                            problems.add(where + ": no field " + name + " for the accessor");
                        }
                    } else if (found.desc.equals(INVOKER)) {
                        checked++;
                        String name = value(found, "value") instanceof String given && !given.isEmpty() ? given
                            : property(method.name, "call", "invoke", "new");
                        if (!name.equals("<init>") && !methods.contains(name)) {
                            problems.add(where + ": no method " + name + " for the invoker");
                        }
                    }
                    // The injectors: where they go in ("method") and what they look for there ("at").
                    for (Object selector : list(value(found, "method"))) {
                        checked++;
                        String name = methodName((String) selector);
                        if (!name.equals("<init>") && !name.equals("<clinit>") && !name.equals("*")
                            && !methods.contains(name)) {
                            problems.add(where + ": no method " + name + " to inject into");
                        }
                    }
                    for (Object at : list(value(found, "at"))) {
                        if (at instanceof AnnotationNode point && value(point, "target") instanceof String aim
                            && !aim.isEmpty()) {
                            checked++;
                            String problem = member(aim);
                            if (problem != null) {
                                problems.add(where + ": @At target " + aim + ": " + problem);
                            }
                        }
                    }
                }
            }
            for (FieldNode field : mixin.fields) {
                AnnotationNode shadow = annotation(field.visibleAnnotations, field.invisibleAnnotations, SHADOW);
                if (shadow != null) {
                    checked++;
                    String name = strip(field.name, value(shadow, "prefix"));
                    if (!fields.contains(name) && !anyOf(fields, list(value(shadow, "aliases")))) {
                        problems.add(simple + " -> " + target.getSimpleName() + ": no field " + name + " to shadow");
                    }
                }
            }
        }
        return checked;
    }

    private static AnnotationNode annotation(List<AnnotationNode> visible, List<AnnotationNode> invisible,
        String descriptor) {
        for (List<AnnotationNode> annotations : Arrays.asList(visible, invisible)) {
            if (annotations != null) {
                for (AnnotationNode annotation : annotations) {
                    if (annotation.desc.equals(descriptor)) {
                        return annotation;
                    }
                }
            }
        }
        return null;
    }

    /** An annotation's value of that name as the class file holds it, or null when it was left at its default. */
    private static Object value(AnnotationNode annotation, String name) {
        if (annotation.values != null) {
            for (int i = 0; i + 1 < annotation.values.size(); i += 2) {
                if (annotation.values.get(i).equals(name)) {
                    return annotation.values.get(i + 1);
                }
            }
        }
        return null;
    }

    /** A value that may be one thing or several, as several. */
    private static List<?> list(Object value) {
        if (value == null) {
            return List.of();
        }
        return value instanceof List<?> many ? many : List.of(value);
    }

    /** The methods and fields of a class and of what it extends, by name. */
    private static void members(Class<?> type, Set<String> methods, Set<String> fields) {
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            for (Method method : current.getDeclaredMethods()) {
                methods.add(method.getName());
            }
            for (Field field : current.getDeclaredFields()) {
                fields.add(field.getName());
            }
            for (Class<?> implemented : current.getInterfaces()) {
                members(implemented, methods, fields);
            }
        }
    }

    /**
     * Looks up the owner and the member of an {@code @At} target written as {@code Lowner;name(...)} or
     * {@code Lowner;name:type}; null when it is there, or when the target is not of that form (a class for NEW, a
     * bare name).
     */
    private static String member(String target) {
        int end = target.indexOf(';');
        if (!target.startsWith("L") || end < 0 || end + 1 >= target.length()) {
            return null;
        }
        String owner = target.substring(1, end).replace('/', '.');
        String rest = target.substring(end + 1);
        int cut = rest.indexOf('(');
        if (cut < 0) {
            cut = rest.indexOf(':');
        }
        String name = cut < 0 ? rest : rest.substring(0, cut);
        Class<?> type;
        try {
            type = load(owner);
        } catch (ClassNotFoundException | LinkageError e) {
            // Another mod's or a library's class that the build does not have is not this check's to judge.
            return owner.startsWith("net.minecraft.") || owner.startsWith("com.mojang.") ? "no class " + owner : null;
        }
        if (name.equals("<init>") || name.isEmpty()) {
            return null;
        }
        Set<String> methods = new HashSet<>();
        Set<String> fields = new HashSet<>();
        try {
            members(type, methods, fields);
        } catch (LinkageError e) {
            return null;
        }
        return methods.contains(name) || fields.contains(name) ? null : owner + " has no " + name;
    }

    private static String methodName(String selector) {
        String name = selector;
        int owner = name.indexOf(';');
        if (name.startsWith("L") && owner >= 0) {
            name = name.substring(owner + 1);
        }
        int descriptor = name.indexOf('(');
        return descriptor < 0 ? name : name.substring(0, descriptor);
    }

    private static String strip(String name, Object givenPrefix) {
        String prefix = givenPrefix instanceof String given ? given : "shadow$";
        return name.startsWith(prefix) ? name.substring(prefix.length()) : name;
    }

    /** getCaption gives caption, callRegister gives register. */
    private static String property(String name, String... prefixes) {
        for (String prefix : prefixes) {
            if (name.startsWith(prefix) && name.length() > prefix.length()) {
                String rest = name.substring(prefix.length());
                return rest.substring(0, 1).toLowerCase(Locale.ROOT) + rest.substring(1);
            }
        }
        return name;
    }

    private static boolean anyOf(Set<String> names, List<?> candidates) {
        for (Object candidate : candidates) {
            if (names.contains(candidate)) {
                return true;
            }
        }
        return false;
    }

    private static Class<?> load(String name) throws ClassNotFoundException {
        // Not initialised: only its shape is read, and Minecraft's classes are not made to start outside the game.
        return Class.forName(name, false, MixinTargetsTest.class.getClassLoader());
    }
}
