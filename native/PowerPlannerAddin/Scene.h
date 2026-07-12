// A tiny device-neutral 2D scene: the chart is described as a list of styled
// primitives (in slide points), and a backend renderer (PptRenderer) emits them
// as native shapes. This decouples the visual vocabulary / theme from the
// PowerPoint object-model plumbing, so restyling (e.g. Material) is just a
// matter of building a different scene.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "GanttTheme.h"

enum class PrimKind { Rect, RoundRect, Diamond, Line, Connector, Text };
enum class TextAlign { Left, Center, Right };

// Colors are BGR (PowerPoint's MsoRGBType byte order): 0x00BBGGRR.
struct Style {
	bool fill = false;            unsigned long fillBgr = 0;
	bool line = false;            unsigned long lineBgr = 0;  float lineWeight = 1.0f;
	unsigned long textBgr = 0;    float fontSize = 12.0f;     bool bold = false;
	TextAlign align = TextAlign::Left;
	bool arrowEnd = false;        // connectors
	float corner = 0.0f;          // RoundRect corner radius in points (0 = renderer default)
	bool dash = false;            // dashed/dotted stroke for Line/Connector
};

struct Prim {
	PrimKind kind;
	float x = 0, y = 0, w = 0, h = 0;  // box; for Line/Connector use (x,y)-(x2,y2)
	float x2 = 0, y2 = 0;
	std::wstring text;
	Style style;
	std::string tagKind, tagId;        // PP_KIND / PP_ID (empty = untagged)
	// Set by the explicit time-window scene filter when this primitive touches a
	// window boundary. The renderer keeps these as scene metadata; continuation
	// glyphs are separate, non-interactive prims.
	bool clippedL = false, clippedR = false;
};

struct Scene { std::vector<Prim> prims; };

// ---- Material theme --------------------------------------------------------

// RGB (0xRRGGBB) -> PowerPoint BGR (0x00BBGGRR).
inline unsigned long Bgr(unsigned long rgb) {
	return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

// Material-style token set (RGB). Lean and calm: one primary for task bars,
// amber accent for milestones, neutral greys for chrome.
struct Theme {
	unsigned long surface;          // chart background
	unsigned long railSurface;      // left navigation rail
	unsigned long divider;          // row dividers / gridlines
	unsigned long onSurface;        // primary text
	unsigned long onSurfaceVariant; // secondary text (axis, labels)
	unsigned long primary;          // task bars
	unsigned long primaryDark;      // percent-complete fill
	unsigned long onPrimary;        // text on task bars
	unsigned long milestone;        // milestone diamonds
	unsigned long summary;          // summary bars
	unsigned long bracket;          // brackets
	unsigned long connector;        // dependency lines
};

// All fields sourced from GanttTheme.h (= docs/design-tokens.md). No hex
// literals here — a token change lands once, in GanttTheme.h.
inline Theme MaterialLight() {
	Theme t;
	t.surface          = gt::surface;
	t.railSurface      = gt::railSurface;
	t.divider          = gt::outline;
	t.onSurface        = gt::ink;
	t.onSurfaceVariant = gt::ink2;
	t.primary          = gt::primary;
	t.primaryDark      = gt::primary;   // progress now uses the solid swatch, not this
	t.onPrimary        = gt::surface;   // white on-bar label
	t.milestone        = gt::ink;       // ink diamonds (tokens §1)
	t.summary          = gt::ink3;      // summary rail
	t.bracket          = gt::ink3;      // structural hairline (no dedicated token)
	t.connector        = gt::ink3;      // dependency connectors (tokens §1)
	return t;
}

// ---- scene helpers ---------------------------------------------------------

namespace scene {

inline Prim box(PrimKind k, float x, float y, float w, float h, const Style& s) {
	Prim p; p.kind = k; p.x = x; p.y = y; p.w = w; p.h = h; p.style = s; return p;
}
inline Prim rect(float x, float y, float w, float h, const Style& s) { return box(PrimKind::Rect, x, y, w, h, s); }
inline Prim roundRect(float x, float y, float w, float h, const Style& s) { return box(PrimKind::RoundRect, x, y, w, h, s); }
inline Prim diamond(float x, float y, float w, float h, const Style& s) { return box(PrimKind::Diamond, x, y, w, h, s); }
inline Prim text(float x, float y, float w, float h, const std::wstring& t, const Style& s) {
	Prim p = box(PrimKind::Text, x, y, w, h, s); p.text = t; return p;
}
inline Prim line(float x1, float y1, float x2, float y2, const Style& s) {
	Prim p; p.kind = PrimKind::Line; p.x = x1; p.y = y1; p.x2 = x2; p.y2 = y2; p.style = s; return p;
}
inline Prim connector(float x1, float y1, float x2, float y2, const Style& s) {
	Prim p; p.kind = PrimKind::Connector; p.x = x1; p.y = y1; p.x2 = x2; p.y2 = y2; p.style = s; return p;
}

} // namespace scene
