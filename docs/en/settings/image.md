[← Settings](../settings.md) · [Index](../README.md)

# Image

![Radiante settings: Image](../../images/ui/settings-image.png)

Defaults are what the default profile, **Bedrock RTX (Enhanced)**, sets; where the **Realistic**
profile sets something else, its value follows in brackets.

| Control | Range / Values | Default | What it does |
|---|---|---|---|
| **Renderer Profile** | Bedrock RTX (Enhanced) · Realistic · Custom | Bedrock RTX (Enhanced) | **Bedrock RTX (Enhanced)**: Bedrock RTX's own sky and sunlight, exposure metered on the light reaching the surfaces in view, Bedrock's histogram tone curve and bloom, exact sRGB colours, Bedrock's glass and water reflections, sunlight filtered by glass, Bedrock's fog volume and denser water that fogs out the distance under water; on top, light blocks with little glow in the pack look lit by their own light and End portals glow. **Realistic** is the same with everything that makes a picture look like plastic set the other way: less ambient and bounced fill (shadows keep their depth), slightly more contrast, calmer colour (92 % saturation), almost no bloom, light shafts and sun glow at half strength, finer irregular water waves, clearer teal water, only polished metal as a clean mirror, and an eye that adapts more slowly. It is darker than Enhanced on purpose. **Custom** appears once a setting a profile manages is changed by hand; pick a profile again to restore its values. Applied last: it overrides the matching settings changed in the same visit. More in [bedrock-rtx.md](../bedrock-rtx.md). |
| **Picture Style** | Natural · Bedrock · Vivid · Custom | Custom | Sets Saturation, Bounced Light and Sky Light at once. The profiles' values match none of the styles, so it shows Custom; picking a style turns the profile into Custom. Full detail in [styles-and-quality.md#picture-styles](../styles-and-quality.md#picture-styles). |
| **Tone Mapping** | PBR Neutral · ACES · Reinhard | — | How bright light is fitted to what the screen can show. PBR Neutral keeps colours true; ACES is more filmic with more contrast; Reinhard is soft and flat. Hidden while the profiles' Bedrock tone curve is in use (it can be changed under Advanced). |
| **Saturation** | 50% – 200% | 100% (92%) | How colourful the image is. |
| **Texture Contrast** | 46% – 109% | 100% | How deep and contrasted block and entity colours look under the light. 100% matches Bedrock RTX; lower gives the paler, flatter look of earlier versions. |
| **Eye Adaptation** | 0% – 100% | 100% | How far the exposure adapts to what you look at. Lower keeps dark rooms dark and daylight bright; 100% always evens the image out. |
| **Eye Adaptation Speed** | 5% – 250% | 100% (50%) | How quickly the exposure follows when you walk from the dark into the light or back. Lower lets you see your eyes adjust for a moment; 100% is close to instant. |
| **Exposure** | -3.0 EV – +3.0 EV | +0.0 EV | Overall brightness of the image, in photographic stops. |
| **Low-Light Boost** | 25% – 400% | 400% | How much the exposure may brighten a dark scene. Higher lets you see more in caves and at night; lower keeps the dark dark. The profiles set 1600%, past the end of the slider, which then shows 400%. |
| **Daytime Exposure** | 10% – 150% | 48% (42%) | How bright the picture is kept by day, against what the light meter alone would make it. Lower is darker. |
| **Night-time Exposure** | 10% – 150% | 65% | The same at night. |
| **Daytime Low-Light Limit** | 50% – 3200% | 250% | How far the exposure may open by day in a dark place. Low keeps closed rooms dark; high brightens rooms lit only through a window. |
| **FSR Sharpness** | 0% – 100% | 70% | How much FSR sharpens the upscaled image. Higher is crisper but can show halos on edges. RT-FSR only. |
| **HDR Output** | On / Off | Off | HDR on an HDR display; the brightness controls below it show when it is on. See [HDR](#hdr). |
| **Motion Blur** | On / Off | Off | Smears what moves across the screen along its motion, as a camera does. Changing it rebuilds the shaders (a brief hitch when applying). |
| **Depth of Field** | On / Off | Off | Keeps what you look at sharp and blurs what is much nearer or farther, focusing on the centre of the screen. Also rebuilds the shaders when changed. |
| **Motion Blur Strength** | 10% – 220% | 100% | How long the smear of Motion Blur is. Needs Motion Blur. |
| **Depth of Field Blur** | 25% – 400% | 100% | How strongly what is out of focus blurs. Needs Depth of Field. |
| **Depth of Field Focus Range** | 10% – 400% | 100% | How much around what you look at stays sharp. Lower blurs sooner, for a close-up look. |
| **Auto Focus** | On / Off | On | Depth of Field focuses on whatever is at the centre of the screen. Off keeps the focus at Focus Distance, for screenshots. |
| **Focus Distance** | 1 – 256 blocks | 12 | How far away the image is sharp when Auto Focus is off. |

With the Bedrock RTX profiles the tone mapper is Bedrock-style (a curve built from the picture's
histogram) and the exposure is metered on the light reaching the surfaces in view, so the exposure
sliders behave a little differently than with the PBR Neutral, ACES or Reinhard curves.

## HDR

HDR Output sits in this same section, between FSR Sharpness and Motion Blur. It is covered in full,
with display considerations, in [hdr.md](../hdr.md) — quick summary:

| Control | Range | Default |
|---|---|---|
| HDR Output | On/Off | Off |
| HDR Peak Brightness | 400 – 4000 nits | 1000 nits |
| HDR Paper White | 80 – 400 nits | 200 nits |
| HDR Debug View | On/Off | Off (not saved) |

Turning HDR Output on or off needs a game restart; the brightness sliders apply with the rest of
the settings, without one.
