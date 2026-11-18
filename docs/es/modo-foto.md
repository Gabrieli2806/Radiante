[← Volver al índice](README.md)

# Modo foto

El modo foto pausa la escena y cambia Radiante a una pipeline de referencia Monte Carlo a resolución completa sin reescalador ni denoiser, acumulando cientos o miles de muestras por píxel a través de una lente delgada física trazada desde la cámara.

## Cómo abrir el modo foto

- Pulsa **`F9`** dentro de un mundo (puedes cambiar la tecla en los controles de Minecraft, bajo **Radiante**).
- O abre los ajustes de Radiante (`F6`) dentro de un mundo y pulsa **Modo foto** en la barra superior.

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

- **Meta de muestras (`64` / `256` / `1024` / `4096`)**: Cuántas muestras por píxel se acumulan antes de pausar el trazado (y antes de escribir en disco una captura en cola).
- **Apertura**: Radio físico de la lente en bloques (`0` = cámara estenopeica con todo enfocado). Al subirlo disminuye la profundidad de campo y aparece bokeh real trazado a través de cristal, agua y reflejos.
- **Distancia focal**: Distancia en bloques al plano enfocado. Haz clic sobre cualquier bloque o entidad en la vista para enfocarlo automáticamente.
- **Palas (`Redonda` / `5` / `6` / `7` / `8`)** y **Giro de palas**: Alterna entre un diafragma circular y uno poligonal con rotación de los destellos del bokeh.
- **Campo de visión (`15°`–`120°`)** e **Inclinación (`-45°`–`+45°`)**: Encuadre teleobjetivo o gran angular e inclinación sobre el eje de visión.
- **Exposición (`-4` a `+4 EV`)**: Ajusta el brillo del mapeo de tonos al instante **sin reiniciar** las muestras ya acumuladas.
- **Manos**: Oculta o muestra las manos y objetos en primera persona.

## Guardar PNG y OpenEXR HDR

Las capturas se guardan en la carpeta `screenshots/` del juego (`radiante_photo_<fecha>_<hora>.png` / `.exr`):

- **Guardar PNG**: Guarda la imagen PNG de 8 bits con mapeo de tonos y gradación de color aplicados.
- **Guardar EXR**: Guarda la radiancia lineal RGB en coma flotante de 16 bits (`HALF`) acumulada antes del mapeo de tonos en formato OpenEXR 2.0 (`.exr`), lista para revelar o etalonar en Darktable, Blender, DaVinci Resolve o Photoshop.
- **Guardar PNG + EXR**: Guarda ambos archivos a partir de la misma acumulación convergida.
