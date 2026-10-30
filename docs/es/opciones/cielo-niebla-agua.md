[← Ajustes](../opciones.md) · [Índice](../README.md)

# Cielo y Agua

![Ajustes de Radiante: Cielo y Agua](../../images/ui/settings-sky.png)

**Niebla volumétrica.** Niebla apagada y encendida al mediodía.

| Apagada | Encendida |
|---|---|
| ![Apagada](../../images/compare/volumetric-fog-off.webp) | ![Encendida](../../images/compare/volumetric-fog-on.webp) |

Los valores por defecto son los de radiante-pt, el aspecto Bedrock RTX ([bedrock-rtx.md](../bedrock-rtx.md)).

| Control | Rango / Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Atmósfera** | Java · Bedrock | Bedrock | Bedrock: colores de cielo y luz solar de Bedrock RTX, leídos de una instalación de Bedrock en este PC (o de `radiante/bedrock/sky.png` y `look_up_tables.png`). Java: el cielo propio de Radiante. Sin esos archivos la opción sale en gris, muestra Java (Bedrock no disponible) y se usa el cielo Java. Detalle completo en [bedrock-rtx.md](../bedrock-rtx.md). |
| **Nubes** | Apagado · Vanilla · Volumétrico | Vanilla | Cómo se dibujan las nubes. Volumétricas son nubes 3D reales que pueden proyectar sombras y cuestan más tiempo de GPU; Vanilla es más barato (la malla de nube plana de siempre, pero trazada e iluminada); Apagado es lo más rápido. |
| **Altura de las Nubes** | -96 – +320 bloques | 0 | Sube o baja las nubes Vanilla sobre su altura normal. Solo con nubes Vanilla. |
| **Sombras de Nubes** | Activado / Desactivado | Desactivado | Las nubes volumétricas proyectan sombras suaves en el suelo. Solo con nubes Volumétricas, igual que los tres de abajo. |
| **Cobertura de Nubes** | 0% – 100% | 50% | Cuánto cielo cubren las nubes volumétricas. |
| **Densidad de Nubes** | 0% – 400% | 100% | Qué tan espesas son las nubes volumétricas. |
| **Calidad de Nubes** | 8 – 256 | 64 | Pasos por rayo a través de las nubes volumétricas. Más es más suave y cuesta FPS. |
| **Estrellas** | 100 – 20000 | 3000 | Cuántas estrellas tiene el cielo nocturno. |
| **Estrellas con la Altura** | 0% – 400% | 100% | Cuánto más nítidas se ven las estrellas mientras más alto estés: a 100% brillan el doble desde la cima de una montaña (cerca de Y 190) que a nivel del mar. 0% las deja igual en todas partes. Con Ambiente de Bioma activado también cuenta el aire del bioma: más nítidas sobre desiertos, cumbres y mar abierto, más tenues en la bruma de un pantano o una jungla. |
| **Resplandor del Sol** | 0% – 300% | 100% | El halo alrededor del sol. |
| **Rayos de Luz** | 0% – 400% | 100% | Haces de luz por ventanas, claraboyas y hojas, hasta unos 48 bloques de ti. Requiere Niebla Volumétrica. |
| **Perspectiva Aérea** | 0% – 400% | 100% | La neblina propia del aire en la distancia: el terreno lejano, también el de Distant Horizons, se funde con el cielo que tiene detrás, alrededor de un octavo a 16 chunks y dos quintos a 64 a nivel del mar en un día despejado. Más espesa con lluvia, tormenta y en biomas húmedos (jungla, pantano), más fina sobre desiertos y desde una cima, nula en cuevas. Funciona con la Niebla Volumétrica activada o no y con Ambiente de Bioma (sin sumarse a su neblina). 0% la quita; no afecta al rendimiento. |
| **Inicio de la Perspectiva Aérea** | 0 – 1024 bloques | 128 | A qué distancia empieza la neblina lejana. Lo más cercano no se nubla; a partir de ahí la neblina entra a lo largo de la mitad de esa distancia (al menos 32 bloques). Súbelo para dejar más nítido el terreno a tu alrededor. |
| **Trayectoria del Sol** | Vanilla · Inclinada · Personalizada | Vanilla | Vanilla: el sol y la luna pasan directamente sobre la cabeza de este a oeste. Inclinada: la trayectoria se inclina diez grados al sur, así que las sombras del mediodía no caen rectas hacia abajo. Personalizada aparece cuando los ajustes de [Trayectoria del Sol y la Luna](#trayectoria-del-sol-y-la-luna-avanzado) tienen otros valores. |
| **Rotación Vanilla del Sol/Luna** | Activado / Desactivado | Activado | Mantiene los cuadrados del sol y la luna alineados con su trayectoria como en vanilla, en vez de girar al cruzar el cielo. |
| **Suelo Mojado con Lluvia** | Activado / Desactivado | Activado | Las superficies a cielo abierto se oscurecen y brillan mientras llueve, y se secan poco a poco. |
| **Charcos** | 0% – 100% | 50% | Cuánta agua estancada deja la lluvia sobre las caras superiores planas de los bloques completos al aire libre, con Suelo Mojado con Lluvia activado: zonas que se unen en una película con anillos tras un par de minutos de lluvia, que luego se seca por zonas en unos tres minutos. Completa desde 50%; 0% deja solo el suelo mojado. |
| **Humedad** | 0% – 200% | 100% | Cuánto se oscurecen y brillan las superficies mojadas con la lluvia. 0% las deja como secas. |
| **Paredes Mojadas** | 0% – 100% | 50% | Cuánto se mojan las paredes, troncos y otras caras verticales expuestas a la lluvia, frente al suelo. La parte de abajo sigue seca. |
| **Tiempo de Empapado** | 10 – 600 s | 150 s | Cuánto tarda la lluvia constante en empapar el suelo abierto: se oscurece en segundos y luego el agua se acumula en zonas que se unen en una película al final. |
| **Tiempo de Secado** | 30 – 600 s | 180 s | Cuánto tarda el suelo en secarse después de la lluvia; los charcos se secan por zonas y el último desaparece para entonces. |
| **Destello de Rayo** | 0% – 200% | 100% | Con qué intensidad un rayo ilumina el mundo durante una tormenta, desde la dirección del rayo y con sombras. 0% deja solo el brillo del propio rayo. Se apaga con la opción de accesibilidad *Ocultar destellos de relámpagos* del juego. |
| **Viento** | Activado / Desactivado | Desactivado | Las hojas, la hierba, las flores, los cultivos, los brotes, la caña de azúcar, las enredaderas y los nenúfares se mecen con el viento a unos 48 bloques de ti, más con lluvia y tormenta; las algas y el kelp se mecen despacio con la corriente. Se mueve la geometría misma, así que las sombras, los reflejos y la luz entre las hojas se mueven con ella. Cambiarlo recarga la geometría del mundo. En un bosque denso cuesta unos 0,6 ms por fotograma en una RTX 5070; desactivado, nada. |
| **Fuerza del Viento** | 0% – 200% | 100% | Cuánto mueve el viento las hojas y las plantas con Viento activado. 0% las deja quietas. |
| **Vidrio sin Bordes** | Activado / Desactivado | Desactivado | Quita el marco de cada bloque y panel de vidrio: una pared de vidrio se ve como una sola lámina. Apagado por defecto, porque Bedrock muestra cada bloque en su marco. |
| **Alcance del vidrio continuo** | Bloques y paneles · Solo bloques de vidrio · Solo paneles | Bloques y paneles | Qué vidrio une Vidrio sin Bordes. Requiere Vidrio sin Bordes. |
| **Ambiente de Bioma** | Activado / Desactivado | Desactivado | El aire y el agua propios de cada bioma, de forma sutil: polvo cálido sobre desiertos, bruma verdosa en pantanos, neblina fría y brillante en la nieve, rojo, turquesa y ceniza en el Nether, violeta en el End; bajo el agua, clara y luminosa junto a la playa, oscura y cerrada en el océano profundo. Se difumina de un bioma al siguiente, no entra en cuevas del Overworld y funciona con o sin Niebla Volumétrica. Más en [Ambiente de Bioma](#ambiente-de-bioma), abajo. |
| **Intensidad del Ambiente de Bioma** | 0% – 400% | 100% | Qué tan densa es la bruma de cada bioma. 100% es el aspecto ajustado, sutil; no afecta al rendimiento. Requiere Ambiente de Bioma. |
| **Niebla Volumétrica** | Activado / Desactivado | Activado | Marcha por el aire con rayos: la luz del sol y la luna se dispersan en él, y árboles y terreno proyectan haces de luz a través de él. Cuesta notablemente más tiempo de GPU que la neblina simple. Por defecto es el volumen de niebla propio de Bedrock. |
| **Estilo de Niebla** | Froxel · Radiante | Froxel | Froxel: rejilla 3D de luz fija al mundo alrededor de la cámara, iluminada por celda y conservada entre fotogramas, así la niebla no se mueve al mirar alrededor; haces más nítidos, las luces de bloque brillan en la niebla, y cuesta menos. Radiante: cada píxel recorre el aire; sigue las superficies exacto pero cuesta más. Oculto mientras la Niebla Volumétrica de Bedrock (Avanzado, activa por defecto) nubla el aire con su propio volumen. |
| **Intensidad de Niebla Volumétrica** | 0% – 400% | 100% | Qué tan denso es el aire que muestra los rayos de sol y de luz. No afecta al rendimiento — solo el número de muestras (en [Rendimiento](rendimiento.md)) lo hace. |
| **Distancia de Niebla** | 48 – 192 bloques | 128 | Hasta dónde llegan la niebla volumétrica y los rayos de luz. |
| **Olas del Agua** | 0% – 400% | 100% | Qué tan agitada es la superficie del agua. 0% es plana como un espejo. |
| **Turbidez del Agua** | 0% – 400% | 100% | Qué tan rápido se desvanece la vista bajo el agua. Menos es más claro. 100% es la extinción completa de Bedrock. |
| **Rayos de Luz Bajo el Agua** | 0% – 400% | 100% | Haces de sol que atraviesan la superficie cuando estás bajo el agua. |

Con la Niebla Volumétrica de Bedrock activada (en Avanzado, por defecto) el aire lo nubla
el volumen propio de Bedrock, de 50 bloques: muestra la niebla del bioma (con Ambiente de Bioma activado) y los
rayos de luz (Rayos de Luz e Intensidad de Niebla Volumétrica fijan cuánto). No usa Distancia de Niebla ni Calidad
de Niebla Volumétrica: esas filas salen en gris y lo dicen. La neblina en la distancia más allá es la Perspectiva
Aérea.

## Trayectoria del Sol y la Luna (avanzado)

Con **Avanzado** activado, el grupo Trayectoria del Sol y la Luna ajusta a mano el recorrido del
cielo. Todo lo que usa el sol o la luna lo sigue al instante, sin reconstruir: el cielo y su
resplandor, las sombras, las nubes, la niebla y los rayos de luz, el agua y la iluminación de
Bedrock RTX.

| Control | Rango | Por defecto | Qué hace |
|---|---|---|---|
| **Inclinación del Sol** | -45° – 45° | 0° | Cuánto se inclina la trayectoria del sol respecto a la vertical: positivo al sur, negativo al norte. El sol sigue saliendo y poniéndose a la misma hora. Inclinada es 10°. |
| **Rotación de la Trayectoria del Sol** | -180° – 180° | 0° | Gira la trayectoria por el cielo; positivo la gira en sentido horario visto desde arriba, moviendo la salida del sol del este hacia el sur. |
| **Rotación del Sol** | -180° – 180° | 0° | Gira el cuadrado del sol sobre su propio centro. Solo cambia cómo se ve. |
| **La Luna Sigue al Sol** | Activado / Desactivado | Activado | La luna cruza por la trayectoria del sol, opuesta a él, como en vanilla. Desactivado, usa los dos ajustes de abajo; sigue saliendo cuando el sol se pone. |
| **Inclinación de la Luna** | -45° – 45° | 0° | La inclinación propia de la luna, como Inclinación del Sol. Solo con La Luna Sigue al Sol desactivado. |
| **Rotación de la Trayectoria de la Luna** | -180° – 180° | 0° | El giro propio de la luna, como Rotación de la Trayectoria del Sol. Solo con La Luna Sigue al Sol desactivado. |
| **Rotación de la Luna** | -180° – 180° | 0° | Gira el cuadrado de la luna sobre su propio centro. Solo cambia cómo se ve. |

Con el trazado de rayos apagado, Minecraft dibuja su propio cielo, que mantiene la trayectoria
vanilla.

## Ambiente de Bioma

Cada bioma tiene su propio aire y cada tipo de agua su propio aspecto. Está pensado para sentirse
más que para verse: una llanura pierde menos de una décima parte de la vista a diez chunks, y el
aire más denso (un jardín pálido) cerca de un tercio. La bruma pertenece a la distancia y al
horizonte; el cielo por encima queda intacto.

- **Aire.** Un tinte, una densidad y cómo absorbe cada color: el polvo del desierto y las badlands
  vuelve cálida la distancia, la nieve y el hielo dan una neblina fría y brillante, los bosques un
  gris verdoso suave, pantanos y manglares una bruma baja que se queda cerca del agua, y las
  cumbres un aire casi limpio. Se espesa con la lluvia, se aclara con la altura y no entra en
  cuevas.
- **Agua.** Vista desde dentro: clara y luminosa junto a la playa y en océanos cálidos, normal en
  el océano, oscura y cerrada en los océanos profundos (más aún en los fríos), turbia en los
  pantanos. Cuanto más hondo está la cámara, más oscura el agua a su alrededor, y el agua bajo
  techo o en una cueva recibe menos luz que a cielo abierto.
- **Al cruzar una frontera.** Se promedian los biomas a veinte bloques de la cámara y el resultado
  entra poco a poco en un par de segundos, así un bioma se funde con el siguiente y nunca cambia
  de golpe.
- **Packs de Bedrock RTX.** Un `.mcpack` convertido trae su propia niebla y agua por bioma. Sus
  colores, y qué biomas son densos o claros, salen del pack; la intensidad se lleva al mismo rango
  sutil.
- **Biomas de mods y data packs.** No hay nada que configurar: el aspecto se deduce de las
  etiquetas del bioma (pantano, desierto, nevado, tenebroso, mágico, etc., incluidas las etiquetas
  compartidas `c:` de los loaders), de su clima y de sus propios colores de niebla y agua.

## Nether y End

El Ambiente de Bioma también aplica en el Nether y el End, donde no hay cielo que ilumine la
bruma: ahí tiene color propio (rojo en los yermos, carmesí, turquesa, azul ceniza en el valle de
almas, gris en los deltas de basalto, violeta en el End) y se mantiene fina, de modo que nunca
tapa más de la mitad de lo que hay detrás. La
Niebla Volumétrica, en cambio, solo se aplica en el Overworld; en el Nether y el End se usa siempre
la neblina simple, esté activada o no.
