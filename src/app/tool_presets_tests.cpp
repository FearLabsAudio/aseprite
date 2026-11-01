// KARN tool preset persistence and settings recall.
#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#include "app/tool_presets.h"
#include "base/fs.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>

using namespace app;
using namespace doc;

TEST(ToolPresets, RecallSprayAndShadingWithEffectiveSharedDynamics)
{
  ToolPreferences source("source"), shared("shared"), destination("destination");
  source.brush.type(gen::BrushType::SQUARE);
  source.brush.size(13);
  source.brush.angle(-47);
  source.ink(tools::InkType::SHADING);
  source.shadingOnReentry(true);
  source.shadingOncePerStroke(false);
  source.spray.width(29);
  source.spray.speed(72);
  source.freehandAlgorithm(tools::FreehandAlgorithm::PIXEL_PERFECT);
  source.dynamics.size(tools::DynamicSensor::Static);
  shared.dynamics.size(tools::DynamicSensor::Pressure);
  shared.dynamics.minPressureThreshold(0.24);
  shared.dynamics.matrixName("Bayer 4x4");
  const auto snapshot = capture_tool_settings(source, shared.dynamics);
  source.brush.size(2);
  source.shadingOncePerStroke(true);
  source.spray.width(1);
  shared.dynamics.size(tools::DynamicSensor::Velocity);
  apply_tool_settings(snapshot, destination, destination.dynamics);
  EXPECT_EQ(13, destination.brush.size());
  EXPECT_EQ(-47, destination.brush.angle());
  EXPECT_EQ(gen::BrushType::SQUARE, destination.brush.type());
  EXPECT_EQ(tools::InkType::SHADING, destination.ink());
  EXPECT_TRUE(destination.shadingOnReentry());
  EXPECT_FALSE(destination.shadingOncePerStroke());
  EXPECT_EQ(29, destination.spray.width());
  EXPECT_EQ(72, destination.spray.speed());
  EXPECT_EQ(tools::FreehandAlgorithm::PIXEL_PERFECT, destination.freehandAlgorithm());
  EXPECT_EQ(tools::DynamicSensor::Pressure, destination.dynamics.size());
  EXPECT_DOUBLE_EQ(0.24, destination.dynamics.minPressureThreshold());
  EXPECT_EQ("Bayer 4x4", destination.dynamics.matrixName());
}

TEST(ToolPresets, RoundTripImageBrushRampRenameAndDelete)
{
  const std::string filename = "tool-presets-roundtrip-test.json";
  ToolPreferences source("source");
  ToolPreset preset;
  preset.name = "Shading & spray \"fine\"";
  preset.toolId = "spray";
  preset.settings = capture_tool_settings(source, source.dynamics);
  preset.shade = { Color::fromIndex(2), Color::fromIndex(5), Color::fromIndex(7) };
  preset.gradientType = 1;
  preset.gradientDithering = "Bayer 4x4";
  preset.brush = std::make_shared<Brush>();
  for (auto format : { IMAGE_INDEXED, IMAGE_RGB, IMAGE_GRAYSCALE }) {
    ImageRef image(Image::create(format, 3, 2));
    ImageRef mask(Image::create(IMAGE_BITMAP, 3, 2));
    image->clear(0);
    image->setMaskColor(5);
    image->putPixel(1, 0, format == IMAGE_RGB ? rgba(12, 34, 56, 78) : 7);
    mask->clear(1);
    mask->putPixel(0, 1, 0);
    preset.brush->setImage(image.get(), mask.get());
    preset.brush->setPattern(BrushPattern::PAINT_BRUSH);
    preset.brush->setCenter(gfx::Point(2, 1));
    preset.brush->setPatternOrigin(gfx::Point(-3, 14));
    save_tool_presets(filename, { preset });
    auto loaded = load_tool_presets(filename);
    ASSERT_EQ(1, loaded.size());
    EXPECT_EQ(preset.name, loaded[0].name);
    EXPECT_EQ(preset.settings.dump(), loaded[0].settings.dump());
    EXPECT_EQ(preset.shade, loaded[0].shade);
    EXPECT_EQ(preset.gradientDithering, loaded[0].gradientDithering);
    EXPECT_EQ(1, loaded[0].gradientType);
    EXPECT_EQ(gfx::Point(2, 1), loaded[0].brush->center());
    EXPECT_EQ(gfx::Point(-3, 14), loaded[0].brush->patternOrigin());
    EXPECT_EQ(BrushPattern::PAINT_BRUSH, loaded[0].brush->pattern());
    EXPECT_EQ(5, loaded[0].brush->image()->maskColor());
    EXPECT_EQ(0, count_diff_between_images(image.get(), loaded[0].brush->image()));
    EXPECT_EQ(0, count_diff_between_images(mask.get(), loaded[0].brush->maskBitmap()));
    auto workingBrush = loaded[0].brush->cloneWithNewImages();
    workingBrush->image()->putPixel(1, 0, 0);
    EXPECT_NE(0, loaded[0].brush->image()->getPixel(1, 0));
    loaded[0].name = "Renamed";
    save_tool_presets(filename, loaded);
    EXPECT_EQ("Renamed", load_tool_presets(filename)[0].name);
  }
  save_tool_presets(filename, {});
  EXPECT_TRUE(load_tool_presets(filename).empty());
  base::delete_file(filename);
}

