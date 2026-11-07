[← Volver al índice](README.md)

# Solución de problemas

Busca tu problema abajo. Si ninguno encaja, mira [¿Sigue sin funcionar?](#sigue-sin-funcionar)

**Contenido:** [El log](#primero-el-log) · [Al arrancar](#al-arrancar) · [Escaladores](#escaladores) ·
[Generación de fotogramas, Reflex y HDR](#generación-de-fotogramas-reflex-y-hdr) · [Imagen y velocidad](#imagen-y-velocidad) ·
[Linux](#linux) · [¿Sigue sin funcionar?](#sigue-sin-funcionar)

## Primero: el log

Todo lo que dicen Radiante y su renderizador va a `logs/latest.log`, en líneas con `Radiante` y `[native]`. Busca
esas primero. Ese archivo basta para reportar un problema.

## Al arrancar

### El juego está en OpenGL

Radiante necesita Vulkan. La pantalla "Radiante: se necesita Vulkan" ofrece **Cambiar a Vulkan**; reinicia después.
También puedes poner la API gráfica en "Predeterminada" o "Preferir Vulkan" en los ajustes de vídeo.

Si Minecraft pasó a OpenGL por su cuenta tras un arranque fallido (o porque se abrieron dos instancias a la vez),
"Radiante: reinicia el juego" lo avisa. Solo dura esa sesión: reinicia, una instancia a la vez.

### "Radiante: no pudo iniciar"

El renderizador no arrancó esta vez, casi siempre porque se abrieron varias instancias de Minecraft a la vez o faltó
memoria de vídeo. Cierra las demás instancias y reinicia. Si se repite, el log dice por qué.

### Mi GPU no soporta trazado de rayos

"Radiante: trazado de rayos no disponible" significa que la GPU o el driver no anuncian
`VK_KHR_ray_tracing_pipeline`. Revisa los [requisitos](primeros-pasos.md#gpu) y actualiza el driver. En Linux también
pasa cuando el juego usa Vulkan por software (`llvmpipe`): ver [Linux](#linux). Puedes seguir jugando con el
renderizador de Minecraft.

### Mundo negro

Busca `[native]` en el log: el renderizador dice qué falló. Un error que se repite cada fotograma sale una vez, con
su número de repeticiones. En Linux, ver [Linux](#linux).

## Escaladores

| Problema | Causa y solución |
|---|---|
| DLSS no está en la lista de Pipeline | DLSS es solo de NVIDIA. Sin instalar, sale como `RT-DLSS (Instalar)`. |
| XeSS no aparece | XeSS es solo de Windows, y en GPUs que no son Intel el driver tiene que pasar la comprobación de XeSS. |
| Sale `(Requiere reinicio)` | Está instalado, pero se carga al arrancar: reinicia. |
| Instalé DLSS y sigo con FSR | Tras reiniciar elegiste **Mantener el pipeline actual**. Elige **RT-DLSS** en la lista de Pipeline. |
| El botón de instalar pide salir del mundo | Los escaladores se instalan desde los menús y terminan en un reinicio. Ve al menú principal y luego Opciones → Ajustes de vídeo → Radiante. |
| La descarga falló | Casi siempre un firewall, un proxy o no hay conexión. FSR sigue funcionando; reintenta desde los ajustes. |

Más en [Escaladores](escaladores.md).

## Generación de fotogramas, Reflex y HDR

- **No aparece Generación de Fotogramas:** se oculta mientras **Salida HDR** está activada.
- **No aparece Reflex:** necesita una GPU NVIDIA y un driver reciente (`VK_NV_low_latency2`).
- **HDR no cambia nada:**
  1. Reinicia el juego: Salida HDR solo se aplica al arrancar.
  2. Activa HDR en Windows para ese monitor (Configuración → Sistema → Pantalla → HDR). Que el monitor lo soporte no
     basta.
  3. Si tras reiniciar el ajuste dice "Activado, pero aún no funciona", el problema es el paso 2.

## Imagen y velocidad

- **Va lento:** ver [Calidad y rendimiento](calidad-y-rendimiento.md). Empieza por el nivel de Calidad, luego el modo
  del escalador y los Rebotes de Luz.
- **La imagen se ve mal tras cambiar ajustes:** mantén **Ctrl** y pulsa **Recompilar Shaders** (abajo a la izquierda
  en los ajustes) para aplicar y compilar los shaders de nuevo.
- **Quiero los ajustes de una instalación nueva:** **Restablecer valores**, abajo a la izquierda en los ajustes.
- **El terreno de Distant Horizons se ve raro:** baja la distancia de Distant Horizons y vuelve a mirar. Ver
  [Distant Horizons](distant-horizons.md).

## Linux

Radiante funciona en Linux x86-64 con el mismo jar que en Windows. Necesita:

- **glibc 2.35 o más nueva** (Ubuntu 22.04, Debian 12, Fedora 36, Arch...).
- **El driver Vulkan real de tu GPU:** el de NVIDIA, o Mesa (RADV, ANV) con trazado de rayos. `vulkaninfo --summary`
  tiene que mostrar tu GPU, no `llvmpipe`.
- **Java 25.**

DLSS funciona en Linux; XeSS no (no hay runtime para Linux).

### Launchers Flatpak

Los launchers instalados como Flatpak (Prism, Modrinth...) corren en un sandbox que tiene que ver tu driver. En
NVIDIA necesita la extensión GL que coincida **exactamente** con el driver del sistema:

```sh
cat /sys/module/nvidia/version                                       # p. ej. 595.91.07
flatpak install flathub org.freedesktop.Platform.GL.nvidia-595-91-07 # los puntos pasan a guiones
flatpak run --command=vulkaninfo <id-del-launcher> --summary         # comprobar dentro del sandbox
```

Repítelo tras cada actualización del driver. Los launchers que no son Flatpak no necesitan nada más.

### Qué dice el log

| Línea del log | Qué significa y qué hacer |
|---|---|
| `Using graphics device: llvmpipe` | No usa el driver de la GPU. Ver Flatpak arriba. |
| `GLIBC_2.xx not found` | La distribución es demasiado antigua. |
| `UnsatisfiedLinkError` | No se pudo cargar el renderizador. Borra `libprismrt.so` de la carpeta `radiante/` del juego y vuelve a abrirlo. |
| `FSR3 CreateContext failed` | Un renderizador compilado sin los arreglos de Linux. Usa una versión publicada. |

Wayland y X11 funcionan los dos. Si ves pantalla negra o parpadeo al cambiar de ventana, prueba el launcher en
X11/XWayland. Comprueba también que el launcher abre la instancia que crees: Modrinth en Flatpak y fuera de Flatpak
usan carpetas distintas, y el `gameDir` del log dice cuál se está ejecutando.

## ¿Sigue sin funcionar?

Pregunta en [Discord](https://discord.gg/DhBbAzugZ9) o abre un
[issue](https://github.com/Gabrieli2806/Radiante/issues/new/choose) con tu `latest.log`, GPU, sistema y loader. Mira
antes los [issues abiertos](https://github.com/Gabrieli2806/Radiante/issues).
