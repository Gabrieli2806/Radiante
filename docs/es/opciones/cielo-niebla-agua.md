[← Ajustes](../opciones.md) · [Índice](../README.md)

# Cielo, Niebla y Agua

![Ajustes de Radiante: Cielo y Agua](../../images/ui/settings-sky.png)

**Niebla volumétrica.** Niebla apagada y encendida al mediodía.

| Apagada | Encendida |
|---|---|
| ![Apagada](../../images/compare/volumetric-fog-off.webp) | ![Encendida](../../images/compare/volumetric-fog-on.webp) |

| Control | Rango / Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Atmósfera** | Java · Bedrock | Bedrock | Bedrock: colores de cielo y luz solar de Bedrock RTX, leídos de una instalación de Bedrock en este PC (o de `radiante/bedrock/sky.png` y `look_up_tables.png`). Java: el cielo propio de Radiante. Sin archivos de Bedrock disponibles, se usa el cielo Java aunque esté puesto en Bedrock. Detalle completo en [bedrock-rtx.md](../bedrock-rtx.md). |
| **Nubes** | Apagado · Vanilla · Volumétrico | Vanilla | Cómo se dibujan las nubes. Volumétricas son nubes 3D reales que proyectan sombras y cuestan más tiempo de GPU; vanilla son más baratas (usan la malla de nube plana de siempre pero trazada e iluminada); apagado es lo más rápido. |
| **Sombras de Nubes** | Activado / Desactivado | Desactivado | Las nubes volumétricas proyectan sombras suaves en el suelo. Solo con Nubes = Volumétrico. |
| **Cobertura de Nubes** | 0% – 100% | 50% | Cuánto cielo cubren las nubes volumétricas. |
| **Densidad de Nubes** | 0% – 400% | 100% | Qué tan espesas son las nubes volumétricas. |
| **Calidad de Nubes** | 8 – 256 | 64 | Pasos por rayo a través de las nubes volumétricas. Más es más suave y cuesta FPS. |
| **Estrellas** | 100 – 20000 | 3000 | Cuántas estrellas tiene el cielo nocturno. |
| **Estrellas con la Altura** | 0% – 400% | 100% | Cuánto más nítidas se ven las estrellas mientras más alto estés: a 100% brillan el doble desde la cima de una montaña (cerca de Y 190) que a nivel del mar. 0% las deja igual en todas partes. Con Ambiente de Bioma activado también cuenta el aire del bioma: más nítidas sobre desiertos, cumbres y mar abierto, más tenues en la bruma de un pantano o una jungla. |
| **Resplandor del Sol** | 0% – 300% | 100% | El halo alrededor del sol. |
| **Rayos de Luz** | 0% – 400% | 100% | Haces de luz por ventanas, claraboyas y hojas. Necesita Niebla Volumétrica activada más abajo. |
| **Trayectoria Vanilla del Sol** | Activado/Desactivado | Activado | El sol y la luna siguen la trayectoria vanilla, directamente sobre la cabeza de este a oeste. Desactivado, la trayectoria se inclina diez grados al sur, así que las sombras del mediodía no caen totalmente rectas hacia abajo. |
| **Rotación Vanilla del Sol/Luna** | Activado/Desactivado | Activado | Mantiene los cuadrados del sol y la luna alineados con su trayectoria como en vanilla, en vez de girar al cruzar el cielo. |
| **Ambiente de Bioma** | Activado/Desactivado | Activado | El aire y el agua propios de cada bioma, de forma sutil: polvo cálido sobre desiertos, bruma verdosa en pantanos, neblina fría y brillante en la nieve, rojo, turquesa y ceniza en el Nether, violeta en el End; bajo el agua, clara y luminosa junto a la playa, oscura y cerrada en el océano profundo. Se difumina de un bioma al siguiente, no entra en cuevas del Overworld y funciona con o sin Niebla Volumétrica. Más en [Ambiente de Bioma](#ambiente-de-bioma), abajo. |
| **Intensidad del Ambiente de Bioma** | 0% – 400% | 100% | Qué tan densa es la bruma de cada bioma. 100% es el aspecto ajustado, sutil; no afecta al rendimiento. |
| **Niebla Volumétrica** | Activado/Desactivado | Activado (si el pipeline lo soporta) | Marcha por el aire con rayos, al estilo Bedrock RTX: la luz del sol y la luna se dispersan en la neblina de bioma, y árboles y terreno proyectan haces de luz a través de ella. Cuesta notablemente más tiempo de GPU que la neblina simple. La calidad (muestras por rayo) se ajusta en [Rendimiento](rendimiento.md). |
| **Estilo de Niebla** | Radiance · Radiante | Radiance | Radiance: rejilla 3D (froxels) iluminada por celda y conservada entre fotogramas; haces más nítidos, las luces de bloque brillan en la niebla, y cuesta menos. Radiante: cada píxel recorre el aire; sigue las superficies exacto pero cuesta más. Oculto con el perfil Bedrock RTX, cuya niebla es el volumen propio de Bedrock. |
| **Intensidad de Niebla Volumétrica** | 0% – 400% | 100% | Qué tan densa es el aire que muestra los rayos de sol y de luz. No afecta al rendimiento — solo el número de muestras (en Rendimiento) lo hace. |
| **Distancia de Niebla** | 48 – 192 bloques | 128 | Hasta dónde llegan la niebla volumétrica y los rayos de luz. |
| **Suelo Mojado con Lluvia** | Activado / Desactivado | Activado | Las superficies a cielo abierto se oscurecen y brillan mientras llueve, y se secan poco a poco. |
| **Refracción de Lluvia** | Activado / Desactivado | Activado | Las gotas deforman lo que hay detrás como pequeñas lentes. Casi no cuesta. |
| **Vidrio sin Bordes** | Activado / Desactivado | Activado | Quita el marco de cada bloque y panel de vidrio: una pared de vidrio se ve como una sola lámina. |
| **Alcance del vidrio continuo** | Bloques y paneles · Solo bloques de vidrio · Solo paneles | Bloques y paneles | Qué vidrio une Vidrio sin Bordes. |
| **Olas del Agua** | 0% – 400% | 100% | Qué tan agitada es la superficie del agua. 0% es plana como un espejo. |
| **Turbidez del Agua** | 0% – 500% | 100% | Qué tan rápido se desvanece la vista bajo el agua. Menos es más claro. |
| **Rayos de Luz Bajo el Agua** | 0% – 400% | 100% | Haces de sol que atraviesan la superficie cuando estás bajo el agua. |

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