TEST(ToolPresets, RejectInvalidSettingsAndDamagedFiles)
{
  ToolPreferences p("test");
  p.brush.size(9);
  p.ink(tools::InkType::SHADING);
  apply_tool_settings(
    json11::Json::object{
      { "brush_size",  -1        },
      { "ink",         1000      },
      { "spray_speed", "invalid" }
  },
    p,
    p.dynamics);
  EXPECT_EQ(9, p.brush.size());
  EXPECT_EQ(tools::InkType::SHADING, p.ink());
  const std::string filename = "tool-presets-damaged-test.json";
  {
    std::ofstream out(filename);
    out << "{\"version\": 1, \"presets\": [";
  }
  EXPECT_THROW(load_tool_presets(filename), std::runtime_error);
  base::delete_file(filename);
  EXPECT_TRUE(load_tool_presets(filename).empty());
}

TEST(ToolPresets, UpdateOnlyColorsAndPersistExactIndices)
{
  ToolPreferences source("source");
  source.brush.size(11);
  source.spray.speed(53);
  ToolPreset preset;
  preset.name = "Color update";
  preset.toolId = "spray";
  preset.brush = std::make_shared<Brush>(kCircleBrushType, 11, 0);
  preset.settings = capture_tool_settings(source, source.dynamics);
  preset.shade = { Color::fromIndex(3), Color::fromIndex(8) };
  const auto originalSettings = preset.settings.dump();
  const auto originalBrush = preset.brush;
  const auto originalShade = preset.shade;
  const std::string filename = "tool-preset-colors-test.json";
  // Existing presets without saved colors retain their old behavior.
  save_tool_presets(filename, { preset });
  EXPECT_FALSE(load_tool_presets(filename)[0].hasColors);
  update_tool_preset_colors(preset, Color::fromIndex(6), Color::fromIndex(0), {});
  save_tool_presets(filename, { preset });
  auto loaded = load_tool_presets(filename)[0];
  EXPECT_TRUE(loaded.hasColors);
  EXPECT_EQ(Color::fromIndex(6), loaded.foreground);
  EXPECT_EQ(Color::fromIndex(0), loaded.background);
  EXPECT_EQ(originalSettings, preset.settings.dump());
  EXPECT_EQ(originalBrush, preset.brush);
  EXPECT_EQ(originalShade, preset.shade);
  update_tool_preset_colors(preset, Color::fromRgb(12, 34, 56, 78), Color::fromGray(90), {});
  save_tool_presets(filename, { preset });
  loaded = load_tool_presets(filename)[0];
  EXPECT_EQ(Color::fromRgb(12, 34, 56, 78), loaded.foreground);
  EXPECT_EQ(Color::fromGray(90), loaded.background);
  source.ink(tools::InkType::SHADING);
  preset.settings = capture_tool_settings(source, source.dynamics);
  preset.hasColors = false;
  const Shade ramp{ Color::fromIndex(12), Color::fromIndex(7), Color::fromIndex(2) };
  update_tool_preset_colors(preset, Color::fromIndex(1), Color::fromIndex(0), ramp);
  save_tool_presets(filename, { preset });
  loaded = load_tool_presets(filename)[0];
  EXPECT_EQ(ramp, loaded.shade); // Preserve reversed ramps and exact local indices.
  EXPECT_FALSE(loaded.hasColors);
  EXPECT_EQ(11, loaded.settings["brush_size"].int_value());
  EXPECT_EQ(53, loaded.settings["spray_speed"].int_value());
  EXPECT_EQ(originalBrush, preset.brush);
  preset.ignoreColors = true;
  save_tool_presets(filename, { preset });
  loaded = load_tool_presets(filename)[0];
  EXPECT_TRUE(loaded.ignoreColors);
  EXPECT_EQ(ramp, loaded.shade); // Keep the snapshot available when unchecked again.
  auto presets = std::vector<ToolPreset>{ loaded, loaded };
  const Shade updatedRamp{ Color::fromIndex(9), Color::fromIndex(4) };
  update_tool_preset_colors(presets[1], Color::fromIndex(1), Color::fromIndex(0), updatedRamp);
  save_tool_presets(filename, presets);
  presets = load_tool_presets(filename);
  EXPECT_EQ(ramp, presets[0].shade); // Only the selected preset is updated.
  EXPECT_EQ(updatedRamp, presets[1].shade);
  EXPECT_TRUE(presets[0].ignoreColors);
  EXPECT_TRUE(presets[1].ignoreColors); // Color updates leave the checkbox unchanged.
  base::delete_file(filename);
}

