# The settings menu: where a setting goes

The menu is built in `SettingsRows` (rows), `AdvancedOptions` (the pipeline's own attributes) and laid out by
`SettingsLayout`. This page is the rule book for adding a setting, so the menu stays short for players who never
open Advanced and predictable for those who do.

## The shape of a section

Each section (`RadianteOptionsScreen.Category`) has three layers, in this order:

1. **Top rows**, with no heading: the two to four things most players come to the section for
   (Ray Tracing and Quality; the four brightness sliders). Added with `addRows(...)` before any `group(...)`.
2. **Groups**, each under a heading that folds (`group("name")`):
   - `addRows(...)`: rows anyone may want. Plain words, an effect you can see, safe at any value.
   - `addAdvanced(...)`: rows for tuning, shown a step in and only while Advanced is on.
   - A group with only advanced rows does not exist without Advanced. A search still counts what it holds
     ("N more in Advanced").
3. **Pipeline attribute groups** (`AdvancedOptions.place`): every module attribute with no row of its own, advanced only.

Nothing is ever removed from the menu to simplify it: it moves to `addAdvanced`.

## The sections

| Section | What belongs there | Groups |
|---|---|---|
| Quality | What decides how the frame is produced: ray tracing, the quality level, upscaling, frame generation | Upscaling, Frame Generation |
| Image | How the finished picture looks: style, exposure, tone, HDR, camera effects | Tone & Exposure, HDR, Camera Effects |
| Lighting | How bright things are and what casts light | Player, Light Sources |
| Sky & Fog | Everything above the ground and in the air: sky, sun and moon, stars, clouds, fog | Sky, Clouds, Fog |
| Water & Weather | What the weather does to the world, and water | Rain, Snow, Water, Wind |
| Performance | Settings whose point is speed, not look | Materials, Ray Tracing, World Building |
| Other | Interface and compatibility, and what fits nowhere else | Glass & Portals |
| Debug | Developer tools; only with Advanced on | none |

## Adding a setting

1. **Pick the section by what the player sees change**, not by which module implements it. A look setting never
   goes in Performance because it happens to cost frames: the tooltip's performance impact says that.
2. **Pick the group**, or add one when three or more rows share a subject none of the groups covers. A new group
   needs `options.radiante.group.<name>` in all three lang files.
3. **Basic or advanced?** Basic only if all of these hold:
   - a player with no rendering knowledge understands the name;
   - the change is visible in the world within seconds;
   - no value of it breaks the picture or needs another setting changed first.
   Otherwise it is advanced. An on/off switch is basic, its fine tuning (strength curves, times, sample counts,
   scopes) is advanced. When in doubt, advanced.
4. **Keep a basic group to about five rows.** If a sixth basic row is needed, one of the six is probably advanced.
5. **Order inside a group**: the on/off switch first, then its main strength, then the rest from most to least used.
6. **Never put a row at the top of a section** unless it replaces one there; the top is not a place for new things.
7. Lang keys in `en_us`, `es_es` and `zh_cn`; a tooltip for every row; the row in `docs/en/settings` and
   `docs/es/opciones`; an entry in `OptionImpact` if it costs frames.

## Checking it

`section=<CATEGORY>` in a dev script opens the menu on a section; `opt=advancedSettings=false|true` switches the
mode. Take a capture of the section in both modes and read it as a player would: without Advanced a section should
fit on one screen without scrolling.
