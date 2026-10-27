# Hacer un paquete de shaders

Idiomas: **ES** · [EN](../en/shader-packs.md)

Un paquete de shaders es el path tracer que usa Radiante: un zip con shaders GLSL y un `configs.json`. Radiante trae uno,
Radiante PT; los jugadores añaden otros en `radiante/shaderpacks/` y los eligen en **Paquetes de shaders** dentro de los
ajustes de Radiante ([opciones](opciones.md#paquetes-de-shaders)).

Esta página es para quien hace paquetes, tal como está hoy. El soporte de paquetes es nuevo: la interfaz sobre la que se
construyen todavía no está fijada, así que un paquete puede necesitar cambios tras una versión de Radiante. Lo planeado,
y cómo los paquetes seguirán funcionando entre versiones, está en el [diseño de los paquetes de shaders](../dev/shader-packs/README.md)
(en inglés).

## Empieza desde el ejemplo

Hoy los paquetes se construyen sobre Radiante PT. Lo más rápido es partir del paquete de ejemplo del repositorio,
[`examples/shaderpacks/radiante-example`](../../examples/shaderpacks/): solo contiene los archivos que cambia (un
gradeo de color, luz a través de las hojas, tonos del agua y del cielo, cada uno con su ajuste).

1. Clona el repositorio y copia `examples/shaderpacks/radiante-example/` a una carpeta nueva a su lado.
2. En `configs.json`, dentro de `"radiante"`, cambia `display_name`, `version`, `author`, `license`, `description` e
   `icon`.
3. Cambia shaders o añade los tuyos; quédate solo con los archivos que cambies.
4. `./gradlew shaderPackExamples` construye `build/shaderpacks/<carpeta>.zip` sobre Radiante PT.
5. Copia el zip a `radiante/shaderpacks/` y elígelo en **Paquetes de shaders**.

## Qué tiene un paquete

| Parte | Dónde | Notas |
| --- | --- | --- |
| Manifiesto | `configs.json`, `"radiante"` | Nombre, versión, autor, licencia, descripción, icono: se muestran en la lista |
| Ajustes | `configs.json`, `"attributes"` | Cada atributo es un ajuste y un define de GLSL; con `"runtime": true` cambia en vivo, sin recompilar |
| Nombres de los ajustes | `lang/en_us.lang`, `es_es`, `zh_cn` | Por el nombre del atributo |
| Pases | `configs.json`, `"passes"` | Pases de ray tracing, compute y pantalla completa, por ejemplo un gradeo después del tone mapping |
| Shaders | las carpetas del paquete | GLSL, se compilan al elegir el paquete |

Ayudas útiles de las cabeceras de Radiante PT: `isLeavesSurface`, `isPlantSurface`, `isWaterSurface`,
`isGlassSurface` y las demás pruebas de superficie en `util/vertex.glsl`; el sol, la luna y el clima en `worldUBO` y
`skyUBO`.

## Si algo falla

- Un paquete que no carga o no compila se cambia por Radiante PT, y los mensajes del compilador (archivo y línea)
  aparecen bajo la lista. **Copiar error** los copia.
- Si el juego se cierra probando un paquete, el siguiente inicio vuelve a Radiante PT y dice qué paquete era.
- Los ajustes se guardan por paquete, así que un paquete empieza con sus propios valores, no con los de Radiante PT.

## Licencia

Radiante PT es GPL-3.0, así que un paquete hecho con sus archivos también es GPL-3.0. Si las futuras cabeceras públicas
tendrán una licencia más permisiva está por decidir.

## Paquetes de Radiance

Los paquetes hechos para Radiance, el proyecto del que viene el renderer de Radiante, no funcionan tal cual: los datos del
renderer cambiaron desde entonces. Ver [qué pasó al probarlos](../dev/shader-packs/radiance-packs.md) (en inglés).
