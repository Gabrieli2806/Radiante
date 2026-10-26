[← Volver al índice](README.md)

# Problemas comunes

## Antes de nada: el log

El **Registro de Depuración** viene activado por defecto, y los mensajes del renderizador nativo
llegan a `logs/latest.log` como líneas `[native] ...` (los errores siempre, aunque lo apagues).
Para reportar un problema basta con ese archivo. Busca primero `[native]` y `Radiante`.

## El juego está en OpenGL

Radiante necesita Vulkan. Si el juego arranca en OpenGL, la pantalla "Radiante: se necesita Vulkan"
ofrece **Cambiar a Vulkan**; reinicia después. También puedes poner tú la API Gráfica en "Preferir
Vulkan" o "Predeterminado" en las opciones de vídeo.

Si Minecraft cambió a OpenGL solo tras un arranque fallido (o porque otro Minecraft se abría a la
vez), "Radiante: reinicia el juego" lo indica. Es la propia protección de Minecraft, y Radiante no
la guarda como tu elección: solo dura esa sesión. Reinicia, una instancia a la vez.

## "Radiante: no pudo iniciar"

El renderizador no arrancó esta vez. Suele pasar cuando se abren varias instancias de Minecraft a
la vez, o cuando falta memoria de vídeo. Cierra las otras instancias y reinicia; si se repite, el
log dice por qué.

## Mi GPU no soporta trazado de rayos

Pantalla "Radiante: trazado de rayos no disponible": la GPU o el driver no reportan
`VK_KHR_ray_tracing_pipeline`. Revisa los [requisitos](requisitos.md#gpu) y actualiza el driver.
También pasa con Vulkan por software (`llvmpipe`), típico en Linux cuando el juego no ve el driver
real: ver [Linux](linux.md). Puedes seguir sin trazado de rayos con el renderizador de Minecraft.

## Mundo negro

- **Linux, `FSR3 CreateContext` en el log**: un renderizador compilado sin los arreglos de Linux;
  ver [Linux](linux.md).
- Busca `[native]` en el log: el renderizador dice qué falló, y un error repetido cada fotograma
  aparece una vez con su número de repeticiones.

## DLSS o XeSS no aparecen en el selector de Pipeline

- **DLSS** solo se ofrece en GPUs NVIDIA. Si no está instalado aparece como `RT-DLSS (Instalar)`.
- **XeSS** solo existe en Windows. En GPUs que no son Intel puede instalarse y aun así no quedar
  disponible, si el driver no pasa la comprobación propia de XeSS.
- Si lo instalaste, falta reiniciar: aparece como `(Requiere reinicio)`.

Ver [Escaladores](escaladores.md).

## Instalé DLSS y sigo en FSR

El primer menú tras el reinicio pregunta si cambiar; si elegiste **Mantener el pipeline actual**,
elige **RT-DLSS** en el selector Pipeline.

## El botón de instalar pide salir del mundo

Instalar o borrar un escalador se hace desde los menús, y termina en un reinicio. Sal al menú
principal y abre los ajustes de Radiante desde Opciones → Ajustes de vídeo.

## La descarga falló

Normalmente es red: firewall, proxy, sin conexión. FSR sigue funcionando. Reintenta desde los
ajustes. Un archivo descargado que no coincide con su SHA-256 se descarta.

## No aparece Generación de Fotogramas o Reflex

- **Generación de Fotogramas** se oculta con **Salida HDR** activada.
- **Reflex** solo aparece en GPUs NVIDIA con `VK_NV_low_latency2` y `VK_KHR_present_id`.
  Actualiza el driver.

Ver [Generación de fotogramas y Reflex](frame-generacion-reflex.md).

## Activé HDR y no veo cambios

1. ¿Reiniciaste el juego? Salida HDR lo necesita.
2. ¿HDR está activado en el sistema para ese monitor, no solo soportado?
3. Si la descripción de "Salida HDR" dice que está activada pero todavía no en uso, el problema es
   el punto 2.

Ver [HDR](hdr.md).

## Linux: no carga el renderizador

`GLIBC_2.xx not found`, `UnsatisfiedLinkError`, `llvmpipe`: lista completa en [Linux](linux.md).

## Distant Horizons: terreno lejano raro

Ver [Distant Horizons](distant-horizons.md). Si algo falla, reduce la distancia de DH (no la de
Minecraft) y comprueba si sigue pasando.

## Va lento

Ver la [guía de rendimiento](rendimiento.md). Lo primero: el nivel de Calidad, y después el modo
del escalador y Rebotes de Luz.

## La imagen se ve mal tras cambiar ajustes

Mantén **Ctrl** y pulsa **Recompilar Shaders** (Restablecer valores se convierte en él), abajo a la
izquierda de los ajustes: aplica los cambios y vuelve a compilar los shaders.

## Quiero volver a los valores de un install nuevo

**Restablecer valores**, abajo a la izquierda de los ajustes. Devuelve todo, incluidos los ajustes
del pipeline sin control visible, y vuelve a elegir el perfil Bedrock RTX (Mejorado). El pipeline
vuelve a RT-DLSS si DLSS está cargado, a FSR si no. Ver [Ajustes](opciones.md).

## Nada de esto funcionó

Pregunta en [Discord](https://discord.gg/DhBbAzugZ9) o abre un
[issue](https://github.com/Gabrieli2806/Radiante/issues/new/choose) con tu `latest.log`, tu GPU,
tu sistema y el loader. Revisa antes los [issues abiertos](https://github.com/Gabrieli2806/Radiante/issues)
por si ya es conocido.
