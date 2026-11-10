[← Volver al índice](README.md)

# HDR

En una pantalla HDR, la luz del sol, la lava, las lámparas y los reflejos brillan más que el blanco en vez de
quedarse en un blanco plano.

## Activarlo

1. Activa HDR en Windows: Configuración → Sistema → Pantalla → HDR, para tu monitor. Que el monitor soporte HDR no
   basta: Windows tiene que estar usándolo.
2. En Radiante, activa **Salida HDR** (sección Imagen).
3. **Reinicia el juego.** Hasta entonces el ajuste dice que está activado pero aún no funciona.

| Control | Rango | Por defecto | Se aplica |
|---|---|---|---|
| Salida HDR | Sí / No | No | Tras reiniciar |
| Brillo Máximo HDR | 400 – 4000 nits | 1000 | Al momento |
| Blanco de Papel HDR | 80 – 400 nits | 200 | Al momento |

- **Brillo Máximo:** lo más brillante que puede mostrar tu pantalla. Usa la especificación del monitor o la
  Calibración HDR de Windows. El sol y la lava de cerca se ajustan por debajo.
- **Blanco de Papel:** qué tan brillantes se ven los menús, el texto y las superficies normales. En la práctica, el
  brillo general: súbelo en una habitación con luz, bájalo a oscuras.

> [!NOTE]
> La generación de fotogramas no está disponible con Salida HDR activada.

## Calibrar HDR

**Calibrar HDR**, bajo Salida HDR cuando HDR ya funciona, ajusta los mismos dos valores con imágenes en vez de
números:

1. Sube el máximo hasta que desaparezcan las marcas del cuadrado.
2. Elige el blanco de papel sobre dos capturas.
3. Revisa el resultado, luego **Listo** y **Aplicar**.

<!-- Captura pendiente: las tres páginas de Calibrar HDR -->

## Comprobar que HDR funciona de verdad

**Vista de Depuración HDR** (sección Depuración, con Avanzado activado) tiñe de azul todo lo que se muestra en rango
SDR. Lo que conserva su color se está mostrando en HDR real. No se guarda y se apaga al siguiente arranque.

<!-- Captura pendiente: la misma escena con una luz intensa, HDR activado y desactivado. Una foto de la pantalla capta la diferencia mejor que un PNG, que se ve en SDR. -->
