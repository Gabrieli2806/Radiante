[← Ajustes](../opciones.md) · [Índice](../README.md)

# Imagen

![Ajustes de Radiante: Imagen](../../images/ui/settings-image.png)

| Control | Rango / Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Perfil de Render** | Radiante · Bedrock RTX | Bedrock RTX | Radiante: el aspecto anterior a la 1.0. Bedrock RTX (por defecto): bloom, un mapeo de tonos estilo Bedrock, colores sRGB exactos, reflejos de vidrio y agua de Bedrock, luz solar filtrada por el vidrio, agua más densa que apaga la distancia bajo el agua y cáusticas de Bedrock. Se aplica al final: gana sobre los ajustes parecidos cambiados en la misma visita. Más en [bedrock-rtx.md](../bedrock-rtx.md). |
| **Estilo de Imagen** | Natural · Bedrock · Vívido · Personalizado | Natural | Ajusta Saturación, Luz Rebotada y Luz del Cielo a la vez. Detalle completo en [estilos-y-calidad.md](../estilos-y-calidad.md#estilos-de-imagen). |
| **Mapeo de Tonos** | PBR Neutral · ACES · Reinhard | PBR Neutral | Cómo se ajusta la luz intensa a lo que la pantalla puede mostrar. PBR Neutral mantiene los colores fieles; ACES es más cinematográfico y con más contraste; Reinhard es suave y plano. |
| **Saturación** | 50% – 200% | 120% | Qué tan colorida es la imagen. 100% no cambia nada respecto al shader pack. |
| **Contraste de Texturas** | 46% – 109% | 100% | Qué tan profundos y contrastados se ven los colores de bloques y entidades bajo la luz. 100% coincide con Bedrock RTX; menos da el aspecto más pálido y plano de versiones anteriores. |
| **Adaptación del Ojo** | 0% – 100% | 50% | Cuánto se adapta la exposición a lo que miras. Menos mantiene los cuartos oscuros y el día brillante, al estilo Bedrock; 100% siempre iguala la imagen. |
| **Velocidad de Adaptación del Ojo** | 5% – 250% | 100% | Qué tan rápido sigue la exposición al pasar de la oscuridad a la luz o al revés. Menos deja ver cómo se ajustan los ojos por un momento, como en Bedrock; 100% es casi instantáneo. |
| **Exposición** | -3.0 EV – +3.0 EV | +0.7 EV | Brillo general de la imagen, en pasos fotográficos. |
| **Refuerzo en Poca Luz** | 25% – 400% | 100% | Cuánto puede aclarar la exposición una escena oscura. Más deja ver mejor en cuevas y de noche; menos mantiene oscuro lo oscuro. |
| **Exposición de Día** | 10% – 150% | 48% | Qué tan brillante se mantiene la imagen de día, frente a lo que daría solo el medidor de luz. Menos es más oscuro. |
| **Exposición de Noche** | 10% – 150% | 65% | Lo mismo de noche. |
| **Límite en Oscuridad de Día** | 50% – 3200% | 250% | Cuánto puede abrirse la exposición de día en un lugar oscuro. Bajo mantiene oscuros los cuartos cerrados; alto aclara cuartos iluminados solo por una ventana. |
| **Nitidez de FSR** | 0% – 100% | 70% | Cuánto afila FSR la imagen escalada. Más es más nítido pero puede mostrar halos en los bordes. Solo en los pipelines con FSR. |
| **Desenfoque de Movimiento** | Activado/Desactivado | Desactivado | Difumina lo que se mueve en pantalla a lo largo de su movimiento, como una cámara. Cambiarlo recompila los shaders (un breve tirón al aplicar). |
| **Profundidad de Campo** | Activado/Desactivado | Desactivado | Mantiene nítido lo que miras y desenfoca lo mucho más cerca o más lejos, enfocando el centro de la pantalla. También recompila los shaders al cambiarlo. |
| **Fuerza de Motion Blur** | 10% – 220% | 100% | Qué tan largo es el barrido del Desenfoque de Movimiento. |
| **Desenfoque de Profundidad de Campo** | 25% – 400% | 100% | Cuánto se desenfoca lo que está fuera de foco. |
| **Rango de Enfoque** | 10% – 400% | 100% | Cuánto alrededor de lo que miras se mantiene nítido. Menos desenfoca antes, para un aspecto de primer plano. |
| **Enfoque Automático** | Activado / Desactivado | Activado | La Profundidad de Campo enfoca lo que esté en el centro de la pantalla. Apagado mantiene el foco en Distancia de Enfoque, para capturas. |
| **Distancia de Enfoque** | 1 – 256 bloques | 12 | A qué distancia queda nítida la imagen con Enfoque Automático apagado. |

Los sliders de Profundidad de Campo y de Desenfoque de Movimiento recompilan los shaders al
cambiarlos, igual que los dos interruptores. Con el perfil Bedrock RTX el mapeo de tonos es estilo
Bedrock (una curva construida a partir del histograma de la imagen) y la exposición se mide sobre
la luz que llega a las superficies a la vista, así que Mapeo de Tonos y los sliders de exposición
se comportan algo distinto ahí.

## HDR

La sección de HDR está en esta misma categoría, debajo de los controles de arriba. Se explica
entera, con capturas y consideraciones de pantalla, en [hdr.md](../hdr.md) — resumen rápido:

| Control | Rango | Por defecto |
|---|---|---|
| Salida HDR | Activado/Desactivado | Desactivado |
| Brillo Máximo HDR | 400 – 4000 nits | 1000 nits |
| Blanco de Papel HDR | 80 – 400 nits | 200 nits |
| Vista de Depuración HDR | Activado/Desactivado | Desactivado (no se guarda) |

Activar o desactivar la Salida HDR necesita reiniciar el juego; los dos sliders de brillo se aplican
al instante.
