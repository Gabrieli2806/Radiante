[← Back to index](README.md)

# HDR

On an HDR display, sunlight, lava, lamps and reflections go brighter than white instead of clipping to flat white.

## Turn it on

1. Turn HDR on in Windows: Settings → System → Display → HDR, for your monitor. A monitor that supports HDR is not
   enough: Windows has to be using it.
2. In Radiante, turn on **HDR Output** (Image section).
3. **Restart the game.** Until then the setting says it is on but not running yet.

| Control | Range | Default | Applies |
|---|---|---|---|
| HDR Output | On / Off | Off | After a restart |
| HDR Peak Brightness | 200 – 4000 nits | 1000 | At once |
| HDR Paper White | 80 – 400 nits | 200 | At once |

- **Peak Brightness:** the brightest your display can show. Use your monitor's specification or Windows' HDR
  Calibration. The sun and lava up close are fitted under it.
- **Paper White:** how bright menus, text and ordinary surfaces look. In practice, the overall brightness: raise it
  in a bright room, lower it in a dark one.

> [!NOTE]
> Frame generation is not available while HDR Output is on.

## Calibrate HDR

**Calibrate HDR**, under HDR Output once HDR is running, sets the same two values with pictures instead of numbers:

1. Raise the peak until the marks in the square disappear.
2. Choose paper white on two screenshots.
3. Check the result, then **Done** and **Apply**.

<!-- Screenshot pending: the three pages of Calibrate HDR -->

## Check that HDR is really on

**HDR Debug View** (Debug section, with Advanced on) tints blue everything shown in SDR range. Whatever keeps its
colour is being shown in real HDR. It is not saved and turns off at the next start.

<!-- Screenshot pending: the same scene with an intense light source, HDR on vs. off. A photo of the screen captures the difference better than a PNG, which displays in SDR. -->
