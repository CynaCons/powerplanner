#pragma once

#include <windows.h>
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define PP_OVERLAY_METRICS_UNDEF_MIN
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define PP_OVERLAY_METRICS_UNDEF_MAX
#endif
#include <gdiplus.h>
#ifdef PP_OVERLAY_METRICS_UNDEF_MIN
#undef min
#undef PP_OVERLAY_METRICS_UNDEF_MIN
#endif
#ifdef PP_OVERLAY_METRICS_UNDEF_MAX
#undef max
#undef PP_OVERLAY_METRICS_UNDEF_MAX
#endif

// ---- DPI-scaled chrome metrics (96-DPI baseline) ---------------------------
const int kBaseInfl = 5;                    // frame inset from shape edge (px)
const int kBaseBadgeH = 20;                 // badge strip height (px)
const int kBaseToolbarH = 28;               // floating action toolbar height (px)
const int kBaseRowInsertButton = 16;
const int kBaseLinkPortRadius = 5;   // link-port circle radius @ 96 DPI (SR-DEP-04)
const int kBaseLinkPortGap = 4;      // gap outside bar edge to port center
const int kBaseButtonW = 32;
const int kBaseButtonH = 20;
const int kBaseButtonGap = 4;
const int kBaseGripSize = 16;
const int kBaseDragThresholdPx = 4;
const int kBaseTooltipPad = 5;
const Gdiplus::REAL kBaseTooltipFontPx = 10.0f;
const Gdiplus::REAL kBaseBadgeFontPx = 11.0f;
const Gdiplus::REAL kBaseButtonFontPx = 12.0f;

// ---- floating card editor metrics (96-DPI baseline, scaled via Scale()) ----
const int kBaseCardW = 260;
const int kBaseCardPad = 10;
const int kBaseCardRowH = 22;
const int kBaseCardRowGap = 6;
const int kBaseCardLabelW = 60;   // field-label column width
const int kBaseCardSwatchSize = 22;
const int kBaseCardSwatchGap = 6;
const int kBaseCardOkW = 60;
const int kBaseCardOkH = 24;
