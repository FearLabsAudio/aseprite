// Aseprite
// Copyright (C) 2018-present  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "app/tools/ink_processing.h"

#include "app/app.h" // TODO avoid to include this file
#include "app/color_utils.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_undo.h"
#include "app/tools/pick_ink.h"
#include "app/transformation.h"
#include "doc/mask.h"
#include "doc/tile.h"
#include "gfx/region.h"

namespace app { namespace tools {

class BaseInk : public Ink {
public:
  BaseInk() {}
  BaseInk(const BaseInk& other) {}

  void inkHline(int x1, int y, int x2, ToolLoop* loop) override
  {
    ASSERT(m_proc);
    m_proc->processScanline(x1, y, x2, loop);
  }

  void prepareForPointShape(ToolLoop* loop,
                            bool firstPoint,
                            int x,
                            int y,
                            doc::SymmetryIndex symmetry) override
  {
    ASSERT(m_proc);
    m_proc->prepareForPointShape(loop, firstPoint, x, y, symmetry);
  }

  void prepareVForPointShape(ToolLoop* loop, int y) override
  {
    ASSERT(m_proc);
    m_proc->prepareVForPointShape(loop, y);
  }

  void prepareUForPointShapeWholeScanline(ToolLoop* loop, int x1) override
  {
    ASSERT(m_proc);
    m_proc->prepareUForPointShapeWholeScanline(loop, x1);
  }

  void prepareUForPointShapeSlicedScanline(ToolLoop* loop, bool leftSlice, int x1) override
  {
    ASSERT(m_proc);
    m_proc->prepareUForPointShapeSlicedScanline(loop, leftSlice, x1);
  }

  // Common to both SelectionInk and SliceInk
  gfx::Rect tileSelectionToCanvas(int x1, int y, int x2, ToolLoop* loop, bool offset)
  {
    gfx::Rect rc(x1, y, x2 - x1 + 1, 1);

    // For tile point shape, the point shape is done in "tiles"
    // coordinates, but we want the selection in canvas/pixels
    // coordinates.
    if (loop->getPointShape()->isTile()) {
      const Grid& grid = loop->getGrid();
      rc = grid.tileToCanvas(rc);
      if (offset) {
        // For feedback purposes, the coordinates must be relative to
        // the getDstImage() and not in absolute sprite canvas
        // coordinates.
        rc.offset(-grid.origin());
      }
    }
    return rc;
  }

protected:
  void setProc(BaseInkProcessing* proc) { m_proc.reset(proc); }

  BaseInkProcessing* proc() { return m_proc.get(); }

private:
  InkProcessingPtr m_proc = std::make_unique<NoopInkProcessing>();
};

// Ink used for tools which paint with primary/secondary
// (or foreground/background colors)
class PaintInk : public BaseInk {
public:
  enum Type { Simple, WithFg, WithBg, AlphaCompositing, Copy, LockAlpha };

private:
  Type m_type;

public:
  PaintInk(Type type) : m_type(type) {}

  Ink* clone() override { return new PaintInk(*this); }

  bool isPaint() const override { return true; }

