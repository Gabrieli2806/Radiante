package com.g2806.radiante.client;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/**
 * Development: a virtual gamepad for testing controller support without a controller. Controlify reads controllers
 * through SDL, and SDL can make a virtual one: it is attached through Controlify's own SDL, so Controlify finds it as
 * it would a real one, and DevAutomation's {@code pad=} steps press its buttons and move its sticks. Everything goes
 * through reflection; without Controlify the steps only log that they did nothing.
 */
final class DevGamepad {

    private static final String[] BUTTONS = {"south", "east", "west", "north", "back", "guide", "start", "ls", "rs",
        "lb", "rb", "up", "down", "left", "right"};
    private static final int AXES = 6;

    private static Object joystickApi;
    private static Object handle;
    private static Method setButton;
    private static Method setAxis;

    private record Release(int ticksLeft, Runnable action) {
    }

    private static final List<Release> releases = new ArrayList<>();

    private DevGamepad() {
    }

    /** {@code pad=connect}, {@code pad=<button>[,ticks]} (pressed for so many ticks), {@code pad=axis,<i>,<value>}. */
    static void run(String step) {
        try {
            String[] parts = step.split(",");
            if (parts[0].equals("connect")) {
                connect();
                return;
            }
            if (handle == null) {
                RadianteClient.LOGGER.warn("[dev] pad {}: no virtual pad connected", step);
                return;
            }
            if (parts[0].equals("axis")) {
                int axis = Integer.parseInt(parts[1]);
                short value = (short) Integer.parseInt(parts[2]);
                setAxis.invoke(joystickApi, handle, axis, value);
                RadianteClient.LOGGER.info("[dev] pad axis {} = {}", axis, value);
                return;
            }
            int button = List.of(BUTTONS).indexOf(parts[0]);
            if (button < 0) {
                RadianteClient.LOGGER.warn("[dev] pad: unknown button {}", parts[0]);
                return;
            }
            int ticks = parts.length > 1 ? Integer.parseInt(parts[1]) : 2;
            setButton.invoke(joystickApi, handle, button, true);
            releases.add(new Release(ticks, () -> {
                try {
                    setButton.invoke(joystickApi, handle, button, false);
                } catch (ReflectiveOperationException e) {
                    RadianteClient.LOGGER.warn("[dev] pad release failed", e);
                }
            }));
            RadianteClient.LOGGER.info("[dev] pad {} pressed for {} ticks", parts[0], ticks);
        } catch (ReflectiveOperationException | RuntimeException e) {
            RadianteClient.LOGGER.warn("[dev] pad {} failed", step, e);
        }
    }

    static void tick() {
        for (int i = releases.size() - 1; i >= 0; i--) {
            Release release = releases.get(i);
            if (release.ticksLeft() <= 1) {
                releases.remove(i);
                release.action().run();
            } else {
                releases.set(i, new Release(release.ticksLeft() - 1, release.action()));
            }
        }
    }

    private static void connect() throws ReflectiveOperationException {
        ClassLoader loader = DevGamepad.class.getClassLoader();
        Class<?> controlify = Class.forName("dev.isxander.controlify.Controlify", true, loader);
        Object instance = controlify.getMethod("instance").invoke(null);
        Object manager = ((java.util.Optional<?>) controlify.getMethod("getControllerManager").invoke(instance))
            .orElseThrow();
        Field sdlField = manager.getClass().getDeclaredField("sdl");
        sdlField.setAccessible(true);
        Object sdl = sdlField.get(manager);
        Class<?> sdlClass = Class.forName("dev.isxander.sdl.Sdl", true, loader);
        joystickApi = sdlClass.getMethod("joystick").invoke(sdl);
        Class<?> joystickClass = Class.forName("dev.isxander.sdl.SdlJoystick", true, loader);
        int gamepadType = joystickClass.getField("SDL_JOYSTICK_TYPE_GAMEPAD").getInt(null);
        Class<?> descClass = Class.forName("dev.isxander.sdl.SdlVirtualJoystickDesc", true, loader);
        Object desc = descClass.getConstructors()[0].newInstance(gamepadType, 0x045E, 0x028E, AXES, BUTTONS.length, 0,
            0, (1 << BUTTONS.length) - 1, (1 << AXES) - 1, "Radiante test pad",
            java.lang.reflect.Array.newInstance(Class.forName("dev.isxander.sdl.SdlVirtualJoystickTouchpadDesc", true,
                loader), 0),
            java.lang.reflect.Array.newInstance(Class.forName("dev.isxander.sdl.SdlVirtualJoystickSensorDesc", true,
                loader), 0),
            null, null, null, null, null, null, null, null, null);
        Class<?> idClass = Class.forName("dev.isxander.sdl.SdlJoystickId", true, loader);
        Object id = joystickClass.getMethod("SDL_AttachVirtualJoystick", descClass).invoke(joystickApi, desc);
        Class<?> handleClass = Class.forName("dev.isxander.sdl.SdlJoystickHandle", true, loader);
        handle = joystickClass.getMethod("SDL_OpenJoystick", idClass).invoke(joystickApi, id);
        setButton = joystickClass.getMethod("SDL_SetJoystickVirtualButton", handleClass, int.class, boolean.class);
        setAxis = joystickClass.getMethod("SDL_SetJoystickVirtualAxis", handleClass, int.class, short.class);
        RadianteClient.LOGGER.info("[dev] virtual pad attached: {}", id);
    }
}
