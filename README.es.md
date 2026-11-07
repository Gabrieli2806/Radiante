# Radiante

Renderizado con trazado de rayos para Minecraft 26.3 (Fabric, NeoForge y Forge), construido sobre el backend
Vulkan propio de Minecraft. Del mismo código salen versiones para 26.2 (Fabric, NeoForge y Forge) y para las
snapshots de 26.4 (Fabric).

[GitHub](https://github.com/Gabrieli2806/Radiante) · [Modrinth](https://modrinth.com/project/radiante) · [CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante) · [Discord](https://discord.gg/DhBbAzugZ9) · [Issues](https://github.com/Gabrieli2806/Radiante/issues) · [Documentación completa](docs/es/README.md)

Idiomas: [EN](README.md) · **ES**

![Radiante con DLSS Quality: bosque y río con sol](docs/images/scenes/hero.webp)

Radiante es un renderizador con trazado de rayos independiente para Minecraft. Su renderizador nativo, PrismRT,
funciona sobre el backend Vulkan "render pearl" (`com.mojang.renderpearl`) de Minecraft. En lugar de crear su propio
dispositivo Vulkan junto al del juego, comparte el que Minecraft ya creó, así que el trazador de rayos y la GUI
vanilla dibujan en el mismo frame.

**Ray tracing apagado y encendido.** La misma vista con la iluminación vanilla y con Radiante.

| Apagado (vanilla) | Encendido (DLSS Quality) |
|---|---|
| ![Apagado (vanilla)](docs/images/compare/raytracing-off.webp) | ![Encendido (DLSS Quality)](docs/images/compare/raytracing-on.webp) |

## Características

- Trazado de rayos por hardware (`VK_KHR_ray_tracing_pipeline`) para el terreno, con iluminación directa
  path-traced, sombras e iluminación global, en Windows y Linux.
- Cielo, sol y dispersión atmosférica físicamente basados.
- Un shader pack incluido, `radiante-pt`, con muestreo directo de luces de bloque (antorchas, lava, lámparas) y su
  reutilización con ReSTIR, niebla volumétrica por froxels o por ray marching, nubes volumétricas con sombras
  opcionales, suelo mojado y vidrio sin juntas.
- Ambiente de Bioma: cada bioma del Overworld, el Nether y el End tiene su propio aire, y cada tipo de agua su
  propio aspecto, de forma sutil y fundiéndose al cruzar fronteras. Los biomas de mods y data packs obtienen el
  suyo a partir de sus etiquetas, clima y colores, sin configurar nada.
- Soporte LabPBR (mapas `_s`/`_n`) para bloques, atlases y entidades/ítems en mano, con emisión por entidad
  (marcos de ítem brillantes, cristales de END, mobs autoluminosos, ítems brillantes en el suelo). Se recomienda usar
  un resource pack personalizado con mapas PBR (o un `.mcpack` de Bedrock) para aprovechar al máximo estas funciones.
- Escalado y generación de frames con FSR incluidos; NVIDIA DLSS (escalado, Ray Reconstruction y generación de
  frames hasta 4x en RTX 50) e Intel XeSS se descargan dentro del juego cuando se piden. Denoising NRD para los
  pipelines que no son DLSS.
- NVIDIA Reflex mediante `VK_NV_low_latency2`, sin reiniciar.
- Motion blur y profundidad de campo, ambos activables/desactivables. Salida HDR en pantallas HDR.
- Soporte de resource packs `.mcpack` de Bedrock (materiales leídos de los mapas MER del propio pack, niebla y
  agua por bioma), detectados directamente en la lista de packs.
- El aspecto Bedrock RTX por defecto: el cielo, la exposición, la curva de tonos, el vidrio, el agua y la niebla de
  Bedrock RTX son los valores por defecto de radiante-pt, con algunos extras como portales del End que brillan. Todos
  los ajustes se pueden cambiar.
- Presets: guarda un aspecto, cambia entre aspectos con un clic, comparte uno como código.
- Una pantalla de ajustes con búsqueda, navegación con teclado y una vista Avanzada; los cambios se compilan en
  segundo plano con un aviso de progreso, y tras una actualización los shaders se compilan en el menú principal
  antes del primer mundo.
- Soporte de [Distant Horizons](https://modrinth.com/mod/distanthorizons): su terreno lejano se traza con path
  tracing junto con el resto del mundo, con un color por cara de bloque, y se oculta columna por columna donde el
  terreno cercano ya está construido (opcional; sin el mod no cambia nada). El costo por frame crece con la
  distancia de render de DH.
- Funciona sobre el dispositivo que Minecraft crea: sin segunda instancia de Vulkan, sin swapchain duplicado.
- Un jar de ~15 MB por loader trae ambos renderizadores; se descomprimen en la carpeta del juego al primer inicio.

## Requisitos

- Windows x64, o Linux x86-64 con glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 o más recientes).
- Una GPU con soporte de ray tracing en Vulkan (`VK_KHR_ray_tracing_pipeline` y
  `VK_KHR_acceleration_structure`).
- Minecraft 26.3 con uno de:
  - Fabric Loader 0.19.5+ y Fabric API (compilado contra 0.162.0+26.3),
  - NeoForge 26.3.0.10-beta+,
  - Forge 26.3-66.0.3+.
- Minecraft 26.2 (rama `mc/26.2`): Fabric Loader 0.19.5+ con Fabric API, NeoForge 26.2.0.93+ o Forge
  26.2-65.1.5+.
- Snapshots de Minecraft 26.4 (rama `mc/26.4`): solo Fabric, Fabric Loader 0.20.1+ con Fabric API.
- FSR (escalado y generación de frames) viene incluido. NVIDIA DLSS (~115 MB) e Intel XeSS (~73 MB, solo Windows)
  se descargan dentro del juego cuando se piden, desde los repositorios oficiales de NVIDIA e Intel, y se usan desde
  el siguiente inicio; el primer menú ofrece DLSS en GPUs NVIDIA y XeSS en Intel, y los ajustes de Radiante permiten
  descargarlos o borrarlos.
- NVIDIA Reflex requiere una GPU NVIDIA con un driver que tenga `VK_NV_low_latency2` (Windows y Linux); en otras
  GPUs la opción no aparece.
- Java 25.

El registro de depuración viene activado por defecto: los diagnósticos de Radiante y los mensajes del propio
renderizador nativo (con el prefijo `[native]`, los errores siempre) van a `latest.log`, así que ese archivo basta para
reportar un problema. Se puede desactivar en los ajustes de Radiante, en Otros.

Documentación completa para jugadores (cada ajuste, escaladores, Linux, problemas comunes): [docs/es](docs/es/README.md).

### Notas para Linux

- El juego debe usar el driver real de la GPU. Si el log muestra `Using graphics device: llvmpipe`, Vulkan cayó al
  renderizador por software de Mesa: el mundo se ve negro y no aparece DLSS.
- **Launchers en Flatpak** (Modrinth, Prism, etc.) necesitan el runtime GL de Flatpak que coincida exactamente con
  el driver NVIDIA del sistema. Mira la versión con `cat /sys/module/nvidia/version` (p. ej. `595.91.07`) e instala
  `flatpak install flathub org.freedesktop.Platform.GL.nvidia-595-91-07` (los puntos pasan a guiones). Repítelo
  tras cada actualización del driver. Los launchers que no son Flatpak usan el driver del sistema directamente.
- En Wayland con NVIDIA el backend OpenGL de Minecraft suele fallar (`EGL_BAD_DISPLAY`); Radiante siempre deja
  Vulkan como respaldo.

#### Mundo negro o con glitches en Linux

Revisa `logs/launcher_log.txt` (o `latest.log`) de la instancia que realmente abres, en este orden:

1. **`Using graphics device: llvmpipe`** - no usa el driver de la GPU, ver la nota de Flatpak arriba.
2. **`version 'GLIBC_2.xx' not found`** - la glibc del sistema es más vieja que la usada al compilar `libprismrt.so`.
   Las versiones publicadas necesitan glibc 2.35+; un `libprismrt.so` compilado en una distro más nueva debe
   conservar el anclaje de símbolos de `native/glibc_compat.h` y `native/src/prism/glibc_shims.cpp` (compruébalo con
   `objdump -T libprismrt.so | grep -o 'GLIBC_2\.[0-9]*' | sort -Vu | tail -1`).
3. **`UnsatisfiedLinkError: ... RendererProxy.<método>`** - el lado Java es más nuevo que la librería nativa. Cualquier
   cambio en `native/` (también hecho desde Windows) requiere volver a ejecutar `native/build-linux.sh` antes de empaquetar.
4. **`FSR3: CreateContext failed`** o **`Failed to load shared shader pack`** - problema de compilación de FidelityFX o
   minizip; hay que recompilar la librería nativa desde este repositorio, que incluye los arreglos de Linux de abajo.
5. El launcher puede abrir otra instancia distinta a la que copiaste el jar (Modrinth Flatpak y no Flatpak usan
   carpetas distintas: `~/.var/app/com.modrinth.ModrinthApp/data/ModrinthApp/profiles` y
   `~/.local/share/ModrinthApp/profiles`). El `gameDir` del log indica cuál se está ejecutando.

Arreglos específicos de Linux en el código de terceros incluido, que hay que conservar al actualizarlo:

- **minizip-ng** se compila con zlib-ng descargado y enlazado estáticamente (`native/CMakeLists.txt`). Sin zlib pierde
  en silencio el soporte de deflate, el paquete de shaders integrado no se puede extraer y el mundo se ve negro.
- **FidelityFX SDK**: `wchar_t` ocupa 4 bytes en Linux (2 en Windows). `FFX_RESOURCE_NAME_SIZE` se mantiene en 64 en
  todas las plataformas; los tamaños de contexto opacos (`FFX_SDK_DEFAULT_CONTEXT_SIZE`, `FFX_DENOISER_CONTEXT_SIZE`)
  se duplican fuera de Windows, y los `static_assert` que comprueban que los contextos privados caben están activos en
  todas partes. Acortar los nombres (un intento anterior) truncaba los nombres de binding de los shaders: el frame
  generation de FSR daba frames negros o corruptos y el upscaler FSR no se creaba, lo que dejaba negro el pipeline
  `RT-FSR`. En los `swprintf` anchos se usa `%ls`, nunca `%s`.
- `libprismrt.so` solo exporta las funciones JNI (`native/src/prism/exports.map`) y mantiene privado su libstdc++ estático;
  si no, choca con el libstdc++ del sistema ya cargado en el juego y crashea al arrancar.

## Compilación

```sh
native/build-windows.bat   # el renderizador nativo (Linux: native/build-linux.sh)
./gradlew build            # un jar por loader en fabric/, neoforge/ y forge/ build/libs
```

La compilación nativa necesita el Vulkan SDK y las librerías del DLSS SDK de NVIDIA, y Gradle no la rehace: vuelve a
ejecutarla tras cualquier cambio en `native/` y al cambiar de rama. Requisitos, Linux, CI y publicación de versiones
están en [docs/es/compilar.md](docs/es/compilar.md); el flujo de desarrollo, en [CONTRIBUTING.md](CONTRIBUTING.md)
(en inglés).

## Cómo funciona

Una explicación técnica detallada del renderizador nativo está en [docs/prismrt.md](docs/prismrt.md) (en inglés).

- La creación de `VulkanInstance` / `VulkanDevice` se redirige a los puntos de entrada nativos `createMerged`,
  que añaden las extensiones de ray tracing, descriptor indexing y upscalers a lo que Minecraft haya pedido,
  y fusionan las cadenas de features.
- El acceso a la cola gráfica se serializa entre Minecraft y el renderizador mediante hooks instalados sobre
  los punteros de función de volk.
- Cada frame el renderizador graba sus command buffers de subida / mundo / composición y se los entrega a
  `VulkanCommandEncoder.execute` de Minecraft, luego espera en el timeline semaphore de Minecraft.
- El terreno se compila al formato de vértices PBR propio del renderizador desde `ModelBlockRenderer` /
  `FluidRenderer`, siguiendo el almacenamiento por secciones de Minecraft.
- Los atlases se cosen en la GPU en 26.3, así que la copia muestreada se reconstruye desde las imágenes de
  sprite, respetando el padding por sprite del stitcher.

## Cómo contribuir

Se agradecen reportes de bugs, traducciones, shader packs y pull requests. [CONTRIBUTING.md](CONTRIBUTING.md) (en
inglés) explica cómo está organizado el proyecto, cómo compilarlo, ejecutarlo y probarlo, las convenciones del código,
las ramas por versión de Minecraft y lo que necesita un pull request. Los bugs e ideas están en
[GitHub Issues](https://github.com/Gabrieli2806/Radiante/issues).

## Licencia

GPL-3.0. Radiante comenzó como un fork de Radiance y su renderer MCVR. Desde entonces se ha rehecho buena parte, y el proyecto sigue alejándose de ellos.

Radiante no está afiliado, respaldado ni patrocinado por Mojang, Microsoft ni NVIDIA. Minecraft, Minecraft Bedrock,
Bedrock RTX y NVIDIA son marcas de sus dueños; sus nombres se usan solo para describir la compatibilidad con sus
resource packs y el aspecto al que aspiran los valores por defecto de radiante-pt.
