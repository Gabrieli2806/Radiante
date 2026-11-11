package com.g2806.radiante.client.compat;

/**
 * Whether another mod is installed, asked the one way that works on every loader: by one of its classes. Each
 * integration in this package starts by asking, and touches the other mod's own classes only after a yes, so a
 * player without that mod never loads code that needs it.
 */
public final class OtherMods {

    private OtherMods() {
    }

    /**
     * True when the class is there to be loaded. It is not initialised: a mod's class may not be ready to run this
     * early, and finding it is all that is asked.
     */
    public static boolean has(String className) {
        try {
            Class.forName(className, false, OtherMods.class.getClassLoader());
            return true;
        } catch (ClassNotFoundException | LinkageError notInstalled) {
            return false;
        }
    }
}
