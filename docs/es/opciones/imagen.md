[← Ajustes](../opciones.md) · [Índice](../README.md)

# Imagen

![Ajustes de Radiante: Imagen](../../images/ui/settings-image.png)

Los valores por defecto son los de radiante-pt, el aspecto Bedrock RTX ([bedrock-rtx.md](../bedrock-rtx.md)).

| Control | Rango / Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Estilo de Imagen** | Natural · Bedrock · Vívido · Personalizado | Bedrock | Ajusta Saturación, Luz Rebotada, Luz del Cielo y la curva de tonos a la vez. Bedrock es el aspecto por defecto. Ver [Estilos de imagen](#estilos-de-imagen) abajo. |
| **Mapeo de Tonos** | Bedrock RTX · PBR Neutral · ACES · Reinhard | Bedrock RTX | Cómo se ajusta la luz intensa a lo que la pantalla puede mostrar. Bedrock RTX es la curva propia de Bedrock, construida con el histograma de la imagen; PBR Neutral mantiene los colores fieles; ACES es más cinematográfico y con más contraste; Reinhard es suave y plano. |
| **Saturación** | 50% – 200% | 100% | Qué tan colorida es la imagen. |
| **Contraste de Texturas** | 46% – 109% | 100% | Qué tan profundos y contrastados se ven los colores de bloques y entidades bajo la luz. 100% coincide con Bedrock RTX; menos da el aspecto más pálido y plano de versiones anteriores. |
| **Adaptación del Ojo** | 0% – 100% | 100% | Cuánto se adapta la exposición a lo que miras. Menos mantiene los cuartos oscuros y el día brillante; 100% siempre iguala la imagen. |
| **Velocidad de Adaptación del Ojo** | 5% – 250% | 100% | Qué tan rápido sigue la exposición al pasar de la oscuridad a la luz o al revés. Menos deja ver cómo se ajustan los ojos por un momento; 100% es casi instantáneo. |
| **Exposición** | -3.0 EV – +3.0 EV | +0.0 EV | Brillo general de la imagen, en pasos fotográficos. |
| **Refuerzo en Poca Luz** | 1% – 200% | 100% | Cuánto puede aclarar la exposición una escena oscura. Más deja ver mejor en cuevas y de noche; menos mantiene oscuro lo oscuro. 100% es el límite de Bedrock RTX. |
| **Exposición de Día** | 10% – 150% | 48% | Qué tan brillante se mantiene la imagen de día, frente a lo que daría solo el medidor de luz. Menos es más oscuro. |
| **Exposición de Noche** | 10% – 150% | 65% | Lo mismo de noche. |
| **Límite en Oscuridad de Día** | 50% – 3200% | 250% | Cuánto puede abrirse la exposición de día en un lugar oscuro. Bajo mantiene oscuros los cuartos cerrados; alto aclara cuartos iluminados solo por una ventana. |
| **Nitidez de FSR** | 0% – 100% | 70% | Cuánto afila FSR la imagen escalada. Más es más nítido pero puede mostrar halos en los bordes. Solo con RT-FSR. |
| **Salida HDR** | Activado / Desactivado | Desactivado | HDR en una pantalla HDR; los controles de brillo aparecen debajo al activarlo. Ver [HDR](../hdr.md). |
| **Desenfoque de Movimiento** | Activado / Desactivado | Desactivado | Difumina lo que se mueve en pantalla a lo largo de su movimiento, como una cámara. Cambiarlo recompila los shaders (un breve tirón al aplicar). |
| **Profundidad de Campo** | Activado / Desactivado | Desactivado | Mantiene nítido lo que miras y desenfoca lo mucho más cerca o más lejos, enfocando el centro de la pantalla. También recompila los shaders al cambiarlo. |
| **Fuerza de Motion Blur** | 10% – 220% | 100% | Qué tan largo es el barrido del Desenfoque de Movimiento. Requiere Desenfoque de Movimiento. |
| **Desenfoque de Profundidad de Campo** | 25% – 400% | 100% | Cuánto se desenfoca lo que está fuera de foco. Requiere Profundidad de Campo. |
| **Rango de Enfoque** | 10% – 400% | 100% | Cuánto alrededor de lo que miras se mantiene nítido. Menos desenfoca antes, para un aspecto de primer plano. |
| **Enfoque Automático** | Activado / Desactivado | Activado | La Profundidad de Campo enfoca lo que esté en el centro de la pantalla. Apagado mantiene el foco en Distancia de Enfoque, para capturas. |
| **Distancia de Enfoque** | 1 – 256 bloques | 12 | A qué distancia queda nítida la imagen con Enfoque Automático apagado. |

Con la curva de tonos Bedrock RTX (la predeterminada) el mapeo de tonos es estilo Bedrock (una curva construida a partir del
histograma de la imagen) y la exposición se mide sobre la luz que llega a las superficies a la
vista, así que los sliders de exposición se comportan algo distinto que con las curvas PBR Neutral,
ACES o Reinhard.

## Estilos de imagen

**Estilo de Imagen** ajusta a la vez Mapeo de Tonos, Saturación, Luz Rebotada y Luz del Cielo. Cambiar uno de ellos a
mano muestra **Personalizado**.

| Estilo | Mapeo de Tonos | Saturación | Luz Rebotada | Luz del Cielo | Aspecto |
|---|---|---|---|---|---|
| **Natural** | PBR Neutral | 100% | 100% | 100% | Físicamente correcto, sin nada ajustado encima |
| **Bedrock** (por defecto) | Bedrock RTX | 100% | 150% | 180% | Bedrock RTX: sombras y luz rebotada más claras |
| **Vívido** | ACES | 130% | 150% | 150% | Más contraste y color |

| Natural | Bedrock | Vívido |
|---|---|---|
| ![Natural](../../images/compare/styles-natural.webp) | ![Bedrock](../../images/compare/styles-bedrock.webp) | ![Vívido](../../images/compare/styles-vivid.webp) |

El Estilo de Imagen y la **Atmósfera** (en [Cielo y Agua](cielo-niebla-agua.md)) son independientes: la Atmósfera
elige de dónde salen los colores del cielo, el Estilo cómo se procesa la imagen final.

La Salida HDR y sus controles de brillo se explican en [HDR](../hdr.md).
