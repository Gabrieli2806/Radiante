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
| `hooks/fog.glsl` | `hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction)` | Bruma y niebla con la luz volumétrica activada |
| `hooks/post_grade.glsl` | `hookPostGrade(inout vec3 color, vec2 uv)` | Gradeo de color de la imagen final. Se activa con `{"RADIANTE_HOOK_POST_GRADE": true}` en `execution_post.global_variables` |

Mantén la guarda `#ifndef`/`#define` del archivo. Los ajustes de tu paquete son defines de GLSL, usables en cualquier
hook. Un archivo de hook sigue solo las actualizaciones de Radiante PT, cosa que un archivo de sombreado copiado no
hace.

## Comprobar un paquete sin el juego

`examples/shaderpacks/README.md` explica cómo montar un paquete y compilar todos sus shaders sin el juego con las
herramientas de `native/src/tools/`: los errores salen con archivo y línea, como bajo la lista en el juego.

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
  aparecen bajo la lista. **Copiar error** los copia.
- Si el juego se cierra probando un paquete, el siguiente inicio vuelve a Radiante PT y dice qué paquete era.
- Los ajustes se guardan por paquete, así que un paquete empieza con sus propios valores, no con los de Radiante PT.
- Un paquete hecho para otro Radiante (`extends` de otra base o versión de hooks, un `api_version` más nuevo, o
  `"requires": {"radiante": ">=..."}` que esta versión no cumple) aparece con el motivo y no se puede elegir.

## Licencia

Radiante PT es GPL-3.0, así que un paquete hecho con sus archivos también es GPL-3.0. Si las futuras cabeceras públicas
tendrán una licencia más permisiva está por decidir.
