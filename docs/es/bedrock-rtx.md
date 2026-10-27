[← Volver al índice](README.md)

# Resource packs de Bedrock RTX

Radiante puede leer resource packs `.mcpack` hechos para **Minecraft Bedrock RTX** y convertirlos en
un pack LabPBR que su propio renderizador entiende, además de leer los archivos de cielo y cáusticas
de una instalación de Bedrock en el mismo PC. Ninguna de las dos cosas es necesaria — Radiante trae
sus propios mapas para 159 texturas vanilla — pero un pack de Bedrock suele dar más detalle y más
variedad de materiales que lo incluido.

## Usar un pack `.mcpack`

1. Coloca el archivo `.mcpack` directamente en la carpeta `resourcepacks` de Minecraft, igual que
   cualquier otro resource pack.
2. Ábrelo en el menú de resource packs del juego: Radiante lo detecta directamente en la lista, sin
   que tengas que descomprimirlo ni renombrarlo a `.zip` primero.
3. La primera vez que lo actives, Radiante lo convierte a un pack LabPBR y guarda el resultado en
   `radiante/bedrock_packs/` dentro de la carpeta del juego (no al lado del `.mcpack` original, para
   que no aparezca dos veces en la lista de packs). Las veces siguientes usa la copia convertida
   directamente, y solo vuelve a convertir si el `.mcpack` cambió o si una versión nueva del
   conversor lo requiere.

<!-- Captura pendiente: la lista de resource packs con un .mcpack visible y activable -->

### Archivos `.mcaddon`

Un `.mcaddon` (y un `.mcpack` que trae varios packs) funciona igual: colócalo en la carpeta
`resourcepacks` y aparece en la lista con su nombre de archivo, con el icono y el nombre del
resource pack que trae dentro.

- Radiante busca todos los packs que contiene: carpetas con un `manifest.json` a cualquier
  profundidad, y archivos `.mcpack` dentro de él.
- Solo se usan los **resource packs**. Los behavior packs (bloques propios, funciones, scripts de
  JavaScript, como las luces dinámicas por script de algunos add-ons) solo funcionan en el motor de
  Bedrock y se omiten; el log dice cuáles.
- Un resource pack con **subpacks** se lee como su primer subpack puesto sobre el pack. Bedrock deja
  elegir otro en los ajustes del pack; aquí todavía no hay esa opción.
- Si un `.mcaddon` trae varios resource packs, se apilan en el orden en que se encuentran, el
  primero arriba.

### Colores de luz de Vibrant Visuals

Un pack con iluminación local de Vibrant Visuals (`local_lighting/local_lighting.json`, el color de
la luz de cada bloque que ilumina) trae esos colores, mientras el pack está activado y con los
perfiles **Bedrock RTX**, que son los que ponen luces puntuales:

- Antorchas, faroles, la vara del End, el faro, el fuego, las fogatas, las bayas brillantes y el
  arbusto de luciérnagas toman el color del pack para la luz que dan (también en la mano, para
  antorchas y faroles).
- Los demás bloques que el pack marca como luz puntual, como velas, pasteles con vela y amatista,
  reciben una luz puntual propia del color del pack, tan fuerte como su nivel de luz.
- Solo se usa el tono de cada color: el brillo de cada luz sigue la calibración de Radiante. Los
  bloques que el pack marca como luz estática (lava, luces de rana, lámparas de redstone) conservan
  el brillo de su textura; su color todavía no se aplica.

### Qué convierte exactamente

Bedrock describe cada textura con un "texture set": una textura de color, un mapa MER(S)
(metalicidad, emisión, rugosidad y opcionalmente subsuperficie) y un mapa de altura o de normales.
Para cada textura de bloque de Java que tiene equivalente en Bedrock, el conversor construye:

- La textura de color (si es compatible en tamaño/formato con la de Java).
- Un mapa especular LabPBR (`_s.png`) a partir del MER — metalicidad, emisión, rugosidad y
  subsuperficie de Bedrock se traducen a los canales que LabPBR espera.
- Un mapa de normales LabPBR (`_n.png`), a partir del mapa de normales de Bedrock si lo trae, o
  derivado de su mapa de altura si no.

Los mapas más finos que la textura de color (96 px es común en packs de Bedrock, frente a 16 px de
Java) se conservan a su propio detalle cuando la textura de color puede escalarse a ese mismo tamaño
sin perder nitidez; si no, se promedian hacia abajo al tamaño de la textura de color. La niebla y el
agua por bioma del pack de Bedrock también se convierten y alimentan el Ambiente de Bioma: se
conservan los colores del pack y qué biomas son densos o claros, con la intensidad más sutil de
Radiante (ver [Cielo y Agua](opciones/cielo-niebla-agua.md)).

