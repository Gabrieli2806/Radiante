# Hacer un paquete de shaders

Idiomas: **ES** · [EN](../en/shader-packs.md)

Un paquete de shaders es el path tracer que usa Radiante: un zip con shaders GLSL y un `configs.json`. Radiante trae uno,
Radiante PT; los jugadores añaden otros en `radiante/shaderpacks/` y los eligen en **Paquetes de shaders** dentro de los
ajustes de Radiante ([opciones](opciones.md#paquetes-de-shaders)).

Esta página es para quien hace paquetes, tal como está hoy. El soporte de paquetes es nuevo: la interfaz sobre la que se
construyen todavía no está fijada, así que un paquete puede necesitar cambios tras una versión de Radiante. Lo planeado,
y cómo los paquetes seguirán funcionando entre versiones, está en el [diseño de los paquetes de shaders](../dev/shader-packs/README.md)
(en inglés).

## Empieza desde un ejemplo

Un paquete se construye sobre Radiante PT y solo contiene lo que cambia. Hay dos ejemplos en el repositorio,
[`examples/shaderpacks/`](../../examples/shaderpacks/): `radiante-example` (un gradeo de color, luz a través de las
hojas, tonos del agua y del cielo) e `inkwell` (aspecto de cómic: luz en bandas, colores de póster, contornos de
tinta).

1. Clona el repositorio y copia uno de ellos a una carpeta nueva a su lado.
2. En `configs.json`, dentro de `"radiante"`, cambia `display_name`, `version`, `author`, `license`, `description` e
   `icon`. Deja `"extends": "radiante-pt@1"`.
3. Cambia los archivos de hooks, añade ajustes, pases o shaders; quédate solo con los archivos que cambies.
4. `./gradlew shaderPackExamples` lo comprime en `build/shaderpacks/<carpeta>.zip`.
5. Copia el zip a `radiante/shaderpacks/` y elígelo en **Paquetes de shaders**.

## Construir sobre Radiante PT: `extends`

Con `"extends": "radiante-pt"` (o `"radiante-pt@1"`, la versión de los hooks de Radiante PT para la que está hecho) en
el manifiesto, Radiante pone Radiante PT debajo del paquete al cargarlo:

- **Archivos**: los de Radiante PT y encima los tuyos. Un archivo en la misma ruta sustituye al de Radiante PT.
- **`configs.json`**: se combina. Atributos, texturas, buffers y pases se emparejan por `name`: los tuyos se añaden, y
  una entrada con el nombre de una de Radiante PT solo cambia los campos que des (`{"name": "...", "default_value":
  "..."}` cambia un valor por defecto). Un pase nuevo se ejecuta al final de su etapa salvo que diga `"before"` o
  `"after"` otro pase. Las variables del script (`global_variables`) se emparejan por nombre; los `commands` que des
  sustituyen a los de Radiante PT, y un comando `{"base": true}` entre ellos ejecuta ahí los de Radiante PT.
- **`lang/*.lang`**: los nombres de Radiante PT y después los tuyos.

Las reglas exactas están en el [diseño, sección 5](../dev/shader-packs/design.md#5-inheritance-and-hook-points) (en
inglés).

## Hooks

Radiante PT llama a una pequeña función en los sitios que un paquete suele querer cambiar. Cada una es un archivo en
`hooks/` que en Radiante PT no hace nada; pon tu propio archivo en la misma ruta:

| Archivo | Función | Qué puedes hacer ahí |
| --- | --- | --- |
| `hooks/surface.glsl` | `hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce)` | Cambiar el color, la rugosidad, la normal o el brillo de una superficie tras leer sus texturas. `isLeavesSurface(surfaceFlags)` y las demás pruebas de `util/vertex.glsl` dicen qué se golpeó `hookTexelCentre(surface.uv, surface.worldPos)` da el centro del texel golpeado; mover `surface.worldPos` ahí y ajustar `g_hookViewDir` ilumina y refleja por texel (mira el ejemplo Pixel Light) |
| `hooks/direct_light.glsl` | `hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF)` | Cambiar cómo iluminan el sol y la luna una superficie (bandas de cel, luz a través de las hojas cuando `NoL < 0`) |
| `hooks/water.glsl` | `hookWaterAbsorption(inout vec3 extinction)` | Color del agua: lo rápido que se absorbe cada color |
| `hooks/sky.glsl` | `hookSky(inout vec3 radiance, vec3 direction)` | Color del cielo; también tiñe la luz que da el cielo |
| `hooks/clouds.glsl` | `hookCloudLayer(vec3 direction)` | Una capa de nubes sobre el cielo: devuelve su luz (rgb) y cuánto cielo deja ver (a). Se dibuja tras la lluvia y por encima del sol y la luna |
| `hooks/cloud_shadow.glsl` | `hookCloudShadow(vec3 worldPos, vec3 lightDir)` | Cuánta luz del sol o la luna dejan pasar tus nubes hasta una superficie (de 0 a 1) |
| `hooks/stars.glsl` | `hookStars(inout vec3 radiance, vec3 direction)` | Estrellas o auroras en el cielo que ve la cámara; devuelve 0 para ocultar las estrellas de Radiante, 1 para mantenerlas |
| `hooks/fog.glsl` | `hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction)` | Bruma y niebla con la luz volumétrica activada |
| `hooks/post_grade.glsl` | `hookPostGrade(inout vec3 color, vec2 uv)` | Gradeo de color de la imagen final. Se activa con `{"RADIANTE_HOOK_POST_GRADE": true}` en `execution_post.global_variables` |

Mantén la guarda `#ifndef`/`#define` del archivo. Los ajustes de tu paquete son defines de GLSL, usables en cualquier
hook. Un archivo de hook sigue solo las actualizaciones de Radiante PT, cosa que un archivo de sombreado copiado no
hace.

### Clima y bioma

`#include "common/pack_world.glsl"` en un hook te da el tiempo y el bioma donde está la cámara, en todos los hooks,
también en `post_grade.glsl`:

| Función | Valor |
| --- | --- |
| `radianteRain()`, `radianteThunder()` | Lluvia y tormenta, de 0 a 1 (`rainStrength` y `thunderStrength` de Iris) |
| `radianteBiomeTemperature()`, `radianteBiomeDownfall()` | La temperatura y la humedad (downfall) del bioma |
| `radianteBiomeSnowy()`, `radianteBiomeDry()`, `radianteBiomeRainy()` | De 0 a 1: aquí nieva, aquí no cae nada, aquí llueve |
| `radianteBiomePrecipitation()` | 0 nada, 1 lluvia, 2 nieve (`biome_precipitation` de Iris) |
| `radianteBiomeIs(RADIANTE_BIOME_PALE_GARDEN)` | De 0 a 1: cuánto está la cámara en ese bioma |

Los valores del bioma cambian en unos dos segundos cuando la cámara pasa a otro bioma, así que un efecto que dependa
de ellos también se funde. Hay un `RADIANTE_BIOME_<NOMBRE>` para cada bioma vanilla; para uno de un mod, la cabecera
explica cómo calcular su valor a partir de su id. Por ejemplo, una aurora solo donde nieva:
`strength *= radianteBiomeSnowy();` (el port de Mellow lo hace así).

### Ajustes en vivo, bloom y efectos de lente en el gradeo

Un atributo con `"runtime": true` cambia sin recompilar, tanto en los shaders de ray tracing como en los pases de
post: su define lee el uniform del mundo (`worldUBO.packParams`, 256 huecos; Radiante PT usa unos 60). Como el valor
se lee mientras corre el shader, se prueba con `if`, no con `#if`: `if (int(MY_PALETTE) > 1) { ... }`. La paleta y el
tramado de Pixel Light y el gradeo del port de Mellow funcionan así.

`hooks/post_grade.glsl` ve `common/pack_post.glsl`, que los pases de post propios de un paquete también pueden
incluir. Declara `worldUBO` y `skyUBO` (define antes `RADIANTE_POST_UNIFORMS_DECLARED` si tu pase ya los declara) y el
bloom:

| Función | Valor |
| --- | --- |
| `radianteBloom(uv)` | La imagen antes del tone mapping, desenfocada: la pirámide de bloom de Radiante, a la mitad del tamaño de la pantalla, en la escala expuesta (1 es más o menos blanco) |
| `radianteBloomDisplay(uv, strength)` | Lo mismo con tone mapping y codificado para la pantalla, para sumarlo al color del gradeo |
| `radianteAddBloom(color, uv, strength)` | `color` con el bloom sumado como luz; entre 0.05 y 0.3 es un resplandor normal |

La pirámide solo se dibuja para un paquete que la pide: `{"RADIANTE_POST_BLOOM": true}` en
`execution_post.global_variables`, junto a `RADIANTE_HOOK_POST_GRADE` (si no, `radianteBloom` es negro; con el ajuste
Bloom de Radiante activado, la imagen ya lo lleva). Su umbral y su extensión son los ajustes de bloom de Radiante.
Léela en otros puntos para efectos de lente: reflejada por el centro para destellos, estirada para rayas. Para la
aberración cromática, lee `hookPostGradeInput` (la imagen) con un `uv` un poco distinto por canal. El paquete de
ejemplo hace las tres cosas, cada intensidad como ajuste en vivo.

## Trabajar en un paquete dentro del juego

Pon el paquete en `radiante/shaderpacks/` como carpeta, no como zip, y elígelo.

- **Recarga en caliente**: guarda un archivo del paquete y el juego lo recoge en unos dos segundos. Primero se
  comprueba (se carga y se compila en segundo plano mientras sigues jugando); si compila, la pipeline se vuelve a
  construir con él, igual que al aplicar un ajuste. Si no compila, se queda la imagen que tenías, un aviso muestra el
  primer error, y el informe completo está en la pantalla de paquetes de shaders (**Copiar error**) y en el registro
  del juego.
- `/radiante pack reload` hace lo mismo al momento; `/radiante pack validate [nombre]` comprueba cualquier paquete de
  la carpeta (el activo si no das nombre) sin cambiar a él, y escribe el informe en el chat.
- **Vistas de depuración**: `/radiante debug view <vista>` muestra una de las imágenes del fotograma en lugar de la
  imagen final: `albedo`, `normal`, `roughness`, `metallic`, `emission`, `depth`, `motion` (vectores de movimiento) o
  `flags` (el tipo de superficie: agua, lava, cristal, hojas, plantas...). `/radiante debug view off` vuelve a la
  normal; sin vista, las lista. La vista no se guarda.
- Tus propias vistas: lístalas en `configs.json`,
  `"debug_views": [{"name": "wetness", "define": "MY_DEBUG_WET"}]`. El define vale 0, y 1 (o `"value"`) mientras esa
  vista está elegida, así que `#if MY_DEBUG_WET` en cualquier shader muestra lo que quieras, por ejemplo en
  `hookPostGrade`. Un paquete que extiende Radiante PT conserva sus vistas y añade las suyas.

## Comprobar un paquete sin el juego

`examples/shaderpacks/README.md` explica cómo montar un paquete y compilar todos sus shaders sin el juego con las
herramientas de `native/src/tools/`: los errores salen con archivo y línea, como bajo la lista en el juego.
`compile_pack.py --debug-view <vista>` compila también una vista de depuración.

## Qué tiene un paquete

| Parte | Dónde | Notas |
| --- | --- | --- |
| Manifiesto | `configs.json`, `"radiante"` | Nombre, versión, autor, licencia, descripción, icono: se muestran en la lista |
| Ajustes | `configs.json`, `"attributes"` | Cada atributo es un ajuste y un define de GLSL; con `"runtime": true` cambia en vivo, sin recompilar |
| Nombres de los ajustes | `lang/en_us.lang`, `es_es`, `zh_cn` | Por el nombre del atributo |
| Pases | `configs.json`, `"passes"` | Pases de ray tracing, compute y pantalla completa, por ejemplo un gradeo después del tone mapping |
| Shaders | las carpetas del paquete, `hooks/` | GLSL, se compilan al elegir el paquete |
| Base | `configs.json`, `"radiante"`, `"extends"` | `radiante-pt@1`: Radiante PT debajo del paquete |

Ayudas útiles de las cabeceras de Radiante PT: `isLeavesSurface`, `isPlantSurface`, `isWaterSurface`,
`isGlassSurface` y las demás pruebas de superficie en `util/vertex.glsl`; el sol, la luna y el clima en `worldUBO` y
`skyUBO`.

## Si algo falla

- Un paquete que no carga o no compila se cambia por Radiante PT, y los mensajes del compilador (archivo y línea)
  aparecen bajo la lista. **Copiar error** los copia. Un cambio en un paquete de carpeta que no compila no se llega a
  construir: el paquete que estaba en marcha se queda.
- Si el juego se cierra probando un paquete, el siguiente inicio vuelve a Radiante PT y dice qué paquete era.
- Los ajustes se guardan por paquete, así que un paquete empieza con sus propios valores, no con los de Radiante PT.
- Un paquete hecho para otro Radiante (`extends` de otra base o versión de hooks, un `api_version` más nuevo, o
  `"requires": {"radiante": ">=..."}` que esta versión no cumple) aparece con el motivo y no se puede elegir.

## Licencia

Radiante PT es GPL-3.0, así que un paquete hecho con sus archivos también es GPL-3.0. Si las futuras cabeceras públicas
tendrán una licencia más permisiva está por decidir.
