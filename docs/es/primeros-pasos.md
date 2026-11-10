[← Volver al índice](README.md)

# Primeros pasos

Qué necesitas, cómo instalar Radiante y qué pasa la primera vez que se abre.

## Requisitos

| | Windows | Linux |
|---|---|---|
| Sistema | Windows 10 u 11, x64 | x86-64 con glibc 2.35+ (Ubuntu 22.04, Debian 12, Fedora 36 o más nuevo) |
| GPU | Trazado de rayos por hardware en Vulkan (ver abajo) | Lo mismo, con el driver real de la GPU (no `llvmpipe`) |
| Java | 25 | 25 |

El mismo jar sirve en los dos sistemas. macOS, Windows en ARM, Java de 32 bits y los launchers de Android no están
soportados: ahí el juego arranca con el renderizador de Minecraft y lo avisa una vez.

### GPU

Hace falta trazado de rayos en Vulkan (`VK_KHR_ray_tracing_pipeline` y `VK_KHR_acceleration_structure`). No hay modo
por software.

| | NVIDIA | AMD | Intel |
|---|---|---|---|
| Tarjetas | RTX 20 o más nueva | RX 6000 o más nueva | Arc |
| FSR (escalado, generación de fotogramas) | Incluido | Incluido | Incluido |
| DLSS (escalado, Reconstrucción de Rayos, generación de fotogramas) | Se descarga en el juego | — | — |
| XeSS (solo Windows) | Si el driver pasa la comprobación de XeSS | Igual | Recomendado |
| NVIDIA Reflex | Con un driver reciente | — | — |

NVIDIA es el objetivo principal: DLSS con Reconstrucción de Rayos da la imagen más limpia. AMD e Intel funcionan,
pero están menos probados.

### Minecraft y loader

Elige el jar de tu versión de Minecraft y tu loader. Todos salen del mismo código y se ven igual.

| Minecraft | Fabric | NeoForge | Forge |
|---|---|---|---|
| **26.3** | Loader 0.19.5+ y Fabric API (compilado con 0.165.2) | 26.3.0.101+ | 26.3-66.0.3+ |
| 26.2 | Loader 0.19.5+ y Fabric API | 26.2.0.93+ | 26.2-65.1.5+ |
| Snapshots de 26.4 | Loader 0.20.1+ y Fabric API | — | — |

Fabric es el más probado. NeoForge y Forge ejecutan el mismo código, generación de fotogramas y Reflex incluidos.

### Recomendado

- Un resource pack con mapas LabPBR (`_n`/`_s`) o un `.mcpack` de Bedrock RTX: ver [Bedrock RTX](bedrock-rtx.md).
  Sin uno, Radiante usa sus propios mapas para 159 texturas vanilla.
- [Distant Horizons](https://modrinth.com/mod/distanthorizons), para trazar también el terreno lejano: ver
  [Distant Horizons](distant-horizons.md).
- Una pantalla HDR: ver [HDR](hdr.md).

## Instalar

Radiante es solo de cliente: los servidores no lo necesitan. Descárgalo de
[Modrinth](https://modrinth.com/project/radiante), [CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante)
o [GitHub](https://github.com/Gabrieli2806/Radiante/releases).

1. Instala el loader: [Fabric](https://fabricmc.net/use/) (y pon **Fabric API** en `mods`),
   [NeoForge](https://neoforged.net/) o [Forge](https://files.minecraftforge.net/).
2. Pon el jar de Radiante de ese loader (`radiante-fabric-…`, `radiante-neoforge-…` o `radiante-forge-…`) en
   `mods`.
3. Abre el juego con ese perfil.

> [!NOTE]
> En Linux con un launcher Flatpak (Prism, Modrinth...), lee primero [Linux](solucion-de-problemas.md#linux): el
> sandbox tiene que ver el driver de tu GPU.

## La primera vez

1. **Se desempaqueta el renderizador.** Radiante desempaqueta su librería nativa en la carpeta `radiante/` del juego
   y la comprueba. Solo se repite tras una actualización.
2. **Los shaders se compilan en segundo plano.** Un aviso arriba a la derecha muestra el progreso, unos segundos
   después del menú principal. Solo vuelve a pasar tras una actualización o un driver nuevo. Puedes usar los menús
   mientras tanto.
3. **Se ofrece DLSS o XeSS.** FSR viene incluido. En NVIDIA una ventana ofrece DLSS (~115 MB); en Intel (Windows),
   XeSS (~73 MB). Descárgalo, déjalo para luego o que no vuelva a preguntar. Ver [Escaladores](escaladores.md).
4. **Entra en un mundo.** Radiante se activa con el aspecto de Bedrock RTX.

Si en vez de eso aparece otra pantalla:

| Pantalla | Qué hacer |
|---|---|
| "Radiante: se necesita Vulkan" | El juego está en OpenGL. Pulsa **Cambiar a Vulkan** y reinicia. |
| "Radiante: trazado de rayos no disponible" | La GPU o el driver no tienen trazado de rayos, o el juego usa Vulkan por software. Ver [Solución de problemas](solucion-de-problemas.md#mi-gpu-no-soporta-trazado-de-rayos). |

<!-- Captura pendiente: la pantalla completa "Radiante: trazado de rayos no disponible" -->

## Teclas

| Tecla | Acción |
|---|---|
| `F6` | Abre los ajustes de Radiante (también: Opciones → Ajustes de vídeo → Radiante). |
| Alternar Trazado de Rayos (sin asignar) | Activa o desactiva el trazado de rayos al instante. Asígnale una tecla en los controles de Minecraft. |

## Siguiente

- [Ajustes](opciones.md): cómo funciona la pantalla de ajustes.
- [Calidad y rendimiento](calidad-y-rendimiento.md): saca el máximo de FPS a tu GPU.
- [Solución de problemas](solucion-de-problemas.md): si algo no arranca o se ve mal.