TEST(ToolPresets, EditSizeWithoutMutatingSavedSnapshot)
{
  ToolPreferences source("source"), recalled("recalled");
  source.brush.size(1);
  ToolPreset saved;
  saved.name = "Pencil";
  saved.toolId = "pencil";
  saved.settings = capture_tool_settings(source, source.dynamics);
  saved.brush = std::make_shared<Brush>(kCircleBrushType, 1, 0);
  saved.ignoreColors = true;
  saved.shade = { Color::fromIndex(3), Color::fromIndex(8) };
  auto edited = saved;
  update_tool_preset_size(edited, 16);
  EXPECT_EQ(1, saved.brush->size()); // Canceling the copy cannot change the saved brush.
  EXPECT_EQ(1, saved.settings["brush_size"].int_value());
  EXPECT_EQ(16, edited.brush->size());
  EXPECT_EQ(16, edited.brush->bounds().w);
  EXPECT_EQ(16, edited.settings["brush_size"].int_value());
  EXPECT_EQ(saved.shade, edited.shade);
  EXPECT_TRUE(edited.ignoreColors);
  const std::string filename = "tool-presets-size-test.json";
  save_tool_presets(filename, { saved, edited });
  const auto loaded = load_tool_presets(filename);
  apply_tool_settings(loaded[1].settings, recalled, recalled.dynamics);
  EXPECT_EQ(16, recalled.brush.size());
  EXPECT_EQ(16, loaded[1].brush->size());
  EXPECT_EQ(1, loaded[0].brush->size());
  base::delete_file(filename);
}

