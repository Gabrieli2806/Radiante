# Hacer mods para Radiante

Idiomas: **ES** · [EN](../en/api.md)

Radiante tiene una pequeña API pública para otros mods: todo lo que está en el paquete
`com.g2806.radiante.api`. El resto de Radiante es interno y puede cambiar en cualquier versión;
construye solo sobre este paquete.

| Clase | Para qué sirve |
|---|---|
| `RadianteApi` | Saber si el mundo se está trazando ahora, y dónde están el sol y la luna que dibuja Radiante |
| `RadianteGeometry` | Meter geometría propia (escombros, efectos, marcadores) en el mundo trazado |
| `RadianteAddons` | Poner las opciones de tu mod en los ajustes de Radiante, en **Complementos** ([abajo](#radianteaddons)) |

La API es Java normal, sin puntos de entrada de un loader concreto, así que las mismas llamadas
funcionan en Fabric, NeoForge y Forge.

## Radiante es solo de cliente

Radiante solo carga en el cliente. Un mod que depende de él también es de cliente (en Fabric:
`"environment": "client"`). En un solo jugador el servidor integrado corre dentro del cliente, así
que el código de servidor de tu mod (IA de mobs, reglas de juego) puede leer Radiante ahí; en un
servidor dedicado Radiante no está cargado. Si tu mod también debe funcionar en servidores, deja
todas las llamadas a la API en una clase que solo cargues cuando Radiante esté presente (por ejemplo
tras `FabricLoader.getInstance().isModLoaded("radiante")`) y haz que Radiante sea opcional.

## Preparar el proyecto

Descarga el jar de Radiante para tu versión de Minecraft y tu loader desde las
[releases de GitHub](https://github.com/Gabrieli2806/Radiante/releases) (por ejemplo
`radiante-fabric-1.1.0+26.3.jar`), ponlo en una carpeta `libs/` de tu proyecto y añádelo en
`build.gradle`:

```gradle
dependencies {
    // Compila contra Radiante y lo carga en el cliente de desarrollo; los jugadores instalan Radiante aparte.
    implementation files("libs/radiante-fabric-1.1.0+26.3.jar")
}
```

Usa `compileOnly` si Radiante es opcional para tu mod y no lo quieres en tu cliente de desarrollo.
No metas Radiante dentro de tu jar.

Después declara la dependencia, para que un jugador sin Radiante reciba un mensaje claro en vez de un
crash:

```json
// fabric.mod.json
"depends": {
  "radiante": ">=1.1.0"
}
```

En NeoForge y Forge, añade una entrada `[[dependencies.tumod]]` con `modId="radiante"` en tu
`neoforge.mods.toml` o `mods.toml`. Pide la primera versión de Radiante que tenga todas las llamadas
que usas: `RadianteAddons` es posterior a la 1.1.0 (mira el changelog).

## RadianteApi

```java
import com.g2806.radiante.api.RadianteApi;

if (RadianteApi.isRayTracingEnabled()) {
    // Radiante está dibujando el mundo; los pases de nivel de Minecraft no se ejecutan.
}

// El sol como lo dibuja Radiante, siguiendo los ajustes de trayectoria del jugador (inclinación y giro).
float sunAngle = level.environmentAttributes().getDimensionValue(EnvironmentAttributes.SUN_ANGLE);
Vec3 toSun = RadianteApi.sunDirection(sunAngle * (float) (Math.PI / 180.0));
Vec3 toMoon = RadianteApi.moonDirection(sunAngle * (float) (Math.PI / 180.0));
```

- `isRayTracingEnabled()`: verdadero mientras Radiante traza el mundo. Lo que se dibuja con el render
  de nivel de Minecraft no aparece entonces; para eso usa `RadianteGeometry`.
- `sunDirection(ángulo)` y `moonDirection(ángulo)`: vectores unitarios desde el mundo hacia el sol y
  la luna, en coordenadas del mundo. El ángulo es el `SUN_ANGLE` de Minecraft en radianes. El sol se
  puede inclinar y girar en los ajustes de Radiante, así que no lo calcules tú: este es el sol que
  ilumina el mundo y proyecta las sombras que ve el jugador.
- `celestialTransform(ángulo)` y `moonTransform(ángulo)`: lo mismo como matriz de rotación, para
  dibujar cosas que se mueven con el cielo.

*Radiante Death Light* usa `sunDirection` para que los no-muertos solo ardan donde les llega el sol
trazado.

## RadianteGeometry

Mientras Radiante traza el mundo, los pases de nivel de Minecraft no se ejecutan, así que lo que un
mod dibuja ahí por su cuenta nunca llega a la pantalla. Las entidades, las entidades de bloque y las
partículas ya se trazan. Para lo demás hay dos entradas, que se llaman en cada frame en el hilo de
render mientras se traza el mundo.

**Un provider** escribe quads con una textura:

```java
RadianteGeometry.register("mymod:debris", sink -> {
    Vec3 camera = sink.camera();
    VertexConsumer quads = sink.quads(Identifier.parse("minecraft:textures/atlas/blocks.png"),
        RadianteGeometry.Alpha.CUTOUT);
    for (Debris piece : MyMod.debris()) {
        // Cuatro vértices forman un quad; las posiciones son relativas a la cámara.
        piece.emit(quads, camera, sink.partialTick());
    }
});
```

**Un submitter** es para un mod que ya envía modelos, ítems o texto al `SubmitNodeCollector` de
Minecraft: las mismas llamadas, hechas sobre el collector que recibe, se trazan.

```java
RadianteGeometry.registerSubmitter("mymod:holograms", collector -> MyMod.submitHolograms(collector),
    false); // false: no proyecta sombras (luz, marcadores, etiquetas)
```

- `Alpha`: `OPAQUE` (lo más barato), `CUTOUT` (cada texel está o no), `TRANSLUCENT` (como vidrio) o
  `DITHERED` (un fantasma difuminado, para previsualizaciones y guías).
- `register(nombre, provider, castsShadows)`: pasa `false` para previsualizaciones, guías y marcadores.
- Registrar otra vez con el mismo nombre reemplaza el anterior; `unregister(nombre)` lo quita.

## RadianteAddons

Un mod hecho sobre Radiante (un *complemento*) puede poner sus opciones en la pantalla de ajustes de
Radiante, en vez de escribir su propia pantalla y su propio archivo de configuración. Cuando al menos
un complemento se registra, los ajustes ganan una sección **Complementos** al final de la barra
lateral, con un título por complemento (su icono, nombre y versión) y sus opciones debajo. Sin complementos
no cambia nada. Al pulsar un título (o Intro, Espacio o la A del mando sobre él) se pliegan o despliegan
las opciones de ese complemento: con uno solo empiezan desplegadas, con dos o más plegadas, y Radiante
recuerda lo que elegiste para cada uno.

Las opciones de un complemento se comportan como las de Radiante: esperan a **Aplicar** o **Listo**,
**Deshacer** las descarta, **Restablecer valores** les devuelve los valores por defecto del
complemento, la búsqueda las encuentra (o todas las de un complemento por su nombre), el panel de
descripción muestra su texto de ayuda, y el teclado y el mando se mueven por ellas.

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

## Estabilidad

- Solo `com.g2806.radiante.api` es público. Los mixins a otras clases de Radiante, o las llamadas a
  ellas, pueden romperse en cualquier versión.
- La API sigue funcionando entre versiones; se añaden llamadas nuevas y los cambios se anotan en el
  changelog.
- Ideas para la API, o algo que tu mod necesita y falta:
  [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).
