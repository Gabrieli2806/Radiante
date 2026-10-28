# Iris and OptiFine packs: converting their look

Experimental (local branch `dev/iris-look`). Iris and OptiFine packs cannot run in Radiante, but their **look** can
be carried over onto Radiante's light.

## Why they cannot run

An Iris pack draws the world with rasterised passes: `gbuffers_*` programs shade every block as it is drawn, with
shadow maps, screen-space reflections and ambient occlusion, into a dozen colour buffers whose layout each pack
invents; `deferred` and `composite` passes then read those buffers. A path tracer has no such buffers and no draw of
each block: emulating them means running the pack's raster pipeline beside the path tracer, which throws Radiante's
light away. Their licences also keep their code out of a GPL pack (Complementary's allows changed copies only under
its own licence, and nothing else).

## What can be carried over

Their **options**: the values a player sets in Iris's menu. A pack lists them as `#define NAME value //[choices]`
lines; Iris keeps the player's choices in `shaderpacks/<pack>.txt`. Those values say how strong the fog is, how
saturated the picture, how murky the water, which clouds. Radiante reads them and writes a pack of its own:

- `extends: radiante-pt@1`, so the light, shadows, reflections and global illumination stay Radiante's;
- Radiante's own hook files (`assets/radiante/iris_look/`): a colour grade (saturation, dark desaturation, warmth,
  contrast, vignette), fog strength, sky saturation, water murkiness, each a setting;
- new defaults for Radiante PT settings (fog scattering, wave strength, cloud mode and coverage).

No line of the raster pack is run or copied.

## Profiles

A profile maps one pack's options to those values (`IrisLookImport.Profile`). Any other pack gets the neutral look
(nothing changed), to be tuned in Radiante's settings.

| Profile | Found by | Options read |
| --- | --- | --- |
| Complementary (Reimagined, Unbound) | Name, or its licence file | `T_SATURATION`, `TM_DARK_DESATURATION`, `ATM_FOG_MULT`, `ATM_FOG_DISTANCE`, `WATER_FOG_MULT`, `WATER_BUMPINESS`, `CLOUD_STYLE_DEFINE`, `CLOUD_UNBOUND_AMOUNT` |

The style each profile starts from (Complementary: vivid but soft colours, warm light, greyer shadows, light haze) is
Radiante's own reading of the pack's look, not taken from its code.

## Using it

Shader Packs screen: Iris packs in the game's `shaderpacks/` folder (and any put in `radiante/shaderpacks/`) are
listed as Iris / OptiFine. Pick one, **Convert Look**: Radiante writes `radiante/shaderpacks/<pack>-radiante-look/`
and switches to it. Converting again after changing the pack's settings in Iris rewrites it.

## Limits and next steps

- Only three live settings fit: Radiante PT already uses nearly all 64 live slots, so water murkiness and the grade
  rebuild when changed.
- No cloud colour hook yet (Complementary's `CLOUD_R/G/B`), no bloom strength, no sun and block light colours: these
  need hooks or settings in Radiante PT first (roadmap phase 3).
- More profiles (BSL, Rethinking Voxels, Photon...) are a table of options each.