  void prepareInk(ToolLoop* loop) override
  {
    switch (m_type) {
      case Simple:
        // Do nothing, use the default colors
        break;

      case WithFg:
      case WithBg: {
        int color = (m_type == WithFg ? loop->getFgColor() : loop->getBgColor());
        loop->setPrimaryColor(color);
        loop->setSecondaryColor(color);
        break;
      }
    }

    // TODO support different ink types for tilemaps (even custom brushes,
    //      and custom inks script-driven)
    if (loop->getDstImage()->pixelFormat() == IMAGE_TILEMAP) {
      setProc(new CopyInkProcessing<TilemapTraits>(loop));
    }
    // Custom brushes
    else if (loop->getBrush()->type() == doc::kImageBrushType) {
      switch (m_type) {
        case Simple:    setProc(get_ink_proc<BrushSimpleInkProcessing>(loop)); break;
        case LockAlpha: setProc(get_ink_proc<BrushLockAlphaInkProcessing>(loop)); break;
        case Copy:      setProc(get_ink_proc<BrushCopyInkProcessing>(loop)); break;
        default:        setProc(get_ink_proc<BrushSimpleInkProcessing>(loop)); break;
      }
    }
    else {
      switch (m_type) {
        case Simple:
        case AlphaCompositing: {
          bool opaque = false;

          // Opacity is set to 255 when InkType=Simple in ToolLoopBase()
          if (loop->getOpacity() == 255 && loop->getDynamics().gradient == DynamicSensor::Static) {
            color_t color = loop->getPrimaryColor();

            switch (loop->sprite()->pixelFormat()) {
              case IMAGE_RGB:       opaque = (rgba_geta(color) == 255); break;
              case IMAGE_GRAYSCALE: opaque = (graya_geta(color) == 255); break;
              case IMAGE_INDEXED:
                // Simple ink for indexed is better to use always
                // opaque if opacity == 255.
                if (m_type == Simple)
                  opaque = true;
                else if (color == loop->sprite()->transparentColor() &&
                         loop->getLayer()->isTransparent()) {
                  opaque = false;
                }
                else {
                  color = loop->getPalette()->getEntry(color);
                  opaque = (rgba_geta(color) == 255);
                }
                break;
            }
          }

          // Use a faster ink, direct copy
          if (opaque)
            setProc(get_ink_proc<CopyInkProcessing>(loop));
          else
            setProc(get_ink_proc<TransparentInkProcessing>(loop));
          break;
        }
        case Copy:      setProc(get_ink_proc<CopyInkProcessing>(loop)); break;
        case LockAlpha: setProc(get_ink_proc<LockAlphaInkProcessing>(loop)); break;
        default:        setProc(get_ink_proc<TransparentInkProcessing>(loop)); break;
      }
    }
  }
};

class ShadingInk : public PaintInk {
  gfx::Region m_previousFootprint;
  gfx::Region m_currentFootprint;
  bool m_onReentry = false;
  bool m_softEdges = false;
  gfx::Region m_pendingEdges;
  gfx::Region m_contactShaded;
  gfx::Point m_brushOrigin;
  const doc::Image* m_shapeImage = nullptr;
  const doc::Image* m_shapeMask = nullptr;

  bool isInterior(int x, int y, ToolLoop* loop) const
  {
    int u = x - m_brushOrigin.x;
    int v = y - m_brushOrigin.y;
    const int w = loop->getDstImage()->width();
    const int h = loop->getDstImage()->height();
    const bool tiledX = int(loop->getTiledMode()) & int(filters::TiledMode::X_AXIS);
    const bool tiledY = int(loop->getTiledMode()) & int(filters::TiledMode::Y_AXIS);
    if (tiledX)
      u = (u % w + w) % w;
    if (tiledY)
      v = (v % h + h) % h;
    auto covered = [&](int bx, int by) {
      if (!m_shapeImage->bounds().contains(gfx::Point(bx, by)))
        return false;
      return m_shapeMask ? m_shapeMask->getPixel(bx, by) != 0 :
                          m_shapeImage->getPixel(bx, by) != m_shapeImage->maskColor();
    };
    // Consider all wrapped copies when a brush is larger than the canvas.
    for (int by = v; by < m_shapeImage->height(); by += tiledY ? h : m_shapeImage->height()) {
      for (int bx = u; bx < m_shapeImage->width(); bx += tiledX ? w : m_shapeImage->width()) {
        bool interior = true;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            interior = interior && covered(bx + dx, by + dy);
        if (interior)
          return true;
      }
    }
    return false;
  }

public:
  ShadingInk() : PaintInk(PaintInk::Simple) {}

  Ink* clone() override { return new ShadingInk(*this); }

  bool isPaint() const override { return true; }
  bool isShading() const override { return true; }

  void beginBrushFootprint() override
  {
    if (m_onReentry)
      m_currentFootprint.clear();
  }

  void endBrushFootprint() override
  {
    if (m_onReentry) {
      m_contactShaded &= m_currentFootprint;
      m_previousFootprint = m_currentFootprint;
    }
  }

  void prepareForPointShape(ToolLoop* loop, bool firstPoint, int x, int y,
                            doc::SymmetryIndex symmetry) override
  {
    PaintInk::prepareForPointShape(loop, firstPoint, x, y, symmetry);
    m_softEdges = loop->shadingSoftEdges();
    if (m_softEdges) {
      m_brushOrigin = gfx::Point(x, y) - loop->getCelOrigin();
      m_shapeImage = loop->getBrush()->getSymmetryImage(symmetry);
      m_shapeMask = loop->getBrush()->getSymmetryMask(symmetry);
    }
  }

