# Welcome

KARN MODS adds palette-bank editing, controlled shading, and KarnEdit exports to Aseprite. This guide covers the custom controls and the workflow for preparing and updating game artwork.

Use the chapter selector to jump to a topic. The manual scrolls independently of the logo and controls, and remembers its window size and position.

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

Global color-mode changes and palette remapping are restricted while bank assignments exist. Export a composed image for conversion, or remove assignments first. Disable Force KARN rules before changing the document away from Indexed mode.

## Troubleshooting

- Missing export layers: check for a numbered bank assignment. Collages also require the layer and its parent groups to be visible.
- Export Collage unavailable: at least two distinct banks must be used by visible layers. TileKit export remains available.
- Progressive shading does not advance: move the brush away before passing over the area again.
- Unexpected collage transparency: unmerged sources replace entire tiles. Merge same-bank BMP sources when pixel compositing is intended.
- Missing colors in KarnEdit: inspect reserved indices 1 and 2 and review the export warning.
- Rejected edits: check Force KARN rules against the document's indices, color mode, and tile settings.
- Updating existing assets: retain their BMP and JSON destinations. Choose unused paths when the output should be a separate resource.
