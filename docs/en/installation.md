[← Back to index](README.md)

# Installation

Check the [requirements](requirements.md) first: GPU, system and loader version. Radiante is
client-only: the server does not need it, and joining a server without it works the same.

There is one jar per loader (`radiante-fabric-…`, `radiante-neoforge-…`, `radiante-forge-…`), and
each works on Windows and Linux. Get it from [Modrinth](https://modrinth.com/project/radiante),
[CurseForge](https://www.curseforge.com/minecraft/mc-mods/radiante) or
[GitHub](https://github.com/Gabrieli2806/Radiante/releases).

## Fabric

1. Install [Fabric Loader](https://fabricmc.net/use/) `0.19.5` or newer for Minecraft 26.3.
2. Put **Fabric API** for 26.3 in `mods` (Radiante is built against `0.162.0+26.3`).
3. Put the Radiante Fabric jar in `mods`.
4. Start the Fabric profile.

## NeoForge

1. Install [NeoForge](https://neoforged.net/) `26.3.0.10-beta` or newer.
2. Put the Radiante NeoForge jar in `mods`.
3. Start the NeoForge profile.

## Forge

1. Install [Forge](https://files.minecraftforge.net/) `26.3-66.0.3` or newer.
2. Put the Radiante Forge jar in `mods`.
3. Start the Forge profile.

## Minecraft 26.2 and 26.4

The same steps work for the 26.2 build (Fabric Loader 0.19.5+ with Fabric API, NeoForge
26.2.0.93+ or Forge 26.2-65.1.5+) and for the 26.4 snapshot build, which is Fabric only (Fabric
Loader 0.20.1+ with Fabric API). Pick the file for your Minecraft version.

## Which loader

Whichever your modpack uses. All three run the same code, frame generation and Reflex included.
Fabric is the most tested; NeoForge and Forge share the code but have been tested less.

## On Linux

Same jar, same steps. If your launcher is a Flatpak, read [Linux](linux.md) first: the sandbox has
to see your GPU's driver.

## First start

### The renderer is unpacked

The first time, Radiante unpacks its native libraries into the game's `radiante/` folder and
checks each against its SHA-256. It takes a moment; later starts only repeat it when the version
changed. A few seconds after the title screen shows, it also compiles its shaders and ray tracing
pipelines in the background, with a popup in the top right corner showing the progress; this only
happens again after an update or a new graphics driver.

### DLSS or XeSS is offered

FSR is built in and is the default upscaler. On the title screen, an NVIDIA GPU gets a window
offering DLSS (~115 MB) and an Intel GPU on Windows, XeSS (~73 MB). You can download it, leave it
for later, or never see it again. Details in [Upscalers](upscalers.md).

### The game has to be on Vulkan

Minecraft lets you pick the graphics API. Radiante needs Vulkan. If the game starts on OpenGL,
a screen explains it and offers **Switch to Vulkan**, then a restart. See
[Common problems](problems.md#the-game-is-on-opengl).

### If your GPU has no ray tracing

You will see "Radiante: ray tracing unavailable" and the game carries on with Minecraft's renderer.
This also happens with software Vulkan (`llvmpipe`). See
[Common problems](problems.md#my-gpu-does-not-support-ray-tracing).

<!-- Screenshot pending: the full "Radiante: ray tracing unavailable" screen -->

### If all goes well

Radiante turns on when you enter a world, with the Bedrock RTX (Enhanced) profile. Default pipeline:
RT-DLSS if DLSS is already installed, otherwise RT-FSR.

### Keys

| Key | Action |
|---|---|
| `F6` | Opens Radiante's settings. |
| Toggle Ray Tracing (unbound) | Turns ray tracing on or off instantly. Give it a key in Minecraft's controls. |

Both are in Minecraft's controls, in Radiante's own category, and can be rebound there.

## After installing

- [Settings](settings.md): the settings screen.
- [Bedrock RTX](bedrock-rtx.md): resource packs with more detail.
- [Common problems](problems.md) if something does not start or looks wrong.
