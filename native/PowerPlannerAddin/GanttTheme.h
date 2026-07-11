// PowerPlanner design tokens — the single native source of truth for every
// color, and the material helpers (blend-on-white, hex parse, effective swatch)
// that the emitter and chrome consume. MIRRORS docs/design-tokens.md §1–5 and
// the mockup's CSS :root block (docs/mockup/onslide-mockup.html) EXACTLY. A
// change to a token here must land in docs/design-tokens.md in the same commit.
//
// Colors are stored RGB (0xRRGGBB). The PowerPoint boundary converts to BGR via
// Scene.h's Bgr() at emit time, so everything above the renderer stays RGB. This
// header is deliberately COM-free / windows-free so the pure ops harness can
// include it and assert on derived colors.
#pragma once

#include <string>

namespace gt {

// ---- §1 Color — neutrals & structure --------------------------------------
constexpr unsigned long ink        = 0x1B1D26; // primary text, milestone diamonds
constexpr unsigned long ink2       = 0x5D6273; // secondary text (axis top, notes, ms labels)
constexpr unsigned long ink3       = 0x9A9FB0; // tertiary text (rail header, chevrons, dep connectors)
constexpr unsigned long surface    = 0xFFFFFF; // slide / chart / app-bar background
constexpr unsigned long railSurface= 0xF6F7FB; // left rail fill
constexpr unsigned long headerBand = 0xF1F3F9; // axis header bands, segmented-control track
constexpr unsigned long rowAlt     = 0xFAFBFE; // alternate row banding (odd row indices)
constexpr unsigned long outline    = 0xE2E5EF; // row/grid hairlines, chart border, app-bar border
constexpr unsigned long outline2   = 0xD3D7E6; // axis band separators, month/major ticks, note borders
constexpr unsigned long chromeHairline = 0xCBD5E1; // overall selection frame (slate-300)

// ---- §2 Color — accent & semantic -----------------------------------------
constexpr unsigned long primary    = 0x4355E0; // selection chrome, today marker, rail selection
constexpr unsigned long primarySoft= 0xE8EBFC; // hover/selected fills
constexpr unsigned long primaryDim = 0x6B7DE8; // brand accents on dark surfaces
constexpr unsigned long deadline   = 0xC6362B; // deadline markers (stem + pill), destructive hover
constexpr unsigned long dangerSoft = 0xFBEAE8; // destructive button hover fill
constexpr unsigned long customMarker = 0x7A4FA3; // default color for custom markers

// ---- §3 Color — task-bar swatch palette (8) -------------------------------
// Order = swatch-picker order. PpTask.color empty ⇒ swatch1.
constexpr unsigned long swatch1 = 0x4355E0; // indigo
constexpr unsigned long swatch2 = 0x0E8D8A; // teal
constexpr unsigned long swatch3 = 0x7A4FA3; // plum
constexpr unsigned long swatch4 = 0x5B6C8F; // slate
constexpr unsigned long swatch5 = 0xB3552F; // rust
constexpr unsigned long swatch6 = 0x8B8E24; // olive
constexpr unsigned long swatch7 = 0xB23A6B; // magenta
constexpr unsigned long swatch8 = 0x2E7D6E; // pine
constexpr unsigned long kSwatches[8] = {
	swatch1, swatch2, swatch3, swatch4, swatch5, swatch6, swatch7, swatch8
};

// ---- §4 Layout dimensions (points) ----------------------------------------
// Color-independent tokens. The ones consumed today by the emitter are the bar
// material (radius); the rest are the normative values later S1 units wire in
// (rail width, axis bands, etc.) — defined here now so the source of truth is
// complete and single.
constexpr float bar_radius    = 5.0f;  // rounded-rectangle corner radius (bars)
constexpr float milestone_size = 16.0f;
constexpr float milestone_radius = 3.0f;
constexpr float marker_width  = 1.5f;
constexpr float hairline      = 0.75f; // row lines, week ticks
constexpr float hairline_major = 1.0f; // month/quarter/year boundary ticks
constexpr float dep_weight    = 1.5f;  // dependency elbow stroke
constexpr float bar_label_pad = 8.0f;  // horizontal inset for on-bar task labels
constexpr float marker_label_h = 12.0f;     // marker label row height (one stagger level)
constexpr float marker_label_gap = 4.0f;    // gap between marker line and label left edge
constexpr float marker_label_strip = 26.0f; // reserved two-level strip above axis headers
constexpr float rail_width    = 150.0f;
constexpr float axis_height   = 30.0f;

// ---- §4b Selection chrome (v2.5.1) ----------------------------------------
constexpr unsigned char chromeRowWashSelect = 18;  // row band wash when selected (0–255)
constexpr unsigned char chromeRowWashHover  = 10;  // row band wash on hover
constexpr unsigned char chromeChipAlpha     = 235; // "PowerPlanner" hover chip fill
constexpr float chromeChipRadius   = 3.0f;
constexpr float chromeChipFontPx   = 10.0f;
constexpr float chromeItemFramePx  = 1.0f; // task/milestone/marker/text frame

// ---- material helpers ------------------------------------------------------

// Pre-blend `rgb` at opacity `a` (0..1) over white to a solid RGB — the CSS
// `opacity` compositing used by the mockup's bar track (`.bar .fill{opacity:.4}`):
// out = rgb*a + white*(1-a), per channel, rounded to nearest.
inline unsigned long BlendOnWhite(unsigned long rgb, float a) {
	if (a < 0.0f) a = 0.0f; else if (a > 1.0f) a = 1.0f;
	unsigned long r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	auto mix = [&](unsigned long c) -> unsigned long {
		float v = (float)c * a + 255.0f * (1.0f - a);
		long iv = (long)(v + 0.5f);
		if (iv < 0) iv = 0; else if (iv > 255) iv = 255;
		return (unsigned long)iv;
	};
	return (mix(r) << 16) | (mix(g) << 8) | mix(b);
}

// Parse "#RRGGBB" (leading '#' optional) into 0xRRGGBB. Returns `fallback` for
// empty or malformed input (wrong length / non-hex digit).
inline unsigned long ParseHexColor(const std::string& s, unsigned long fallback) {
	if (s.empty()) return fallback;
	const char* p = s.c_str();
	if (*p == '#') ++p;
	std::string hex(p);
	if (hex.size() != 6) return fallback;
	unsigned long v = 0;
	for (char c : hex) {
		unsigned long d;
		if (c >= '0' && c <= '9') d = (unsigned long)(c - '0');
		else if (c >= 'a' && c <= 'f') d = (unsigned long)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') d = (unsigned long)(c - 'A' + 10);
		else return fallback;
		v = (v << 4) | d;
	}
	return v;
}

// A task's effective bar swatch: its explicit color if set, else swatch1.
inline unsigned long EffectiveSwatch(const std::string& color) {
	return ParseHexColor(color, swatch1);
}

// A per-element color override with a token default: explicit color if set and
// valid, else `dflt`.
inline unsigned long EffectiveColor(const std::string& color, unsigned long dflt) {
	return ParseHexColor(color, dflt);
}

} // namespace gt
