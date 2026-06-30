// A tiny device-neutral 2D scene: the chart is described as a list of styled
// primitives (in slide points), and a backend renderer (PptRenderer) emits them
// as native shapes. This decouples the visual vocabulary / theme from the
// PowerPoint object-model plumbing, so restyling (e.g. Material) is just a
// matter of building a different scene.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class PrimKind { Rect, RoundRect, Diamond, Line, Connector, Text };
enum class TextAlign { Left, Center, Right };

// Colors are BGR (PowerPoint's MsoRGBType byte order): 0x00BBGGRR.
struct Style {
	bool fill = false;            unsigned long fillBgr = 0;
	bool line = false;            unsigned long lineBgr = 0;  float lineWeight = 1.0f;
	unsigned long textBgr = 0;    float fontSize = 12.0f;     bool bold = false;
	TextAlign align = TextAlign::Left;
	bool arrowEnd = false;        // connectors
};

struct Prim {
	PrimKind kind;
	float x = 0, y = 0, w = 0, h = 0;  // box; for Line/Connector use (x,y)-(x2,y2)
	float x2 = 0, y2 = 0;
	std::wstring text;
	Style style;
	std::string tagKind, tagId;        // PP_KIND / PP_ID (empty = untagged)
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

inline Theme MaterialLight() {
	Theme t;
	t.surface          = 0xFFFFFF;
	t.railSurface      = 0xF1F3F4;
	t.divider          = 0xE8EAED;
	t.onSurface        = 0x202124;
	t.onSurfaceVariant = 0x5F6368;
	t.primary          = 0x1A73E8;  // Material blue
	t.primaryDark      = 0x1457B0;
	t.onPrimary        = 0xFFFFFF;
	t.milestone        = 0xF9AB00;  // amber
	t.summary          = 0x9AA0A6;  // grey
	t.bracket          = 0x80868B;
	t.connector        = 0xBDC1C6;
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