### Los materiales del pack tal como se hicieron

La copia convertida conserva también los mapas MER originales del pack. Con **Materiales de Packs
Bedrock** activado (por defecto, en la sección [Rendimiento](opciones/rendimiento.md)) el metal,
la rugosidad y qué texeles brillan se leen de esos mapas con todo su detalle, como hace Bedrock, en
vez de la copia LabPBR: las vetas lisas de los minerales y los bordes de metal conservan sus
reflejos, y un bloque que emite luz brilla donde el pack lo marca. Cuánta luz proyecta un bloque
sigue dependiendo de su nivel de luz, así las luces del pack encajan con la escala de luz de
Radiante. No afecta a packs Java.

## Los perfiles de render

**Perfil de Render** (en la sección [Imagen](opciones/imagen.md)) tiene dos perfiles; cambiar un
ajuste que gestiona uno de ellos lo convierte en **Personalizado**.

- **Bedrock RTX (Mejorado)**, el predeterminado, lleva el resto del aspecto a Bedrock RTX: el cielo
  y la luz solar de Bedrock, bloom, una curva de tonos estilo Bedrock construida con el histograma
  de la imagen, exposición medida sobre la luz que llega a las superficies a la vista, colores sRGB
  exactos, reflejos de vidrio y agua y superficie del agua de Bedrock, luz solar filtrada por el
  vidrio, el volumen de niebla propio de Bedrock y agua más densa. Sobre Bedrock mantiene algunos
  extras: los bloques de luz con poco brillo en el pack se ven iluminados por su propia luz y los
  portales del End brillan. Funciona con o sin un `.mcpack`; los dos juntos son lo más cercano a
  Bedrock RTX.
- **Realista** parte del mismo aspecto y pone menos luz de relleno, más contraste, color más
  calmado, olas más finas y agua turquesa más clara, para que nada parezca de plástico. Ver
  [Imagen](opciones/imagen.md) para cada valor.

## Archivos de una instalación de Bedrock

Algunas partes del aspecto de Bedrock necesitan archivos que vienen con Minecraft Bedrock, no con
un resource pack. Radiante busca cada uno en, por orden:

1. `radiante/bedrock/` dentro de la carpeta del juego.
2. `C:/XboxGames/Minecraft for Windows/Content/data/ray_tracing/` — la ruta por defecto donde
   Minecraft Bedrock (versión de Xbox/Microsoft Store) los instala, si tienes Bedrock instalado en
   el mismo PC.

| Archivo | Para qué | Sin él |
|---|---|---|
| `sky.png` (64×256) y `look_up_tables.png` (128 de ancho) | **Atmósfera: Bedrock** (en [Cielo y Agua](opciones/cielo-niebla-agua.md)): los colores de cielo y luz solar de Bedrock RTX | La opción sale en gris y muestra Java (Bedrock no disponible); se usa el cielo propio de Radiante. |
| `water_n.tga` | La superficie del agua de Bedrock (tres capas en movimiento de sus normales de agua), que usa el perfil Bedrock RTX (Mejorado) | El agua mantiene la superficie propia de Radiante. |
| `caustics.png` y `wibbly.png` | **Bedrock Caustics**, en Avanzado (desactivado por defecto) | La opción sale en gris; se usan las cáusticas procedurales de Radiante. |

<!-- Captura pendiente: el cielo con Atmósfera en Java y en Bedrock, misma hora del día -->

### Copiar los archivos a mano

Si no tienes Bedrock instalado en este PC pero sí tienes esos archivos de otra fuente, colócalos en
`radiante/bedrock/` dentro de la carpeta del juego, con esos nombres exactos:

```
<carpeta del juego>/radiante/bedrock/sky.png
<carpeta del juego>/radiante/bedrock/look_up_tables.png
<carpeta del juego>/radiante/bedrock/water_n.tga
<carpeta del juego>/radiante/bedrock/caustics.png
<carpeta del juego>/radiante/bedrock/wibbly.png
```

## Antes / después

<!-- Captura pendiente: la misma escena con un .mcpack activado y desactivado -->

## Ver también

- [estilos-y-calidad.md](estilos-y-calidad.md) — el Estilo de Imagen "Bedrock" ajusta saturación y
  luz rebotada para parecerse más a Bedrock RTX; es un ajuste de color independiente de usar un pack
  o los archivos de cielo de Bedrock.
- El estado de la paridad visual con Bedrock RTX (qué falta todavía, qué ya se igualó) se sigue en
  el [issue #19](https://github.com/Gabrieli2806/Radiante/issues/19).