TEST(ToolPresets, EditSettingsKeepsBrushAndRecallInSync)
{
  ToolPreferences source("source"), recalled("recalled");
  source.brush.size(3);
  source.ink(tools::InkType::SIMPLE);
  ToolPreset saved;
  saved.name = "Editable spray";
  saved.toolId = "spray";
  saved.settings = capture_tool_settings(source, source.dynamics);
  saved.brush = std::make_shared<Brush>(kCircleBrushType, 3, 0);
  saved.shade = { Color::fromIndex(8), Color::fromIndex(3) };
  saved.ignoreColors = true;
  auto edited = saved;
  update_tool_preset_settings(edited,
                              json11::Json::object{
                                { "ink",             int(tools::InkType::SHADING) },
                                { "shading_once",    true                         },
                                { "shading_reentry", false                        },
                                { "brush_type",      int(kSquareBrushType)        },
                                { "brush_size",      16                           },
                                { "brush_angle",     -47                          },
                                { "spray_width",     35                           },
                                { "spray_speed",     72                           },
                                { "freehand",        1                            },
                                { "dynamic_size",    1                            },
                                { "min_size",        4                            },
                                { "min_pressure",    0.25                         }
  });
  EXPECT_EQ(kCircleBrushType, saved.brush->type());
  EXPECT_EQ(3, saved.brush->size());
  EXPECT_EQ(int(tools::InkType::SIMPLE), saved.settings["ink"].int_value());
  EXPECT_EQ(kSquareBrushType, edited.brush->type());
  EXPECT_EQ(16, edited.brush->size());
  EXPECT_EQ(-47, edited.brush->angle());
  EXPECT_TRUE(edited.ignoreColors);
  EXPECT_EQ(saved.shade, edited.shade);
  const std::string filename = "tool-presets-edited-settings-test.json";
  save_tool_presets(filename, { saved, edited });
  auto loaded = load_tool_presets(filename);
  apply_tool_settings(loaded[1].settings, recalled, recalled.dynamics);
  EXPECT_EQ(tools::InkType::SHADING, recalled.ink());
  EXPECT_TRUE(recalled.shadingOncePerStroke());
  EXPECT_FALSE(recalled.shadingOnReentry());
  EXPECT_EQ(16, recalled.brush.size());
  EXPECT_EQ(-47, recalled.brush.angle());
  EXPECT_EQ(35, recalled.spray.width());
  EXPECT_EQ(72, recalled.spray.speed());
  EXPECT_EQ(tools::FreehandAlgorithm::PIXEL_PERFECT, recalled.freehandAlgorithm());
  EXPECT_EQ(tools::DynamicSensor::Pressure, recalled.dynamics.size());
  EXPECT_EQ(4, recalled.dynamics.minSize());
  EXPECT_DOUBLE_EQ(0.25, recalled.dynamics.minPressureThreshold());
  EXPECT_EQ(16, loaded[1].brush->size());
  base::delete_file(filename);
  // Editing ink/dynamics must not regenerate an image stamp into a standard brush.
  ImageRef stamp(Image::create(IMAGE_INDEXED, 2, 3));
  stamp->clear(7);
  edited.brush->setImage(stamp.get(), nullptr);
  update_tool_preset_settings(edited,
                              json11::Json::object{
                                { "opacity", 128 }
  });
  EXPECT_EQ(kImageBrushType, edited.brush->type());
  EXPECT_EQ(0, count_diff_between_images(stamp.get(), edited.brush->image()));
}

TEST(ToolPresets, LegacyColorRecallDefaults)
{
  const std::string filename = "tool-presets-legacy-test.json";
  const auto legacy = json11::Json::object{
    { "name",     "Legacy"                                                       },
    { "tool",     "pencil"                                                       },
    { "settings", json11::Json::object{ { "ink", int(tools::InkType::SIMPLE) } } },
    { "brush",    json11::Json::object{ { "type", 0 }, { "size", 3 } }           }
  };
  auto regular = legacy;
  auto shading = legacy;
  shading["settings"] = json11::Json::object{
    { "ink", int(tools::InkType::SHADING) }
  };
  auto colored = legacy;
  colored["colors"] = json11::Json::object{
    { "foreground", Color::fromIndex(7).toString() },
    { "background", Color::fromIndex(0).toString() }
  };
  {
    std::ofstream out(filename);
    out << json11::Json(json11::Json::object{
                          { "version", 1                                                },
                          { "presets", json11::Json::array{ regular, shading, colored } }
    })
             .dump();
  }
  const auto loaded = load_tool_presets(filename);
  ASSERT_EQ(3, loaded.size());
  EXPECT_TRUE(loaded[0].ignoreColors);
  EXPECT_FALSE(loaded[1].ignoreColors);
  EXPECT_FALSE(loaded[2].ignoreColors);
  base::delete_file(filename);
}

