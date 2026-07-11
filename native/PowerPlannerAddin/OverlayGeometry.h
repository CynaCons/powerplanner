#pragma once

#include <windows.h>
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define PP_OVERLAY_GEOM_UNDEF_MIN
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define PP_OVERLAY_GEOM_UNDEF_MAX
#endif
#include <gdiplus.h>
#ifdef PP_OVERLAY_GEOM_UNDEF_MIN
#undef min
#undef PP_OVERLAY_GEOM_UNDEF_MIN
#endif
#ifdef PP_OVERLAY_GEOM_UNDEF_MAX
#undef max
#undef PP_OVERLAY_GEOM_UNDEF_MAX
#endif

inline Gdiplus::Color GpColor(BYTE a, COLORREF c) {
	return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

inline Gdiplus::Color GpToken(BYTE a, unsigned long rgb) {
	return Gdiplus::Color(a, (BYTE)((rgb >> 16) & 0xFF), (BYTE)((rgb >> 8) & 0xFF), (BYTE)(rgb & 0xFF));
}

inline void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
	Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
	if (w <= 0.0f || h <= 0.0f) return;
	Gdiplus::REAL maxR = (w < h ? w : h) / 2.0f;
	if (r > maxR) r = maxR;
	if (r <= 0.0f) {
		path.AddRectangle(Gdiplus::RectF(x, y, w, h));
		return;
	}
	Gdiplus::REAL d = r * 2.0f;
	path.StartFigure();
	path.AddArc(x, y, d, d, 180.0f, 90.0f);
	path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
	path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
	path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
	path.CloseFigure();
}

inline COLORREF AbRgb(unsigned long v) {
	return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

inline Gdiplus::Font* MakeAppBarFont(Gdiplus::REAL px, int style) {
	return new Gdiplus::Font(L"Segoe UI", px, style, Gdiplus::UnitPixel);
}

inline Gdiplus::REAL MeasureTextW(Gdiplus::Graphics& g, Gdiplus::Font& font, const wchar_t* text) {
	Gdiplus::RectF bounds;
	g.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), &bounds);
	return bounds.Width;
}
