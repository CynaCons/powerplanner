// N2: native shape emission. Maps the spec-conformant abstract layout
// (GanttLayout) to native PowerPoint shapes on the active slide.
#pragma once

#include "pch.h"
#include "GanttModel.h"

// A built-in sample chart (N3 replaces this with a parsed/round-tripped document).
PpDocument MakeSampleDocument();

// Emit the document as native shapes on the active slide, grouped under a tagged
// root carrying the serialized document (PP_DOC). pApp is the cached
// PowerPoint.Application (IDispatch). On success returns S_OK and sets *outShapeCount.
HRESULT InsertGantt(IDispatch* pApp, const PpDocument& doc, int* outShapeCount, const std::string& selectId = "");

// Diff-based rebuild: if an existing CHART_ROOT is present on the active
// slide, recompute the layout/scene for `doc` and reconcile it against the
// CURRENT children by (PP_KIND, PP_ID, ordinal) identity — move/resize and
// retext shapes in place, add only brand-new shapes, delete only removed
// ones, and refresh the group's PP_DOC/PP_PROJ tags. Ungroup/regroup only
// happens when the child SET actually changed (adds or removes); a pure
// move/resize/retext edit (drag, nudge, percent-without-crossing-zero,
// inline-edit text) never deletes or regroups anything, so the group's own
// COM shape identity and its total child count are stable across the call.
// Falls back to a full InsertGantt (delete + re-emit) if no CHART_ROOT is
// present yet, or if anything throws — callers should treat this the same
// as InsertGantt (S_OK / failure HRESULT), and never assume in-place
// reconciliation actually happened (check shape identity if that matters).
HRESULT UpdateGantt(IDispatch* pApp, const PpDocument& doc, const std::string& selectId = "");

// Read the embedded document JSON back from the active slide's chart group
// (PP_DOC on the CHART_ROOT). Returns "" if no PowerPlanner chart is present.
std::string ReadGanttFromSlide(IDispatch* pApp);

// N5: read each task shape's position back into dates (inverse projection via
// the PP_PROJ tag), update the document, and reflow the chart (re-emit) so
// dependent connectors/summary and PP_DOC stay in sync. Sets *outChanged.
HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged);

// V3-1 fit-to-slide: locate the CHART_ROOT group on the active slide (built by
// a prior InsertGantt at its natural, unscaled size) and resize/reposition it
// to fill the slide's content area — the full slide width minus ~18pt side
// margins, and the slide height below a reserved top title zone (~15% of
// slide height). Width always scales to fill the content width; height scales
// by that same factor unless the chart would then be shorter than the content
// area, in which case it scales up further to fill height too (non-uniform
// stretch is allowed — see task comment). GanttLayout/InsertGantt themselves
// are untouched (conformance fixtures stay byte-stable): this only moves/
// resizes the already-built group, then rewrites its PP_PROJ tag so the day
// <-> point projection matches the new (scaled) geometry, and finally calls
// ReflowFromSlide as a defensive re-sync (expected to be a no-op / changed
// false, since PP_PROJ was already corrected for the new scale). Returns
// S_FALSE if no CHART_ROOT is present, S_OK on success, or a failure HRESULT.
HRESULT FitChartRootToSlide(IDispatch* pApp);

// Parsed form of the CHART_ROOT's PP_PROJ tag payload
// ({"minDay":N,"pad":N,"ptPerDay":N,"originX":N}) — the day<->point projection
// used both by ReflowFromSlide and by the overlay's drag-gesture math. Returns
// false (leaving *out untouched) if projJson is empty or malformed
// (ptPerDay <= 0).
struct PpProj {
	long minDay = 0;
	long pad = 0;
	float ptPerDay = 1.0f;
	float originX = 0.0f;
};
bool ParseProj(const std::string& projJson, PpProj* out);