  void inkHline(int x1, int y, int x2, ToolLoop* loop) override
  {
    if (!m_onReentry) {
      PaintInk::inkHline(x1, y, x2, loop);
      return;
    }
    const gfx::Region scanline(gfx::Rect(x1, y, x2 - x1 + 1, 1));
    gfx::Region entering(scanline);
    entering -= m_previousFootprint;
    entering -= m_currentFootprint;
    m_currentFootprint |= scanline;
    if (m_softEdges) {
      for (int x = x1; x <= x2; ++x) {
        const gfx::Point pt(x, y);
        if (m_contactShaded.contains(pt))
          continue;
        const gfx::Region pixel(gfx::Rect(x, y, 1, 1));
        bool shade = isInterior(x, y, loop);
        if (!shade && entering.contains(pt)) {
          if (m_pendingEdges.contains(pt)) {
            m_pendingEdges -= pixel;
            shade = true;
          }
          else {
            m_pendingEdges |= pixel;
          }
        }
        if (shade) {
          // Interior coverage completes a pass immediately and consumes any
          // pending half-pass. Symmetry copies cannot shade the same contact twice.
          m_pendingEdges -= pixel;
          m_contactShaded |= pixel;
          PaintInk::inkHline(x, y, x, loop);
        }
      }
      return;
    }
    // Hard-edged sizes can complete a pending edge contact immediately when
    // pressure/velocity crosses the automatic size threshold.
    gfx::Region hardCoverage(scanline);
    hardCoverage -= m_contactShaded;
    m_contactShaded |= hardCoverage;
    m_pendingEdges -= hardCoverage;
    for (const auto& rc : hardCoverage)
      PaintInk::inkHline(rc.x, rc.y, rc.x2() - 1, loop);
  }

  void prepareInk(ToolLoop* loop) override
  {
    m_onReentry = loop->shadingOnReentry();
    m_softEdges = loop->shadingSoftEdges();
    if (loop->getShadingRemap()) {
      if (loop->getBrush()->type() == doc::kImageBrushType) {
        setProc(get_ink_proc<BrushShadingInkProcessing>(loop));
      }
      else {
        setProc(get_ink_proc<ShadingInkProcessing>(loop));
      }
    }
    else {
      PaintInk::prepareInk(loop);
    }
  }
};

class GradientInk : public BaseInk {
public:
  Ink* clone() override { return new GradientInk(*this); }

  bool isPaint() const override { return true; }
  bool isEffect() const override { return true; }
  bool withDitheringOptions() const override { return true; }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<GradientInkProcessing>(loop)); }

  void prepareForStrokes(ToolLoop* loop, Strokes& strokes) override
  {
    proc()->prepareForStrokes(loop, strokes);
  }
};

class ScrollInk : public Ink {
public:
  Ink* clone() override { return new ScrollInk(*this); }

  bool isScrollMovement() const override { return true; }

  void prepareInk(ToolLoop* loop) override
  {
    // Do nothing
  }

  void inkHline(int x1, int y, int x2, ToolLoop* loop) override
  {
    // Do nothing
  }
};

class ZoomInk : public Ink {
public:
  Ink* clone() override { return new ZoomInk(*this); }

  bool isZoom() const override { return true; }
  void prepareInk(ToolLoop* loop) override {}
  void inkHline(int x1, int y, int x2, ToolLoop* loop) override {}
};

class MoveInk : public Ink {
  bool m_autoSelect;

public:
  MoveInk(bool autoSelect) : m_autoSelect(autoSelect) {}

  Ink* clone() override { return new MoveInk(*this); }

  bool isCelMovement() const override { return true; }
  bool isAutoSelectLayer() const override { return m_autoSelect; }
  void prepareInk(ToolLoop* loop) override {}
  void inkHline(int x1, int y, int x2, ToolLoop* loop) override {}
};

class SelectLayerInk : public Ink {
public:
  Ink* clone() override { return new SelectLayerInk(*this); }

  bool isCelMovement() const override { return true; }
  void prepareInk(ToolLoop* loop) override {}
  void inkHline(int x1, int y, int x2, ToolLoop* loop) override {}
};

