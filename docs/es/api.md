# Hacer mods para Radiante

Idiomas: **ES** · [EN](../en/api.md)

Radiante tiene una pequeña API pública para otros mods: todo lo que está en el paquete
`com.g2806.radiante.api`. El resto de Radiante es interno y puede cambiar en cualquier versión;
construye solo sobre este paquete.

| Clase | Para qué sirve |
|---|---|
| `RadianteApi` | Saber si el mundo se está trazando ahora, y dónde están el sol y la luna que dibuja Radiante |
| `RadianteGeometry` | Meter geometría propia (escombros, efectos, marcadores) en el mundo trazado |
| `RadianteAddons` | Poner las opciones de tu mod en los ajustes de Radiante, en **Complementos** ([detalles](complementos.md)) |

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

Las opciones de tu mod en los ajustes de Radiante, guardadas por Radiante y legibles desde cualquier
hilo:

```java
RadianteAddons.Addon addon = RadianteAddons.register("mymod", Component.translatable("mymod.name"));
RadianteAddons.Setting<Boolean> showRays = addon.toggle("show_rays",
    Component.translatable("mymod.show_rays"), Component.translatable("mymod.show_rays.tooltip"), false);

if (showRays.get()) { ... }
```

El título muestra el icono de los metadatos de tu mod (`addon.icon(...)` elige otro). La guía
completa, con listas de valores y deslizadores: [Opciones de complementos](complementos.md).

## Estabilidad

- Solo `com.g2806.radiante.api` es público. Los mixins a otras clases de Radiante, o las llamadas a
  ellas, pueden romperse en cualquier versión.
- La API sigue funcionando entre versiones; se añaden llamadas nuevas y los cambios se anotan en el
  changelog.
- Ideas para la API, o algo que tu mod necesita y falta:
  [GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).
