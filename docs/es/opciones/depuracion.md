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
| Albedo, Normales, Rugosidad, Metálico, Emisión, Profundidad, Vectores de Movimiento | La imagen del fotograma con ese nombre. |
| Tipos de Superficie | Un color por tipo de superficie que ve la cámara: agua, lava, vidrio, hojas, plantas... |
| Mapa de Calor Any-hit | Cuántas veces se ejecutó un shader any-hit por píxel en el rayo de cámara: azul es 1, pasando por cian, verde y amarillo hasta rojo con 16 o más; negro es ninguna. Las zonas calientes cuestan tiempo de fotograma: superficies translúcidas apiladas o caras ocultas que cruza el rayo, como el terreno de Distant Horizons bajo los chunks reales. En GPUs con micromapas de opacidad, la mayoría de las hojas y el terreno se resuelven sin shader any-hit, así que se ven negros. |
| Mapa de Calor de Rebotes | Cuántos rayos trazó cada píxel tras el rayo de cámara, en la misma escala. Muestra dónde los caminos se alargan, por ejemplo entre espejos. |

Las vistas propias de un paquete de shaders aparecen después de estas, con sus nombres. La vista no
se guarda, así que vuelve a estar desactivada la próxima vez que abras el juego.