class SliceInk : public BaseInk {
  bool m_createSlice;
  gfx::Rect m_maxBounds;

public:
  SliceInk() : m_createSlice(false) {}

  Ink* clone() override { return new SliceInk(*this); }

  bool isSlice() const override { return true; }
  bool needsCelCoordinates() const override { return (m_createSlice ? false : true); }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<XorInkProcessing>(loop)); }

  void inkHline(int x1, int y, int x2, ToolLoop* loop) override
  {
    // Map tile coords to canvas if needed
    gfx::Rect rc(BaseInk::tileSelectionToCanvas(x1, y, x2, loop, !m_createSlice));

    if (m_createSlice)
      m_maxBounds |= rc;
    else {
      if (loop->isSelectionToolLoop()) {
        rc &= loop->sprite()->bounds();
        loop->addSelectionToolPoint(rc);
      }
      else {
        rc &= loop->getDstImage()->bounds();
        for (int v = rc.y; v < rc.y2(); ++v)
          BaseInk::inkHline(rc.x, v, rc.x2() - 1, loop);
      }
    }
  }

  void setFinalStep(ToolLoop* loop, bool state) override
  {
    m_createSlice = state;
    if (state) {
      m_maxBounds = gfx::Rect(0, 0, 0, 0);
    }
    else {
      m_maxBounds &= (loop->isSelectionToolLoop() ? loop->sprite()->bounds() :
                                                    loop->getDstImage()->bounds());
      loop->onSliceRect(m_maxBounds);
    }
  }
};

class EraserInk : public BaseInk {
public:
  enum Type { Eraser, ReplaceFgWithBg, ReplaceBgWithFg };

private:
  Type m_type;

public:
  EraserInk(Type type) : m_type(type) {}

  Ink* clone() override { return new EraserInk(*this); }

  bool isPaint() const override { return true; }
  bool isEffect() const override { return true; }
  bool isEraser() const override { return true; }

  void prepareInk(ToolLoop* loop) override
  {
    switch (m_type) {
      case Eraser: {
        if (loop->getBrush()->type() == doc::kImageBrushType) {
          setProc(get_ink_proc<BrushEraserInkProcessing>(loop));
        }
        else if (loop->getDstImage()->pixelFormat() == IMAGE_TILEMAP) {
          color_t clearColor = doc::notile;
          loop->setPrimaryColor(clearColor);
          loop->setSecondaryColor(clearColor);
          setProc(new CopyInkProcessing<TilemapTraits>(loop));
        }
        else {
          // TODO app_get_color_to_clear_layer should receive the context as parameter
          color_t clearColor = app_get_color_to_clear_layer(loop->getLayer());
          loop->setPrimaryColor(clearColor);
          loop->setSecondaryColor(clearColor);

          if (loop->getOpacity() == 255) {
            setProc(get_ink_proc<CopyInkProcessing>(loop));
          }
          else {
            // For opaque layers
            if (loop->getLayer()->isBackground()) {
              setProc(get_ink_proc<TransparentInkProcessing>(loop));
            }
            // For transparent layers
            else {
              if (loop->sprite()->pixelFormat() == IMAGE_INDEXED)
                loop->setPrimaryColor(loop->sprite()->transparentColor());

              setProc(get_ink_proc<MergeInkProcessing>(loop));
            }
          }
        }
        break;
      }
      case ReplaceFgWithBg:
        loop->setPrimaryColor(loop->getFgColor());
        loop->setSecondaryColor(loop->getBgColor());
        setProc(get_ink_proc2<ReplaceInkProcessing>(loop));
        break;

      case ReplaceBgWithFg:
        loop->setPrimaryColor(loop->getBgColor());
        loop->setSecondaryColor(loop->getFgColor());
        setProc(get_ink_proc2<ReplaceInkProcessing>(loop));
        break;
    }
  }
};

class BlurInk : public BaseInk {
public:
  Ink* clone() override { return new BlurInk(*this); }

  bool isPaint() const override { return true; }
  bool isEffect() const override { return true; }
  bool needsSpecialSourceArea() const override { return true; }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<BlurInkProcessing>(loop)); }

  void createSpecialSourceArea(const gfx::Region& dirtyArea, gfx::Region& sourceArea) const override
  {
    // We need one pixel more for each side, to use a 3x3 convolution matrix.
    for (const auto& rc : dirtyArea) {
      sourceArea.createUnion(sourceArea, gfx::Region(gfx::Rect(rc).enlarge(1)));
    }
  }
};

