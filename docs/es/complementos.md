# Opciones de complementos

Idiomas: **ES** · [EN](../en/addons.md)

Un mod hecho sobre Radiante (un *complemento*) puede poner sus opciones en la pantalla de ajustes de
Radiante, en vez de escribir su propia pantalla y su propio archivo de configuración. Cuando al menos
un complemento se registra, los ajustes ganan una sección **Complementos** al final de la barra
lateral, con un título por complemento (su icono, nombre y versión) y sus opciones debajo. Sin complementos
no cambia nada.

Las opciones de un complemento se comportan como las de Radiante: esperan a **Aplicar** o **Listo**,
**Deshacer** las descarta, **Restablecer valores** les devuelve los valores por defecto del
complemento, la búsqueda las encuentra (o todas las de un complemento por su nombre), el panel de
descripción muestra su texto de ayuda, y el teclado y el mando se mueven por ellas.

## La API

Todo está en `com.g2806.radiante.api.RadianteAddons`; el resto de Radiante es interno. Es Java normal,
sin punto de entrada propio de un loader, así que la misma llamada funciona en Fabric, NeoForge y
Forge. Llámala una vez desde el inicializador **de cliente** de tu mod (Radiante solo va en el
cliente):

```java
import com.g2806.radiante.api.RadianteAddons;
import java.util.List;
import net.minecraft.network.chat.Component;

public final class MyAddonClient {
    public static RadianteAddons.Setting<Boolean> debugRays;
    public static RadianteAddons.Setting<String> aiMode;
    public static RadianteAddons.Setting<Integer> range;

    public static void init() {
        RadianteAddons.Addon addon = RadianteAddons.register("mymod", Component.translatable("mymod.name"));

        debugRays = addon.toggle("debug_rays", Component.translatable("mymod.debug_rays"),
            Component.translatable("mymod.debug_rays.tooltip"), false);

        aiMode = addon.cycle("ai_mode", Component.translatable("mymod.ai_mode"),
            Component.translatable("mymod.ai_mode.tooltip"), List.of("off", "avoid_sun", "seek_shade"), "avoid_sun",
            value -> Component.translatable("mymod.ai_mode." + value));

        range = addon.slider("range", Component.translatable("mymod.range"),
            Component.translatable("mymod.range.tooltip"), 8, 64, 4, 16)
            .onChange(value -> MyAddon.LOGGER.info("Range is now {}", value));
    }
}
```

Después lee el valor donde haga falta, desde cualquier hilo (también el hilo del servidor en un
jugador): `if (MyAddonClient.debugRays.get()) { ... }`.

| Llamada | Se ve como |
|---|---|
| `toggle(key, title, tooltip, default)` | una casilla |
| `cycle(key, title, tooltip, values, default)` (con una función `label` opcional) | un valor que pasa por `values` al hacer clic |
| `slider(key, title, tooltip, min, max, step, default)` | un deslizador de números enteros |

- `id` es el id de tu mod: Radiante muestra la versión del mod junto al título y guarda los valores
  en `radiante/addons/<id>.properties`. Los ids y las claves usan minúsculas, dígitos, `_`, `-` y `.`.
- El icono a la izquierda del título es el de tu mod, buscado por ese id: el `icon` de
  `fabric.mod.json`, o el `logoFile` de `neoforge.mods.toml` / `mods.toml`. Un PNG cuadrado de 32 x 32
  o menos se muestra tal cual (el pixel art queda nítido); uno más grande se reduce. Para mostrar
  otra textura, llama a `addon.icon(Identifier.fromNamespaceAndPath("mymod", "textures/gui/addon.png"))`
  después de `register`. Un icono que falta o no se puede leer se registra una vez en el log y el
  título queda sin icono.
- Los títulos y textos de ayuda son tus propios `Component`; pon sus textos en los archivos de idioma
  de tu mod. El texto de ayuda puede ser `null`. Que sea corto: qué hace la opción y qué cambia al
  cambiarla.
- Los `onChange` se llaman en el hilo del cliente cuando el jugador aplica un cambio.
- Los errores no rompen la pantalla de ajustes: un id o clave no válido o repetido, un valor por
  defecto que no está entre los valores, o un `onChange` que lanza una excepción se registran en el
  log (`Radiante/addons`) y se saltan. Una opción que no se pudo listar sigue devolviendo su valor
  por defecto con `get()`.
- Si tu complemento también corre en un servidor dedicado, no llames a la API allí: Radiante no se
  carga en servidores.

Para que Radiante sea opcional en tu mod, deja las llamadas en una clase que solo cargues cuando
Radiante esté presente (por ejemplo tras `FabricLoader.getInstance().isModLoaded("radiante")`).
