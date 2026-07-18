// Pure, COM-free entity dump for the session recorder (R1a / SR-ENT-01..08).
//
// Serializes every rendered primitive as a uniform entity so harness and live
// recorder share one serializer (SR-ENT-07/08). No Windows/COM headers — the
// ops harness (ppops) includes this directly.
#pragma once

#include "GanttHitTest.h"

#include <cstdio>
#include <string>
#include <vector>

// Axis-aligned rect as [l,t,w,h] (slide points or screen pixels). Distinct from
// HtRect's half-open LTRB edges so the dump JSON matches the design contract
// without converting at every call site.
struct PpEntityRect {
	double l = 0;
	double t = 0;
	double w = 0;
	double h = 0;
};

struct PpEntityStyle {
	// 0xAARRGGBB (alpha optional; JSON "fill"/"stroke" emit #RRGGBB from the
	// lower 24 bits, matching docs/session-recorder-spec.md §R1a).
	unsigned long fillArgb = 0;
	unsigned long strokeArgb = 0;
	double strokeW = 0;
	double fontPx = 0;
};

struct PpEntityFlags {
	bool selectedOwn = false;
	bool selectedNative = false;
	bool hover = false;
	bool clipped = false;
	bool visible = true;
};

struct PpEntity {
	std::string id;
	std::string kind;
	std::string parentId;
	std::string rowId;
	PpEntityRect slideRect;
	PpEntityRect screenRect;
	int z = 0;
	PpEntityStyle style;
	std::string text;
	PpEntityFlags flags;
};

// Parent-link derivation for child visual prims (SR-ENT-03).
// Task-family children share the parent task's PP_ID — same grouping surface as
// IsTaskKind in GanttHitTest.h, but TASK itself is a root (parentId "").
// MILESTONE_LABEL links to its milestone id; all other kinds yield "".
inline std::string EntityParentId(const std::string& kind, const std::string& id) {
	if (kind == "TASK_LABEL" || kind == "TASK_PROGRESS" || kind == "TASK_PCT"
		|| kind == "RAIL_TASKLBL" || kind == "RAIL_DOT")
		return id;
	if (kind == "MILESTONE_LABEL")
		return id;
	return "";
}

// Linear slide-points -> screen-pixels projection (SR-ENT-04), pure analogue of
// DocumentWindow::PointsToScreenPixelsX/Y under uniform zoom (Overlay.cpp
// BuildRowBands maps each child via those COM calls).
//
//   screenL = originX + slideL * scaleX
//   screenT = originY + slideT * scaleY
//   screenW = slideW * scaleX
//   screenH = slideH * scaleY
//
// originX/Y = screen coords of slide point (0,0); scaleX/Y = pixels-per-point
// (the same role as PP_PROJ's ptPerDay-style scale factors once the day->point
// step is already applied — callers that already hold screen px/day can pass
// pxPerPt = pxPerDay / ptPerDay).
inline PpEntityRect EntityProjectSlideToScreen(const PpEntityRect& slide,
	double originX, double originY, double scaleX, double scaleY)
{
	PpEntityRect out;
	out.l = originX + slide.l * scaleX;
	out.t = originY + slide.t * scaleY;
	out.w = slide.w * scaleX;
	out.h = slide.h * scaleY;
	return out;
}

// ---- JSON helpers (manual concat, same style as Overlay_DumpChromeStateForTest)

inline void EntityJsonAppendEscaped(std::string& out, const std::string& in) {
	for (unsigned char c : in) {
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
				out += buf;
			} else {
				out += static_cast<char>(c);
			}
			break;
		}
	}
}

inline void EntityJsonAppendNumber(std::string& out, double v) {
	char buf[64];
	// Compact but stable: drop trailing zeros when integral, else a few decimals.
	if (v == static_cast<double>(static_cast<long long>(v))
		&& v >= -9007199254740992.0 && v <= 9007199254740992.0) {
		::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
	} else {
		::snprintf(buf, sizeof(buf), "%.6g", v);
	}
	out += buf;
}

inline void EntityJsonAppendHexColor(std::string& out, unsigned long argb) {
	char buf[16];
	::snprintf(buf, sizeof(buf), "#%06lX", argb & 0x00FFFFFFul);
	out += buf;
}

// PowerPoint scene styles are stored as 0x00BBGGRR; entity JSON uses RGB.
inline unsigned long EntityRgbFromBgr(unsigned long bgr) {
	return ((bgr & 0xFFul) << 16) | (bgr & 0xFF00ul) | ((bgr >> 16) & 0xFFul);
}

inline void EntityJsonAppendRect(std::string& out, const PpEntityRect& r) {
	out += '[';
	EntityJsonAppendNumber(out, r.l); out += ',';
	EntityJsonAppendNumber(out, r.t); out += ',';
	EntityJsonAppendNumber(out, r.w); out += ',';
	EntityJsonAppendNumber(out, r.h);
	out += ']';
}

inline void EntityJsonAppendBool(std::string& out, bool v) {
	out += v ? "true" : "false";
}

// Serialize entities to the §R1a shape: {"entities":[ {...}, ... ]}.
inline std::string EntityDumpToJson(const std::vector<PpEntity>& entities) {
	std::string s;
	s.reserve(entities.size() * 256 + 32);
	s += "{\"entities\":[";
	for (size_t i = 0; i < entities.size(); ++i) {
		const PpEntity& e = entities[i];
		if (i > 0) s += ',';
		s += '{';
		s += "\"id\":\""; EntityJsonAppendEscaped(s, e.id); s += "\",";
		s += "\"kind\":\""; EntityJsonAppendEscaped(s, e.kind); s += "\",";
		s += "\"parentId\":\""; EntityJsonAppendEscaped(s, e.parentId); s += "\",";
		s += "\"rowId\":\""; EntityJsonAppendEscaped(s, e.rowId); s += "\",";
		s += "\"slideRect\":"; EntityJsonAppendRect(s, e.slideRect); s += ',';
		s += "\"screenRect\":"; EntityJsonAppendRect(s, e.screenRect); s += ',';
		s += "\"z\":"; EntityJsonAppendNumber(s, static_cast<double>(e.z)); s += ',';
		s += "\"style\":{";
		s += "\"fill\":\""; EntityJsonAppendHexColor(s, e.style.fillArgb); s += "\",";
		s += "\"stroke\":\""; EntityJsonAppendHexColor(s, e.style.strokeArgb); s += "\",";
		s += "\"strokeW\":"; EntityJsonAppendNumber(s, e.style.strokeW); s += ',';
		s += "\"fontPx\":"; EntityJsonAppendNumber(s, e.style.fontPx);
		s += "},";
		s += "\"text\":\""; EntityJsonAppendEscaped(s, e.text); s += "\",";
		s += "\"flags\":{";
		s += "\"selectedOwn\":"; EntityJsonAppendBool(s, e.flags.selectedOwn); s += ',';
		s += "\"selectedNative\":"; EntityJsonAppendBool(s, e.flags.selectedNative); s += ',';
		s += "\"hover\":"; EntityJsonAppendBool(s, e.flags.hover); s += ',';
		s += "\"clipped\":"; EntityJsonAppendBool(s, e.flags.clipped); s += ',';
		s += "\"visible\":"; EntityJsonAppendBool(s, e.flags.visible);
		s += "}}";
	}
	s += "]}";
	return s;
}
