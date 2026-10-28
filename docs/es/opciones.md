[← Volver al índice](README.md)

# Ajustes de Radiante

Se abren con `F6` dentro de un mundo, o desde el botón de Radiante en Opciones → Ajustes de vídeo
(también en el menú principal). Las secciones están a la izquierda (Calidad, Imagen, Iluminación,
Cielo y Agua, Rendimiento, Otros) y se muestra una a la vez; el panel de la derecha describe la
opción bajo el ratón. Los cambios se acumulan y se aplican juntos con **Aplicar** (la pantalla
sigue abierta) o **Listo**, así que el renderizador se reconstruye una sola vez; **Deshacer**
descarta lo cambiado desde el último Aplicar. Mientras un cambio se compila, un aviso en la esquina
superior derecha muestra el progreso (compilando shaders, construyendo pipelines y luego "ajustes
aplicados" o el error) y puedes seguir usando los menús y el juego. Funciona igual dentro de un
mundo y en el menú principal.

![Ajustes de Radiante: Calidad](../images/ui/settings-quality.png)

## Buscar

La caja **Buscar...** filtra los ajustes de todas las secciones por nombre. Es la forma más rápida
de encontrar algo sin saber en qué sección está. Con Avanzado apagado, una sección dice cuántas
coincidencias más tiene entre los ajustes avanzados.

## Teclado

Arriba y Abajo recorren las opciones y los títulos de los grupos avanzados, y pasada la última llegan
a los botones de abajo; Arriba desde la primera opción va al cuadro de búsqueda. Izquierda y Derecha
mueven un slider un paso (diez con Shift), cambian una lista, pliegan o despliegan un grupo, o pasan
de un botón a otro. Enter o Espacio activan un interruptor, cambian una lista, pliegan un grupo o
pulsan el botón. Tab pasa a la sección siguiente (Shift+Tab: a la anterior).

## Mando

La pantalla funciona con los mods de mando: los que recorren los menús como las flechas de Minecraft
(Controlify y parecidos) y los que pulsan teclas por los botones del mando. La cruceta o el stick
izquierdo se mueven como las flechas de arriba, A hace de Enter y B sale de la pantalla como
**Listo**. Con Controlify, además:

| Botón | Hace |
|---|---|
| LB / RB | Sección anterior / siguiente |
| X | Aplicar |
| Y | Deshacer |
| Stick derecho | Arriba y abajo desplazan la lista (el foco sigue en una fila a la vista); izquierda y derecha mueven un slider diez pasos |

Controlify muestra los dibujos de sus botones junto a Listo, Aplicar y Deshacer y bajo las secciones.
Su ratón virtual queda apagado en esta pantalla salvo que lo actives en ella.

## Qué dice el panel

- **Impacto en rendimiento: Ninguno / Bajo / Medio / Alto / Variable** en los ajustes que cuestan
  tiempo de fotograma. Variable significa que depende de la escena (por ejemplo, de cuántas luces
  haya).
- Una fila que no hace nada tal como están las cosas sale en gris, y el panel dice por qué: hay que
  activar antes otro ajuste, otro ocupa su lugar, el trazado de rayos está apagado o faltan los
  archivos de Bedrock.
- **También decide:** los ajustes que este apaga o reemplaza.
- Un ajuste que gestiona el perfil de render lo indica: cambiarlo convierte el perfil en
  Personalizado.

## Calidad y Restablecer valores

**Calidad** (Baja / Media / Alta / Ultra / Personalizado) ajusta de una vez lo que más cuesta:
modo del escalador, rebotes de luz, niebla volumétrica, nubes, distancia de renderizado y más.
Tocar uno de esos a mano lo deja en Personalizado. Detalle en
[estilos-y-calidad.md](estilos-y-calidad.md#niveles-de-calidad).

**Restablecer valores**, abajo a la izquierda, devuelve todo a un install nuevo, incluidos los
ajustes del pipeline y del shader pack que no tienen control aquí, y vuelve a elegir el perfil
Bedrock RTX (Mejorado). Con **Ctrl** pulsado se convierte en **Recompilar Shaders**: aplica los
cambios y vuelve a compilar los shaders, para cuando la imagen se ve mal tras cambiar ajustes.

## Ajustes que piden reiniciar

Salida HDR, e instalar o borrar un escalador, solo se aplican al reiniciar. Salida HDR lleva la
marca **(reinicio)** y, al salir de la pantalla, un aviso lista los cambios pendientes con
**Cerrar el juego ahora** o **Más tarde**.

## Secciones

| Sección | Contenido |
|---|---|
| [Calidad](opciones/calidad-y-escalado.md) | Interruptor de trazado de rayos, nivel de calidad, pipeline, modo DLSS o de escalado, Reconstrucción de Rayos, resolución de render y de salida, generación de fotogramas, Reflex. |
| [Imagen](opciones/imagen.md) | Perfil de render, estilo de imagen, exposición, nitidez de FSR, HDR, desenfoque de movimiento, profundidad de campo. |
| [Iluminación](opciones/iluminacion.md) | Brillos, luz rebotada y del cielo, muestreo de luces de bloque, ReSTIR, SER, luz del objeto en mano, sombra en primera persona. |
| [Cielo y Agua](opciones/cielo-niebla-agua.md) | Atmósfera, nubes, estrellas, sol, trayectoria del sol, lluvia, vidrio, ambiente de bioma, niebla volumétrica, agua. |
| [Rendimiento](opciones/rendimiento.md) | Rebotes de luz, espejos, vidrio, textura del agua, portal, lluvia, caché, tablero, parallax, materiales de packs Bedrock, calidad de niebla, construcción de chunks. |
| [Otros](opciones/otros.md) | Contorno de bloque, overlays de otros mods, fondo de textos, bordes de parallax, registro de depuración. |
| [Complementos](complementos.md) | Solo con un complemento instalado: sus propias opciones, bajo su nombre. |

Un ajuste solo aparece si el pipeline, la GPU o el shader pack lo admiten: Modo DLSS solo con
RT-DLSS, Modo de Escalado solo con RT-FSR o RT-XeSS, Reflex solo en NVIDIA, la generación de
fotogramas no con Salida HDR. Si algo de esta documentación no aparece en tu pantalla, depende de
eso.

## Presets

**Presets**, junto a la caja de búsqueda, guarda tus aspectos: guarda el actual con un nombre,
cambia entre ellos con un clic, copia uno como código para compartir o importa un código que te
hayan enviado. Un preset lleva luz, cielo, niebla, agua, tono y efectos de cámara; el escalador,
la generación de fotogramas y el HDR no cambian.

## Paquetes de shaders

**Paquetes de shaders**, junto a Presets, elige el trazador de rayos: el **Radiante PT** incluido
(el predeterminado) o un paquete que hayas añadido. Los paquetes van en `radiante/shaderpacks/`
dentro de la carpeta del juego (**Abrir carpeta** la abre), como `.zip` o como carpeta, con el
`configs.json` del paquete arriba del todo: un zip de una carpeta que contiene el paquete (como el
"Download ZIP" de GitHub) aparece con ese problema. Es la carpeta propia de Radiante, no la
`shaderpacks/` del juego, que Iris y OptiFine usan para otro tipo de paquetes.

La lista muestra el icono, el nombre, la versión, el autor y la licencia de cada paquete, cuando
el paquete los da. Haz clic en un paquete y pulsa **Usar** (o doble clic, o Enter) para cambiar:
los shaders se compilan como con cualquier otro cambio. Si el paquete no carga o un shader no
compila, Radiante vuelve a Radiante PT y muestra bajo la lista qué falló (los mensajes del
compilador, con archivo y línea); **Copiar error** lo copia para el autor del paquete. Si el juego se cierra mientras se prueba un
paquete (un cierre inesperado o la GPU deja de responder), el siguiente inicio vuelve a Radiante
PT y lo indica aquí. Cada paquete
guarda sus ajustes en un archivo a su lado (`<paquete>.zip.txt`), así que cambiar de uno a otro
los conserva. La mayoría de los ajustes de Radiante son de Radiante PT; otro paquete muestra los
que ofrece.

Los paquetes hechos para Radiance (el mod del que nació Radiante) usan el mismo formato pero una
interfaz del renderizador más antigua, y de momento no funcionan tal cual.

## Avanzado

**Avanzado: NO / SÍ**, abajo, muestra los controles finos de cada sección en grupos plegables bajo
los principales (exposición y tono, efectos de cámara, luz, nubes, atmósfera, agua, niebla,
estrellas, trayectoria del sol y la luna, denoiser, renderizador, escalador, vistas de depuración,
otros ajustes del pipeline). Sus valores se aplican se muestren o no. Son los parámetros propios
del pipeline: cámbialos de uno en uno, y usa **Deshacer** o **Restablecer valores** para volver.
