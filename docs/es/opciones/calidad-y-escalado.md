[← Ajustes](../opciones.md) · [Índice](../README.md)

# Calidad

![Ajustes de Radiante: Calidad](../../images/ui/settings-quality.png)

| Control | Valores | Por defecto | Qué hace |
|---|---|---|---|
| **Trazado de Rayos** | Activado / Desactivado | Activado | Cambia al instante entre el renderizador con trazado de rayos de Radiante y el de Minecraft, igual que la tecla Alternar Trazado de Rayos (sin asignar por defecto; se asigna en Controles). El mundo se reconstruye para el renderizador que toma el relevo. Aparece dentro de un mundo. |
| **Calidad** | Baja · Media · Alta · Ultra · Personalizado | — | Ajusta de una vez lo que más cuesta. Ver [Calidad y Restablecer valores](../opciones.md#calidad-y-restablecer-valores). |
| **Pipeline** | RT-DLSS · RT-Nativo · RT-FSR · RT-XeSS. Los no instalados salen como `(Instalar)` o `(Requiere reinicio)` | RT-DLSS si DLSS está instalado y la GPU lo soporta; si no, RT-XeSS si está instalado, y si no RT-FSR | Denoiser y escalador. Ver abajo y [Escaladores](../escaladores.md). |
| **Modo DLSS** | Rendimiento Ultra · Rendimiento · Equilibrado · Calidad · DLAA · Personalizado | Rendimiento Ultra | Solo con RT-DLSS. La resolución a la que DLSS renderiza antes de escalar. Personalizado muestra Resolución de Render. |
| **Reconstrucción de Rayos** | Activado / Desactivado | Activado | Solo con RT-DLSS. El denoiser propio de DLSS; desactivado, NRD hace el denoising y DLSS solo escala, lo que puede ayudar contra el ghosting. Solo aparece donde la GPU puede usar ambos. |
| **Modo de Escalado** | FSR: Rendimiento Ultra · Rendimiento · Equilibrado · Calidad · AA Nativo. XeSS añade Ultra Calidad y Ultra Calidad Plus. Los dos terminan con Personalizado | Rendimiento Ultra | Lo mismo que Modo DLSS, para FSR y XeSS. El nivel de Calidad también lo ajusta. |
| **Resolución de Render** | 20% – 100% con DLSS, 33% – 100% con FSR y XeSS | 50% | Solo en el modo **Personalizado**: qué parte de la resolución de pantalla se traza antes de que el escalador la amplíe. Menos es más rápido y más suave. Elegir un nivel de Calidad u otro modo lo deja. |
| **Resolución de Salida** | Nativa · 1440p · 1080p · 900p · 720p · Personalizada (solo salen los tamaños menores que la ventana) | Nativa | El tamaño al que se renderiza y escala el mundo antes de que un escalado final FSR 1 llene la ventana. El HUD y el texto siguen nítidos. En una pantalla 4K es donde más se ahorra. |
| **Escala de Salida** | 25% – 99% | 75% | Solo con Resolución de Salida en Personalizada: qué parte del tamaño de la ventana se renderiza. Muestra el tamaño que resulta. |
| **Tipo de Generación de Fotogramas** | Auto · DLSS · FSR | Auto | Aparece donde la GPU soporta ambos. Auto elige DLSS si la GPU lo soporta, FSR si no. FSR funciona con cualquier escalador pero solo duplica. |
| **Generación de Fotogramas** | Apagado · 2x … 4x | Apagado | Fotogramas extra entre los renderizados. Oculto con Salida HDR. Ver [frame-generacion-reflex.md](../frame-generacion-reflex.md). |
| **NVIDIA Reflex** | Activado / Desactivado | Desactivado | Baja latencia. Solo NVIDIA con `VK_NV_low_latency2`; oculto en otras GPUs. Sin reinicio. |

## Los pipelines

- **RT-DLSS**: con Reconstrucción de Rayos activada, DLSS hace denoising y escalado a la vez, el de
  menos ruido; desactivada, NRD hace el denoising y DLSS escala. Solo NVIDIA, se descarga en el
  juego.
- **RT-FSR**: denoiser NRD (RELAX) y FSR 3. Cualquier GPU, viene incluido. Es el valor por
  defecto si DLSS no está instalado.
- **RT-XeSS**: NRD y XeSS. Windows, se descarga en el juego. Recomendado en Intel Arc.
- **RT-Nativo**: NRD sin escalador, a resolución nativa. Cualquier GPU; el más caro.

## Elegir un pipeline no instala nada solo

Elegir `RT-DLSS (Instalar)` pone **Instalar DLSS (115 MB)** en la barra de abajo. Instalar o
borrar se hace fuera de un mundo y termina en un reinicio. En el siguiente inicio Radiante pregunta
si quieres cambiar al pipeline de DLSS (o de XeSS); también puedes elegirlo aquí después. Ver
[Escaladores](../escaladores.md).
