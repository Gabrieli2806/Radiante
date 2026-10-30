[← Ajustes](../opciones.md) · [Índice](../README.md)

# Depuración

Herramientas para ver qué hace el renderizador y para reportar problemas. La sección solo aparece
mientras **Avanzado** está activado.

| Control | Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Vista de Depuración** | Desactivada, y las vistas del paquete en uso | Desactivada (no se guarda) | Muestra una de las imágenes internas del renderizador en lugar de la imagen, igual que `/radiante debug view <vista>`. Cambiarla recompila los shaders. |
| **Modo de Depuración SHaRC** | Desactivado, y las vistas de la caché | Desactivado | Muestra las vistas internas de la caché de luz en lugar de la imagen. |
| **Vista de Depuración HDR** | Activado/Desactivado, solo con la Salida HDR activada | Desactivado (no se guarda) | Tiñe de azul las partes SDR de la imagen. Mira [HDR](../hdr.md). |
| **Tiempos de GPU en F3** | Activado / Desactivado | Activado | Muestra el tiempo de cada pase de render en la pantalla F3. La misma opción que en la pantalla de opciones de depuración (`F3` + `F6`). |
| **Registro de Depuración** | Activado / Desactivado | Activado | Escribe en `latest.log` el detalle de Radiante y del renderizador nativo (líneas `[native] ...`). Activado por defecto para que un solo log baste al reportar un problema. Los errores del renderizador llegan al log aunque lo apagues. |
| **Resumen para Reportes** | Copiar | | Copia las versiones, la GPU, el pipeline en uso y todos los ajustes del pipeline al portapapeles, y los escribe en el registro del juego. Pégalo en tu reporte. |
| **Registros del Juego** | Abrir Carpeta | | Abre la carpeta con `latest.log`. |
| **Paquetes de Shaders** | Abrir Carpeta | | Abre la carpeta de la que se cargan los paquetes de shaders. |

## Vistas de depuración

| Vista | Muestra |
|---|---|
| Albedo, Normales, Rugosidad, Metálico, Emisión, Profundidad | La imagen del fotograma con ese nombre. |
| Vectores de Movimiento | Hacia dónde se movió cada píxel desde el último fotograma: gris es quieto, más rojo es a la derecha, menos rojo a la izquierda, más verde hacia abajo, menos verde hacia arriba; con 32 píxeles llega al color pleno. |
| Tipos de Superficie | Un color por tipo de superficie que ve la cámara: agua, lava, vidrio, hojas, plantas... |
| Mapa de Calor Any-hit | Cuántas veces se ejecutó un shader any-hit por píxel en el rayo de cámara: azul es 1, pasando por cian, verde y amarillo hasta rojo con 16 o más; negro es ninguna. Las zonas calientes cuestan tiempo de fotograma: superficies translúcidas apiladas o caras ocultas que cruza el rayo, como el terreno de Distant Horizons bajo los chunks reales. En GPUs con micromapas de opacidad, la mayoría de las hojas y el terreno se resuelven sin shader any-hit, así que se ven negros. |
| Mapa de Calor de Rebotes | Cuántos rayos trazó cada píxel tras el rayo de cámara, en la misma escala. Muestra dónde los caminos se alargan, por ejemplo entre espejos. |
| Lluvia | Rojo: cuánto se moja la superficie. Verde: cuánto está abierta al cielo (con cualquier clima). Azul: agua estancada. Rojo y azul necesitan lluvia, y suben a medida que se acumula la humedad. El agua y el vidrio quedan en negro. |
| Luz Directa, Luz Indirecta | La luz ruidosa sobre superficies difusas antes del denoiser, comprimida al rango de la pantalla: directa de las luces, o tras rebotar. Una superficie cuyo camino tomó el lóbulo especular en ese fotograma se ve negra, así que es normal ver ruido. La lluvia oculta el sol, así que la Luz Directa se oscurece con ella. |
| Aporte de SHaRC | La luz que los caminos de cada píxel tomaron de la caché SHaRC, comprimida al rango de la pantalla, antes de la exposición, así que se ve tenue. Negro con SHaRC apagado, y casi negro al aire libre en terreno abierto, donde los rebotes se van al cielo y no consultan la caché; en interiores la mayoría de los píxeles la muestran. |
| Difuso sin Filtrar, Especular sin Filtrar | Las señales ruidosas de la primera superficie antes del denoiser, comprimidas al rango de la pantalla. Con NRD son las señales que lee NRD; DLSS Ray Reconstruction lee la imagen completa, y estas muestran la misma luz separada. |
| Luz de la Niebla, Transmitancia de la Niebla | Lo que hace la niebla por sí sola: la luz que añade (comprimida al rango de la pantalla) y cuánto de lo que hay detrás la atraviesa, blanco para todo, negro para nada. |

Las vistas propias de un paquete de shaders aparecen después de estas, con sus nombres. La vista no
se guarda, así que vuelve a estar desactivada la próxima vez que abras el juego.