class JumbleInk : public BaseInk {
public:
  Ink* clone() override { return new JumbleInk(*this); }

  bool isPaint() const override { return true; }
  bool isEffect() const override { return true; }
  bool needsSpecialSourceArea() const override { return true; }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<JumbleInkProcessing>(loop)); }

  void createSpecialSourceArea(const gfx::Region& dirtyArea, gfx::Region& sourceArea) const override
  {
    // We need one pixel more for each side.
    for (const auto& rc : dirtyArea) {
      sourceArea.createUnion(sourceArea, gfx::Region(gfx::Rect(rc).enlarge(1)));
    }
  }
};

// Ink used for selection tools (like Rectangle Marquee, Lasso, Magic Wand, etc.)
class SelectionInk : public BaseInk {
  bool m_modify_selection;
  Mask m_mask;
  Mask m_intersectMask;
  Rect m_maxBounds;

public:
  SelectionInk() : m_modify_selection(false) {}

  Ink* clone() override { return new SelectionInk(*this); }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<XorInkProcessing>(loop)); }

  bool isSelection() const override { return true; }
  bool needsCelCoordinates() const override { return (m_modify_selection ? false : true); }

  void inkHline(int x1, int y, int x2, ToolLoop* loop) override
  {
    // Map tile coords to canvas if needed
    gfx::Rect rc(BaseInk::tileSelectionToCanvas(x1, y, x2, loop, !m_modify_selection));

    if (m_modify_selection) {
      int modifiers = int(loop->getModifiers());

      if ((modifiers & (int(ToolLoopModifiers::kReplaceSelection) |
                        int(ToolLoopModifiers::kAddSelection))) != 0) {
        m_mask.add(rc);
      }
      else if ((modifiers & int(ToolLoopModifiers::kSubtractSelection)) != 0) {
        m_mask.subtract(rc);
      }
      else if ((modifiers & int(ToolLoopModifiers::kIntersectSelection)) != 0) {
        m_intersectMask.add(rc);
      }

      m_maxBounds |= rc;
    }
    else {
      if (loop->isSelectionToolLoop()) {
        rc &= loop->sprite()->bounds();
        loop->addSelectionToolPoint(rc);
      }
      else {
        rc &= loop->getDstImage()->bounds();
        for (int v = rc.y; v < rc.y2(); ++v)
          BaseInk::inkHline(rc.x, v, rc.x2() - 1, loop);
      }
    }
  }

  void setFinalStep(ToolLoop* loop, bool state) override
  {
    m_modify_selection = state;
    int modifiers = int(loop->getModifiers());

    if (state) {
      m_maxBounds = loop->getMask()->bounds();

      m_mask.copyFrom(loop->getMask());
      m_mask.freeze();
      m_mask.reserve(loop->sprite()->bounds());

      if ((modifiers & int(ToolLoopModifiers::kIntersectSelection)) != 0) {
        m_intersectMask.clear();
        m_intersectMask.reserve(loop->sprite()->bounds());
      }
    }
    else {
      if ((modifiers & int(ToolLoopModifiers::kIntersectSelection)) != 0) {
        m_mask.intersect(m_intersectMask);
        m_intersectMask.clear();
      }

      // We can intersect the used bounds in inkHline() calls to
      // reduce the shrink computation.
      m_mask.intersect(m_maxBounds);

      m_mask.unfreeze();

      loop->setMask(&m_mask);
      const float cornerThick = (loop->isTilemapMode() ? CORNER_THICK_FOR_TILEMAP_MODE :
                                                         CORNER_THICK_FOR_PIXELS_MODE);
      loop->getDocument()->setTransformation(Transformation(RectF(m_mask.bounds()), cornerThick));

      m_mask.clear();
    }
  }
};

class TextInk : public BaseInk {
public:
  Ink* clone() override { return new TextInk(*this); }

  bool isText() const override { return true; }

  void prepareInk(ToolLoop* loop) override { setProc(get_ink_proc<XorInkProcessing>(loop)); }
};

}} // namespace app::tools
