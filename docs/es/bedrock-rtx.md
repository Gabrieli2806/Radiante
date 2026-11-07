[← Volver al índice](README.md)

# Packs y aspecto de Bedrock RTX

Radiante lee resource packs hechos para **Minecraft Bedrock RTX** (`.mcpack`, `.mcaddon`) y se ve como Bedrock RTX
por defecto. Ninguno necesita un pack: Radiante trae sus propios mapas para 159 texturas vanilla. Un pack de Bedrock
añade más detalle y materiales.

## Usar un `.mcpack`

1. Suelta el `.mcpack` (o `.mcaddon`) en la carpeta `resourcepacks` de Minecraft, o arrástralo a la pantalla de packs.
2. Actívalo en la lista de resource packs, como cualquier otro. Sin descomprimir ni renombrar.
3. La primera vez, Radiante lo convierte a un pack LabPBR en `radiante/bedrock_packs/`. Los siguientes arranques usan
   esa copia.

<!-- Captura pendiente: la lista de resource packs con un .mcpack visible y activable -->

| Qué | Cómo se usa |
|---|---|
| Color, MER(S) y mapas de normales o de altura | Se convierten a LabPBR (`_s`, `_n`). Con **Materiales de Packs Bedrock** activado (por defecto), el metal, la rugosidad y el brillo se leen de los mapas MER del pack a todo detalle, como hace Bedrock. |
| Niebla y agua por bioma | Alimentan el [Ambiente de Bioma](opciones/cielo-niebla-agua.md#ambiente-de-bioma), con la intensidad más suave de Radiante. |
| Colores de luz de Vibrant Visuals (`local_lighting.json`) | Antorchas, faroles, fogatas y otras luces toman el color del pack. Velas, amatista y otros bloques de la lista reciben una luz puntual de ese color. |
| Attachables de escudo (p. ej. antorchas de luces dinámicas) | Se dibujan en el escudo en la mano; brilla e ilumina el mundo como una antorcha en la mano. |

<details>
<summary>Detalles: add-ons, subpacks y límites</summary>

- Dentro de un `.mcaddon` se encuentran todos los resource packs (carpetas con `manifest.json` y archivos `.mcpack`
  anidados). Si hay varios, se apilan en el orden encontrado, el primero arriba.
- **Los behavior packs se saltan:** bloques propios, funciones y scripts solo funcionan en Bedrock. La lista de packs
  dice "Add-on de Bedrock: solo el resource pack" y el log los nombra.
- Un pack con **subpacks** se lee con su primer subpack. Todavía no se puede elegir otro.
- Los mapas más finos que la textura de color (96 px es habitual) conservan su detalle cuando la textura de color se
  puede escalar sin que se vea borrosa; si no, se promedian al tamaño de esta.
- De los colores de Vibrant Visuals solo se usa el tono; el brillo sigue la calibración de Radiante. Las luces
  estáticas (lava, froglights, lámparas de redstone) conservan el brillo de su textura.
- Los archivos comprimidos dentro del pack se desempaquetan hasta un límite de tamaño y cantidad; lo que quede fuera
  sale en el log.

</details>

## El aspecto de Bedrock RTX

Los valores por defecto de radiante-pt son el aspecto de Bedrock RTX: el cielo y la luz del sol de Bedrock, el bloom,
la curva de tonos y la exposición, el vidrio, el agua y la niebla. Encima añade algunos extras, como bloques de luz
iluminados por su propia luz y portales del End que brillan. Funciona con o sin `.mcpack`; los dos juntos son lo más
parecido a Bedrock RTX. **Restablecer valores** lo devuelve tras cambiar ajustes.

Sin un pack de Bedrock, Radiante usa valores propios para la niebla y el agua de cada bioma y para el brillo de cada
bloque de luz. Cualquier pack de Bedrock que actives los reemplaza.

Otros aspectos son [paquetes de shaders](opciones.md#paquetes-de-shaders). El antiguo perfil **Realista** es ahora el
paquete de ejemplo `realistic`.

## Archivos de una instalación de Bedrock

Algunas partes del aspecto usan archivos que vienen con el propio Minecraft Bedrock. Radiante los busca en
`radiante/bedrock/` dentro de la carpeta del juego, y luego en la carpeta de instalación de Bedrock
(`C:/XboxGames/Minecraft for Windows/Content/data/ray_tracing/`).

| Archivo | Para qué | Sin él |
|---|---|---|
| `sky.png`, `look_up_tables.png` | **Atmósfera: Bedrock**: los colores del cielo y la luz del sol de Bedrock RTX | La opción sale en gris y se usa el cielo propio de Radiante. |
| `water_n.tga` | La superficie del agua de Bedrock | La superficie del agua propia de Radiante. |
| `caustics.png`, `wibbly.png` | **Bedrock Water Caustics** (Avanzado, desactivadas por defecto) | Las cáusticas propias de Radiante. |

Sin Bedrock en este PC, copia esos archivos de otra fuente a `<carpeta del juego>/radiante/bedrock/` con esos nombres
exactos.

<!-- Captura pendiente: el cielo con Atmósfera en Java y en Bedrock, a la misma hora -->
<!-- Captura pendiente: la misma escena con un .mcpack activado y desactivado -->

La paridad visual con Bedrock RTX se sigue en el [issue #19](https://github.com/Gabrieli2806/Radiante/issues/19).
