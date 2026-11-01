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
- [Export layers as 4bpp BMPs](#export-layers-as-4bpp-bmps)
- [Export to Karnedit](#export-to-karnedit)
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
- Progressive Shading: each visit advances a pixel one shade. The entire brush must leave that pixel before returning for another step. Repeated passes continue through the ramp.
- Once Per Stroke: each pixel advances at most one shade while the button is held. Release and begin another stroke for the next step.

Progressive Shading suits gradual buildup and repeated sculpting passes. Once Per Stroke gives a consistent single-step adjustment across a broad sweep. Both support darkening and lightening through the existing mouse-button behavior.

Progressive Shading is the default. Built-in brushes at 1px and 2px have hard edges. At 3px and above, an automatic soft perimeter requires two visits to advance a shade. The perimeter adds two pixels to the displayed size: a 3px brush uses a 5px footprint. Visit counts reset at the end of the stroke.

Indexed shading follows the selected ramp without changing palette indices through color matching. Spray retains its own distribution controls; use spray width and speed to adjust the marks.

## Tool presets

Open View > Tool Presets to show or hide the panel below the palette. Drag the divider between the palette and presets to adjust their heights. Choose Float to move presets into a separate window, or Dock to return them to the sidebar.

- + captures the current painting tool and brush settings. Name the preset, review its settings, and choose Save New.
- Click a preset to recall it. Its highlight clears if you choose a different tool or change settings that no longer match it. Click the preset again to restore its settings. Color changes do not clear presets with Ignore colors enabled.
- E opens the selected preset for editing. Change its name, brush shape, size, angle, ink, shading mode, or stroke style. The scrollable form also includes spray, fill, gradient, and dynamics controls where supported by the tool. Controls that do not apply to the current settings are dimmed. Save keeps your changes; Cancel leaves the preset and your current tool unchanged. Click the preset to use the edited settings. Check Ignore colors to recall its brush settings while keeping your current colors and shading ramp.
- The trash button deletes the selected preset after confirmation.
- Update color(s) saves new colors only to the selected preset. Select a preset, choose your colors, then click Update color(s). Shading presets save the current ramp; other presets save the foreground and background colors. Other presets, brush settings, and the Ignore colors checkbox stay unchanged. If changing colors clears the highlight, Update color(s) still targets the last preset you chose; hover over the button to see its name.

Each preset shows its saved brush size beside its name; image brushes show their dimensions. Swatches show the saved foreground/background colors, or the full ramp for shading presets. An empty swatch with a red diagonal means colors are not included. Scroll horizontally to see longer ramps. Indexed swatches use the current palette; their saved indices stay exact.

Presets include the tool, brush shape and size, ink, shading mode, spray controls, and dynamics. New presets also capture colors; use Ignore colors in the New or Edit window to leave your working colors unchanged on recall. Image brushes retain their pixels. Shared ink and dynamics follow Aseprite's current sharing settings.

Presets are saved with your application settings and remain available across documents and sessions. The panel remembers whether it is open, docked or floating, along with its divider position and floating window size and position.

## Palette banks

Numbered banks are available on both ordinary image layers and tilemap layers. Each layer retains local indices 0-15 and displays them through its assigned bank, with index 0 transparent. Banks use palette offsets 0, 16, 32, and so on, allowing several 4bpp palettes in one document.

Open Palette Banks with the PB button near the tilemap controls:

- Layers lists editable layer names and bank assignments.
- Banks lists each bank's index, name, and swatches.

Edits apply immediately and are undoable. Layer > Properties also provides bank assignment and naming controls.

Selecting a banked layer shows its 16 entries in the color bar. Painting, shading, and the eyedropper use local indices. Layers assigned to the same bank share palette edits. Loading a palette while a banked layer is selected replaces that bank's color entries without replacing the other banks.

A shared tileset can appear on several layers with different bank assignments. Tile edits still affect every use of the shared tile; the layer assignment controls its displayed palette. Converting between ordinary and tilemap layers preserves the bank and local indices.

Document Palette leaves a layer unassigned and excludes it from KARN exports. Groups and Aseprite's built-in Reference layers cannot receive banks. Convert background layers to ordinary layers before assigning one.

Existing indexed artwork must use indices 0-15 before receiving a numbered bank. Assignment preserves those indices rather than remapping a 256-color image. Force KARN rules can convert incompatible artwork after confirmation; review the resulting colors and tiles before saving.

## VERA colors and KARN rules

The 4-bit checkbox beside the palette Options button enables Force 4 bit color channels for the document. It constrains all banks to VERA-compatible RGB444 colors, including subsequent palette edits and imports. Pixel indices and alpha are preserved.

File > Export > KARN > Force KARN rules applies the broader production constraints:

- Indexed artwork using local indices 0-15, with index 0 transparent.
- RGB444 palette colors across all banks.
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

## Export layers as 4bpp BMPs

Open File > Export > KARN > Export layers as 4bpp BMPs. Both ordinary image layers and tilemaps are supported; only layers assigned to numbered banks are eligible.

Choose the BMP output folder, base name, and source layers. All, Visible, and None provide quick selection. Hidden layers may also be selected explicitly.

Default filenames combine the base name with the bank name. Separate layers sharing a bank also receive a layer name. Review or edit each filename; Reset filenames regenerates them from the current naming choices.

Enable Merge layers sharing a palette bank to produce one BMP per selected bank. Selected layers composite in canvas order, with transparency revealing lower selected layers in that bank. Different banks remain separate. Merging requires Normal blending, full layer/group/cel opacity, and zero cel Z-index.

BMPs preserve the arranged artwork and exact local indices. The canvas origin is retained, with dimensions padded to whole 8 x 8 tiles. Artwork is not trimmed or packed into a tileset. Use Aseprite's existing tileset export when you need the tileset itself.

Indices 1 and 2 are preserved on export, with a warning: KarnEdit treats them as transparent on import. Move affected artwork to other indices if those pixels must remain visible in KarnEdit.

Export confirms before overwriting existing files. Retain destinations for routine updates, or choose new paths for a separate resource set. Selection, merge settings, filenames, and folders are remembered; save the .aseprite file to retain them between sessions.

## Export to Karnedit

File > Export > KARN > Export to Karnedit uses the shared BMP export setup and creates one bitmap TileKit per BMP. BMPs and TileKit JSON files can be kept in different folders.

Missing or outdated BMPs open the BMP export step first. Current BMPs are reused without repeating that setup. You can also export BMPs manually before running Export to Karnedit.

Choose the game workspace, TileKit folder, and JSON destination for each source BMP. BMP exports reopens the shared bitmap setup. Reset TileKit paths derives destinations from the current folder and BMP names.

Export Collage is available when visible banked layers use multiple banks, and is enabled by default when available. Disable it for TileKit-only output. Collages use 8 x 8 cells and support layouts up to 64 x 64 cells.

Collage overlap is resolved per tile. The topmost nonempty tile wins in full, including its transparent pixels; lower tiles do not show through those holes. A completely empty upper tile is skipped. Each cell therefore retains a single source palette.

To composite pixels from same-bank layers, explicitly merge their BMPs in the shared export step. Unmerged sources retain the topmost-tile behavior. Exclude hidden layers from merged collage sources when they should not contribute to the scene.

Export updates existing destinations while retaining TileKit brushes and resource links. New destinations create new resources. Existing files require overwrite/update confirmation. Save the .aseprite source afterward to retain the mappings for the next update.

## Saving and compatibility

Use .aseprite for editable sources. The file retains bank assignments, names, KARN settings, full-color reference artwork, and export mappings.

Standard Aseprite does not apply the custom bank assignments or enforce KARN rules. Banked artwork will display through the first palette bank there, so use this build when editing these sources.

Full color Reference layers include an indexed fallback for standard Aseprite. Saving in standard Aseprite discards the original full-color data. Retain the custom-build source when exchanging files.

PNG and sprite-sheet exports compose visible layers using their assigned banks. Hide full-color guides if they should not appear in the output. Export Tileset uses the selected layer's bank.

Global color-mode changes and palette remapping are restricted while bank assignments exist. Export a composed image for conversion, or remove assignments first. Disable Force KARN rules before changing the document away from Indexed mode.

## Troubleshooting

- Missing export layers: check for a numbered bank assignment. Collages also require the layer and its parent groups to be visible.
- Export Collage unavailable: at least two distinct banks must be used by visible layers. TileKit export remains available.
- Progressive shading does not advance: move the entire brush footprint off the pixel before returning. The soft perimeter needs two visits within the stroke.
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
