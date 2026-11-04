# Aseprite KARN Mods

Authored by Justin Schwartzbauer and ChatGPT

KARN MODS adds palette-bank editing, controlled shading, and KarnEdit exports to Aseprite. This guide covers the custom controls and the workflow for preparing and updating game artwork.

This guide is also available in **Help > ABOUT KARN MODS** inside the editor.

## User guide

- [Quick start](#quick-start)
- [Controlled shading](#controlled-shading)
- [Tool presets](#tool-presets)
- [Palette banks](#palette-banks)
- [VERA colors and KARN rules](#vera-colors-and-karn-rules)
- [Full color Reference](#full-color-reference)
- [Converting RGB artwork](#converting-rgb-artwork)
- [Export](#export)
- [Saving and compatibility](#saving-and-compatibility)
- [Troubleshooting](#troubleshooting)
- [About Aseprite](#about-aseprite)

## Quick start

- Work in an Indexed document with transparent index 0.
- Open Palette Banks with PB, or use Layer > Properties to assign numbered banks to your artwork layers.
- Name banks to suit the asset, such as Hull, Interior, or Effects. Select a layer to edit and paint with its bank.
- Enable File > Export > KARN > Force KARN rules when you want the document kept within the game's color and tile limits.
- Export through Export layers as 4bpp BMPs or Export to Karnedit in the same KARN menu.

Save the editable source as .aseprite. Save again after configuring exports to retain the layer mappings and output paths for future updates. Save and Save As continue to work normally.

## Controlled shading

Select the Pencil tool with B, choose Shading Ink, and configure the ramp using Aseprite's existing shade controls. The separate shading-mode dropdown changes how repeated brush coverage affects a pixel:

- Original: standard Aseprite behavior. Overlapping brush stamps can advance a pixel several shades during one stroke.
- Progressive Shading: each new brush stroke advances one shade.
- Once Per Stroke: each pixel advances at most one shade while the button is held. Release and begin another stroke for the next step.

Progressive Shading suits gradual buildup and repeated sculpting passes. Once Per Stroke gives a consistent single-step adjustment across a broad sweep. Both support darkening and lightening through the existing mouse-button behavior.

Progressive Shading is the default.

Indexed shading follows the selected ramp without changing palette indices through color matching. Spray retains its own distribution controls; use spray width and speed to adjust the marks.

## Tool presets

Open View > Tool Presets to save and recall your favorite tool and brush settings. The panel docks below the palette or floats in its own window, and remembers its placement.

Click **+** to capture your current settings, name the preset, and choose Save New. Click a preset to use it, **E** to edit it, or the trash button to delete it. The highlight clears when your tool or settings no longer match the preset.

Each preset shows its brush size and saved colors, including the full ramp for shading presets. Enable Ignore colors in the preset dialog to keep your current colors when recalling it; a crossed-out swatch marks this option.

To replace a preset's colors, select it, choose your new colors or ramp, then click Update color(s). This updates only the last preset you chose, even if its highlight has cleared. Presets remain available across documents and sessions.

## Palette banks

Use palette banks to work with several 4bpp palettes in one Indexed document. Both ordinary image layers and tilemap layers can have a bank assignment, using local indices 0-15 with index 0 transparent.

Open Palette Banks with the **PB** button. Layers and palettes share one scrollable page. Rename layers, choose their banks, and use **Current** to select the layer you want to work on. **Visible** shows or hides a layer; **Locked** protects it from painting. Below the layers, review the document palette and name each bank. Edits apply immediately and can be undone. You can also assign a bank through Layer > Properties.

Select a layer to paint with and edit its palette in the color bar. Layers assigned to the same bank share its colors. Choose Document Palette to leave a layer unbanked; only numbered banks are included in KARN exports.

Document Palette and Bank 00 have separate colors. Pasting or importing a 16-color palette into one leaves the other unchanged.

A bank that has never been assigned starts NEW. Its first assignment copies the layer's previous palette colors. If a previously used bank has no other layers assigned, choose Copy Colors or Keep Bank when assigning it again. Banks already shared by other layers keep their colors.

Use the **x** beside an unused bank's swatches to clear its colors and reset it to NEW. Confirm the prompt to clear all frames; the bank keeps its name. Assigned banks cannot be cleared, and Undo restores a cleared bank.

Import a palette with a banked layer selected to update only that bank. To copy colors between banks, select and copy the source swatches, select the destination layer and swatches, then paste. Under Force KARN rules, each palette shown in the color bar stays at 16 slots.

Palette move/copy commands also use the selected bank. Copying replaces destination swatches within its 16 slots. Smooth resizing matches colors within each bank and separates shared tilesets when their banks need different results; nearest-neighbor resizing keeps them shared. New Sprite from Selection and Lua cel-image exports retain local indices and their bank colors. Copy Merged preserves composed bank colors and full-color reference pixels, including alpha.

After rearranging or replacing colors, click **Remap Palette** to match the artwork to its previous colors. With a numbered bank selected, this remaps all frames of every layer using that bank, including hidden and locked layers. Document Palette remaps unbanked indexed layers. Full color Reference pixels stay unchanged, and transparent slot 0 stays fixed for numbered banks. Shared tilesets are separated when needed so other banks keep their pixels and tile IDs. Exact matches preserve appearance; colors missing from the new palette use the nearest match. Remapping is a separate undo step from the palette edit. Switching banks or frames clears the pending remap.

The Eyedropper and **Alt-pick** sample the visible artwork across layers and select an exact match in the active layer's palette bank. A small red X means that color is unavailable; your selected color stays unchanged.

## VERA colors and KARN rules

The 4-bit checkbox beside the palette Options button enables Force 4 bit color channels for the document. It constrains all banks to VERA-compatible RGB444 colors, including subsequent palette edits and imports. Pixel indices and alpha are preserved.

File > Export > KARN > Force KARN rules applies the broader production constraints:

- Indexed artwork using local indices 0-15, with index 0 transparent.
- Fully opaque RGB444 palette colors across all banks. Index 0 remains transparent in artwork, while its color is visible in the palette.
- 8 x 8 tiles and grid, with H, V, and combined H+V flipping allowed.

Aseprite's diagonal flip is a transpose, not combined H+V, and is excluded. New and converted tilemap layers use the enforced settings automatically.

Unbanked layers remain allowed. Under these rules they use the first 16 palette entries and stay outside KARN exports. Full color Reference layers retain full-color editing.

Enabling the rules on incompatible artwork opens a confirmation prompt. Applying may reduce colors, remap excess indices, convert backgrounds, or rebuild tilemaps. The initial conversion is one undo step. Rebuilt tile IDs can change, so review converted maps before committing the source file.

While enabled, incompatible edits are rejected. Disable Force KARN rules when the document needs other settings. The separate 4-bit color option remains enabled until you turn it off independently.

## Full color Reference

Assign Full color Reference to an ordinary image layer for sketches, source images, paint studies, or guides within an Indexed document. Painting, alpha, fills, filters, selections, and transforms behave as they do in a full-color document.

Selecting another layer restores that layer's indexed editing behavior. KARN rules and RGB444 constraints do not reduce the reference layer's pixels.

Full color Reference is a bank assignment, separate from Aseprite's built-in Reference layer type. Convert tilemap layers to ordinary layers before assigning it. Existing indexed artwork expands through its current palette when assigned to Full color Reference.

These layers are excluded from KARN BMP, TileKit, and collage exports. They remain visible in composed PNG and sprite-sheet exports unless hidden.

## Converting RGB artwork

Pasting RGB artwork into an indexed layer, or changing a Full color Reference layer to an indexed bank, opens Aseprite's Color Mode conversion options automatically.

Choose the dithering method while watching the live canvas preview. Floyd-Steinberg exposes an Amount control; Advanced Options provides the RGB-to-index mapping and best-fit choices. Conversion uses the destination layer's palette.

The preview is limited to the incoming image or converted layer. Other artwork remains visible for judging the result in context.

- For a paste, OK continues into the normal movable selection so you can position or transform the result.
- For a bank change, OK converts all frames of that layer and retains linked cels. The current frame supplies the preview.
- Cancel retains the original artwork and bank assignment.

Accepted conversions remain undoable. Pasting into Full color Reference keeps normal RGB behavior and does not require indexed conversion.

## Export

Start by assigning numbered palette banks to the artwork layers you want to export. The workflow is BMPs first, then TileKits, with an optional collage to carry the scene layout into KarnEdit.

1. **Export the BMPs.** Open File > Export > KARN > Export layers as 4bpp BMPs. Choose your layers, BMP output folder, and filenames. Enable Merge layers sharing a palette bank if you want those layers combined into one BMP. Click Export.

2. **Set up the TileKits.** Open File > Export > KARN > Export to Karnedit. Choose your game workspace, TileKit folder, and a JSON destination for each BMP. Each BMP gets its own TileKit, which references that image. BMPs and TileKits can live in separate folders. If any BMPs are missing or out of date, the shared BMP export window opens first so you can update them.

3. **Include a collage if needed, then export.** Export Collage becomes available when visible artwork layers use multiple numbered palette banks, and is checked by default. Leave it checked and choose the collage JSON destination to export the scene layout. Clear it for TileKits only. Click Export to write the files.

For later updates, keep the same destinations and confirm the overwrite prompt. Choose new paths to create separate resources. Save your .aseprite file after setting up exports so it remembers the choices for next time.

KarnEdit treats indices 1 and 2 as transparent on import. Export preserves those indices and warns if they are used.

## Saving and compatibility

Use .aseprite for editable sources. The file retains bank assignments, names, KARN settings, full-color reference artwork, and export mappings.

Standard Aseprite does not apply the custom bank assignments or enforce KARN rules. Banked artwork will display through the first palette bank there, so use this build when editing these sources.

Full color Reference layers include an indexed fallback for standard Aseprite. Saving in standard Aseprite discards the original full-color data. Retain the custom-build source when exchanging files.

PNG and sprite-sheet exports compose visible layers using their assigned banks. Hide full-color guides if they should not appear in the output. Export Tileset uses the selected layer's bank.

Global color-mode changes are restricted while bank assignments exist. Export a composed image for conversion, or remove assignments first. Disable Force KARN rules before changing the document away from Indexed mode.

## Troubleshooting

- Missing export layers: check for a numbered bank assignment. Collages also require the layer and its parent groups to be visible.
- Export Collage unavailable: at least two distinct banks must be used by visible layers. TileKit export remains available.
- Progressive shading does not advance: move the brush away before passing over the area again.
- Unexpected collage transparency: unmerged sources replace entire tiles. Merge same-bank BMP sources when pixel compositing is intended.
- Missing colors in KarnEdit: inspect reserved indices 1 and 2 and review the export warning.
- Rejected edits: check Force KARN rules against the document's indices, color mode, and tile settings.
- Updating existing assets: retain their BMP and JSON destinations. Choose unused paths when the output should be a separate resource.

---

## About Aseprite

[![build](https://github.com/aseprite/aseprite/actions/workflows/build.yml/badge.svg)](https://github.com/aseprite/aseprite/actions/workflows/build.yml)
[![Translation Status](https://hosted.weblate.org/widget/aseprite/aseprite/svg-badge.svg)](https://hosted.weblate.org/engage/aseprite/)
[![Discourse Community](https://img.shields.io/badge/discourse-community-brightgreen.svg?style=flat)](https://community.aseprite.org/)
[![Discord Server](https://discordapp.com/api/guilds/324979738533822464/embed.png)](https://discord.gg/Yb2CeX8)

### Introduction

**Aseprite** is a program to create animated sprites. Its main features are:

* Sprites are composed of [layers &amp; frames](https://www.aseprite.org/docs/timeline/) as separated concepts.
* Support for [color profiles](https://www.aseprite.org/docs/color-profile/) and different [color modes](https://www.aseprite.org/docs/color-mode/): RGBA, Indexed (palettes up to 256 colors), Grayscale.
* [Animation facilities](https://www.aseprite.org/docs/animation/), with real-time [preview](https://www.aseprite.org/docs/preview-window/) and [onion skinning](https://www.aseprite.org/docs/onion-skinning/).
* [Export/import](https://www.aseprite.org/docs/exporting/) animations to/from [sprite sheets](https://www.aseprite.org/docs/sprite-sheet/), GIF files, or sequence of PNG files (and FLC, FLI, JPG, BMP, PCX, TGA).
* [Multiple editors](https://www.aseprite.org/docs/workspace/#drag-and-drop-tabs) support.
* [Layer groups](https://imgur.com/x3OKkGj) for organizing your work, and [reference layers](https://twitter.com/aseprite/status/806889204601016325) for rotoscoping.
* Pixel-art specific tools like [Pixel Perfect freehand mode](https://imgur.com/0fdlNau), [Shading ink](https://www.aseprite.org/docs/shading/), [Custom Brushes](https://twitter.com/aseprite/status/1196883990080344067), [Outlines](https://twitter.com/aseprite/status/1126548469865431041), [Wide Pixels](https://imgur.com/1yZKUcs), etc.
* Other special drawing tools like [Pressure sensitivity](https://twitter.com/aseprite/status/1253770784708886533), [Symmetry Tool](https://twitter.com/aseprite/status/659709226747625472), [Stroke and Fill](https://imgur.com/7JZQ81o) selection, [Gradients](https://twitter.com/aseprite/status/1126549217856622597).
* [Tiled mode](https://youtu.be/G_JeWBaxQIg) useful to draw patterns and textures.
* [Transform multiple frames/layers](https://twitter.com/aseprite/status/1170007034651172866) at the same time.
* [Lua scripting capabilities](https://www.aseprite.org/docs/scripting/).
* [CLI - Command Line Interface](https://www.aseprite.org/docs/cli/) to automatize tasks.
* [Quick Reference / Cheat Sheet](https://www.aseprite.org/quickref/) keyboard shortcuts ([customizable keys](https://imgur.com/rvAUxyF) and [mouse wheel](https://imgur.com/oNqFqVb)).
* [Reopen closed files](https://twitter.com/aseprite/status/1202641475256881153) and [recover data](https://www.aseprite.org/docs/data-recovery/) in case of crash.
* Undo/Redo for every operation and support for [non-linear undo](https://imgur.com/9I42fZK).
* [More features &amp; tips](https://twitter.com/aseprite/status/1124442198651678720)

### Issues

There is a list of
[Known Issues](https://github.com/aseprite/aseprite/issues) (things
to be fixed or that aren't yet implemented).

If you found a bug or have a new idea/feature for the program,
[you can report them](https://github.com/aseprite/aseprite/issues/new).

### Support

You can ask for help in:

* [Aseprite Community](https://community.aseprite.org/)
* [Aseprite Discord Server](https://discord.gg/Yb2CeX8)
* Official support: [support@aseprite.org](mailto:support@aseprite.org)
* Social networks and community-driven places:
  [Twitter](https://twitter.com/aseprite/),
  [Facebook](https://facebook.com/aseprite/),
  [YouTube](https://www.youtube.com/user/aseprite),
  [Instagram](https://www.instagram.com/aseprite/).

### Credits

Aseprite was originally created by [David Capello](https://davidcapello.com/)
and is now being developed and maintained by [Igara Studio](https://igara.com/)
and contributors.

Check the [AUTHORS](AUTHORS.md) file for details about the active team
of developers working on Aseprite.

### License

Aseprite's maintainer has
[clarified that public source forks, including modified source, are permitted](https://github.com/aseprite/aseprite/issues/2266).
Keep the original license and copyright notices. **Do not distribute compiled
Aseprite/KARN Mods binaries without separate permission from Igara Studio.**

This program is distributed under three different licenses:

1. Source code and official releases/binaries are distributed under
   our [End-User License Agreement for Aseprite (EULA)](EULA.txt). Please check
   that there are [modules/libraries in the source code](src/README.md) that
   are distributed under the MIT license
   (e.g. [laf](https://github.com/aseprite/laf),
   [clip](https://github.com/aseprite/clip),
   [undo](https://github.com/aseprite/undo),
   [observable](https://github.com/aseprite/observable),
   [ui](src/ui), etc.).
2. You can request a special
   [educational license](https://www.aseprite.org/faq/#is-there-an-educational-license)
   in case you are a teacher in an educational institution and want to
   use Aseprite in your classroom (in-situ).
3. Steam releases are distributed under the terms of the
   [Steam Subscriber Agreement](http://store.steampowered.com/subscriber_agreement/).

You can get more information about Aseprite license in the
[FAQ](https://www.aseprite.org/faq/#licensing-&-commercial).
