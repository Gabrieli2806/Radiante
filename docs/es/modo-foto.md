[← Volver al índice](README.md)

# Modo foto

El modo foto pausa la escena, libera la cámara y promedia un fotograma tras otro de la imagen trazada a través de una lente delgada física trazada desde la cámara. Con el **Denoiser** activado (por defecto) promedia la imagen ya limpia y queda sin ruido en unas decenas de muestras; desactivado, promedia el trazado de rayos tal cual, una imagen de referencia sin denoiser que necesita miles de muestras para que desaparezca el ruido.

## Cómo abrir el modo foto

El modo foto no tiene tecla hasta que le asignes una: en **Opciones → Controles → Asignación de teclas** de Minecraft, bajo **Radiante**, asigna **Modo foto** y pulsa esa tecla dentro de un mundo.

> [!TIP]
> Entra al modo foto desde primera persona para una cámara limpia sin tu personaje, o cambia antes a tercera persona (`F5`) si quieres que tu personaje quede posando en el mundo mientras vuelas con la cámara a su alrededor.

## Controles

| Entrada | Acción |
|---|---|
| `WASD` / `Espacio` / `Shift` | Volar con la cámara del modo foto (mantén `Ctrl` para ir más rápido) |
| Rueda del ratón (en la escena) | Ajustar la velocidad de vuelo (`Shift` + rueda ajusta el campo de visión) |
| Arrastrar ratón (en la escena) | Orientar la cámara |
| Clic izquierdo (en la escena) | Enfocar la lente en el bloque o entidad bajo el cursor |
| `H` o `F1` | Ocultar o mostrar la interfaz del modo foto |
| `F2` | Guardar un PNG al alcanzar las muestras objetivo |
| `R` | Restablecer posición, lente y exposición |
| `Esc` | Salir del modo foto y volver al juego normal |

## Cámara y lente física

- **Denoiser (Sí / No)**: Sí, el denoiser del juego (DLSS Ray Reconstruction, o el de tu reescalador) limpia cada fotograma y el modo foto los promedia: limpio en unas decenas de muestras. No, se promedia el trazado de rayos tal cual, una referencia sin denoiser que necesita miles de muestras. Al cambiarlo la pipeline se reconstruye un momento.
- **Meta de muestras (`64` / `256` / `1024` / `4096`)**: Cuántas muestras por píxel se acumulan antes de pausar el trazado (y antes de escribir en disco una captura en cola).
- **Apertura**: Radio físico de la lente en bloques (`0` = cámara estenopeica con todo enfocado). Al subirlo disminuye la profundidad de campo y aparece bokeh real trazado a través de cristal, agua y reflejos.
- **Distancia focal**: Distancia en bloques al plano enfocado. Haz clic sobre cualquier bloque o entidad en la vista para enfocarlo automáticamente.
- **Palas (`Redonda` / `5` / `6` / `7` / `8`)** y **Giro de palas**: Alterna entre un diafragma circular y uno poligonal con rotación de los destellos del bokeh.
- **Campo de visión (`15°`–`120°`)** e **Inclinación (`-45°`–`+45°`)**: Encuadre teleobjetivo o gran angular e inclinación sobre el eje de visión.
- **Exposición (`-4` a `+4 EV`)**: Ajusta el brillo del mapeo de tonos al instante **sin reiniciar** las muestras ya acumuladas.
- **Manos**: Oculta o muestra las manos y objetos en primera persona.

## Panel cinematográfico

El panel de la izquierda:

- **Encuadre**: el formato al que se recorta la foto: la pantalla, `16:9`, los formatos de cine `2.39:1` y `1.85:1`, `4:3`, `1:1` o `9:16` vertical. Las barras muestran lo que queda fuera, y el PNG y el EXR guardados se recortan igual.
- **Guías**: líneas de composición sobre el encuadre (tercios, proporción áurea, centro con zona segura, diagonales). Nunca salen en la foto guardada.
- **Lente**: recorre objetivos full frame (14, 24, 35, 50, 85, 135 mm) ajustando el campo de visión, que también muestra su distancia focal.
- **Escena** (solo un jugador): horas y climas preparados por turnos: Amanecer, Mañana, Mediodía, Hora Dorada, Atardecer, Hora Azul, Luz de Luna, Día Lluvioso, Tormenta y Noche de Tormenta.
- **Estilo**: ajustes de cámara preparados por turnos, el objetivo, la profundidad de campo y la exposición a la vez: Natural, Paisaje, Retrato, Macro, Cinemático, Nocturno y Dramático. Los controles los afinan.
- **Hora del día** y **Clima** (solo un jugador): cambian la hora y el clima del mundo para la hora dorada, la hora azul, la noche, la lluvia o una tormenta. El mundo avanza un momento para que el cielo y la lluvia se actualicen, y luego la imagen empieza de nuevo.

## Guardar PNG y OpenEXR HDR

Las capturas se guardan en la carpeta `screenshots/` del juego (`radiante_photo_<fecha>_<hora>.png` / `.exr`):

- **Guardar PNG**: Guarda la imagen PNG de 8 bits con mapeo de tonos y gradación de color aplicados.
- **Guardar EXR**: Guarda la radiancia lineal RGB en coma flotante de 16 bits (`HALF`) acumulada antes del mapeo de tonos en formato OpenEXR 2.0 (`.exr`), lista para revelar o etalonar en Darktable, Blender, DaVinci Resolve o Photoshop.
- **Guardar PNG + EXR**: Guarda ambos archivos a partir de la misma acumulación convergida.