int app_main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ToolPresets, MatchIncludesToolBrushInkSprayAndSharedDynamics)
{
  ToolPreferences source("match");
  source.brush.size(8);
  ToolPreset saved;
  saved.toolId = "pencil";
  saved.settings = capture_tool_settings(source, source.dynamics);
  saved.brush = std::make_shared<Brush>(kCircleBrushType, 8, 0);
  auto current = saved;
  current.name = "Different name";
  current.brush = saved.brush->cloneWithNewImages();
  current.brush->setPatternOrigin(gfx::Point(33, 44));
  EXPECT_TRUE(tool_preset_matches(saved, current)); // Painting placement is not a setting.
  current.toolId = "spray";
  EXPECT_FALSE(tool_preset_matches(saved, current));
  current = saved;
  for (const auto* key :
       { "brush_size", "ink", "shading_reentry", "spray_width", "dynamic_size" }) {
    auto changed = saved.settings.object_items();
    changed[key] = changed[key].is_bool() ? json11::Json(!changed[key].bool_value()) :
                                            json11::Json(changed[key].int_value() + 1);
    current.settings = changed;
    EXPECT_FALSE(tool_preset_matches(saved, current)) << key;
  }
  current = saved;
  current.brush = std::make_shared<Brush>(kSquareBrushType, 8, 0);
  EXPECT_FALSE(tool_preset_matches(saved, current));
  current = saved;
  current.gradientType = 1;
  EXPECT_TRUE(tool_preset_matches(saved, current)); // Irrelevant to pencil.
  saved.toolId = current.toolId = "gradient";
  EXPECT_FALSE(tool_preset_matches(saved, current));
}

TEST(ToolPresets, MatchHonorsIgnoreColorsAndOrderedShadingRamp)
{
  ToolPreset saved;
  saved.toolId = "pencil";
  saved.settings = json11::Json::object{
    { "ink", int(tools::InkType::SHADING) }
  };
  saved.hasColors = true;
  saved.foreground = Color::fromIndex(3);
  saved.background = Color::fromIndex(4);
  saved.shade = { Color::fromIndex(3), Color::fromIndex(4), Color::fromIndex(5) };
  auto current = saved;
  current.foreground = Color::fromIndex(6);
  EXPECT_TRUE(tool_preset_matches(saved, current)); // Shading uses the ramp, not foreground.
  saved.settings = current.settings = json11::Json::object{
    { "ink", int(tools::InkType::SIMPLE) }
  };
  EXPECT_FALSE(tool_preset_matches(saved, current));
  saved.settings = json11::Json::object{
    { "ink", int(tools::InkType::SHADING) }
  };
  current = saved;
  std::reverse(current.shade.begin(), current.shade.end());
  EXPECT_FALSE(tool_preset_matches(saved, current));
  saved.ignoreColors = true;
  current.foreground = Color::fromIndex(6);
  current.background = Color::fromIndex(7);
  EXPECT_TRUE(tool_preset_matches(saved, current));
  saved.ignoreColors = false;
  saved.hasColors = false;
  saved.settings = current.settings = json11::Json::object{
    { "ink", int(tools::InkType::SIMPLE) }
  };
  EXPECT_TRUE(tool_preset_matches(saved, current)); // Legacy preset with no colors.
}

TEST(ToolPresets, MatchImageStampAndMaskButAllowIgnoredRecoloring)
{
  ImageRef pixels(Image::create(IMAGE_INDEXED, 2, 2));
  pixels->clear(3);
  pixels->putPixel(1, 1, 4);
  ToolPreset saved;
  saved.toolId = "pencil";
  saved.brush = std::make_shared<Brush>();
  saved.brush->setImage(pixels.get(), nullptr);
  auto current = saved;
  current.brush = saved.brush->cloneWithNewImages();
  EXPECT_TRUE(tool_preset_matches(saved, current));
  current.brush->setImageColor(Brush::ImageColor::MainColor, 8);
  EXPECT_FALSE(tool_preset_matches(saved, current));
  saved.ignoreColors = true;
  EXPECT_TRUE(tool_preset_matches(saved, current));
  current.brush->maskBitmap()->putPixel(0, 0, 0);
  EXPECT_FALSE(tool_preset_matches(saved, current));
  current.brush = saved.brush->cloneWithNewImages();
  current.brush->image()->putPixel(1, 0, 9);
  EXPECT_FALSE(tool_preset_matches(saved, current));
}
