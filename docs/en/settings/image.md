[← Settings](../settings.md) · [Index](../README.md)

# Image

![Radiante settings: Image](../../images/ui/settings-image.png)

Defaults are radiante-pt's, the Bedrock RTX look ([bedrock-rtx.md](../bedrock-rtx.md)).

| Control | Range / Values | Default | What it does |
|---|---|---|---|
| **Picture Style** | Natural · Bedrock · Vivid · Custom | Bedrock | Sets Saturation, Bounced Light, Sky Light and the tone curve at once. Bedrock is the default look. Full detail in [styles-and-quality.md#picture-styles](../styles-and-quality.md#picture-styles). |
| **Tone Mapping** | Bedrock RTX · PBR Neutral · ACES · Reinhard | Bedrock RTX | How bright light is fitted to what the screen can show. Bedrock RTX is Bedrock's own curve, built from the picture's histogram; PBR Neutral keeps colours true; ACES is more filmic with more contrast; Reinhard is soft and flat. |
| **Saturation** | 50% – 200% | 100% | How colourful the image is. |
| **Texture Contrast** | 46% – 109% | 100% | How deep and contrasted block and entity colours look under the light. 100% matches Bedrock RTX; lower gives the paler, flatter look of earlier versions. |
| **Eye Adaptation** | 0% – 100% | 100% | How far the exposure adapts to what you look at. Lower keeps dark rooms dark and daylight bright; 100% always evens the image out. |
| **Eye Adaptation Speed** | 5% – 250% | 100% | How quickly the exposure follows when you walk from the dark into the light or back. Lower lets you see your eyes adjust for a moment; 100% is close to instant. |
| **Exposure** | -3.0 EV – +3.0 EV | +0.0 EV | Overall brightness of the image, in photographic stops. |
| **Low-Light Boost** | 1% – 200% | 100% | How much the exposure may brighten a dark scene. Higher lets you see more in caves and at night; lower keeps the dark dark. 100% is Bedrock RTX's limit. |
| **Daytime Exposure** | 10% – 150% | 48% | How bright the picture is kept by day, against what the light meter alone would make it. Lower is darker. |
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

With the Bedrock RTX tone curve (the default) the tone mapper is Bedrock-style (a curve built from the picture's
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

HDR Debug View is in the [Debug](debug.md) section. Turning HDR Output on or off needs a game restart; the brightness sliders apply with the rest of
the settings, without one.
