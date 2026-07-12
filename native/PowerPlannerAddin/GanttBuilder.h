// N2: native shape emission. Maps the spec-conformant abstract layout
// (GanttLayout) to native PowerPoint shapes on the active slide.
#pragma once

#include "pch.h"
#include "GanttModel.h"

// A built-in sample chart (N3 replaces this with a parsed/round-tripped document).
PpDocument MakeSampleDocument();
PpDocument MakeBlankDocument();

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

// SR-SMO-02 op-dispatch fast path: hand back the parsed document last written
// to PP_DOC WITHOUT touching PowerPoint, but ONLY when the in-memory scene
// cache is valid AND owned by the group whose shape id is `chartRootShapeId`.
// The id gate is the exact trust boundary UpdateGantt's scene-diff fast path
// uses (g_sceneCacheValid && id == g_chartRootShapeId): the cached doc and the
// cached scene are written together in CommitSceneCache and dropped together in
// InvalidateSceneCache, so a cache hit here is coherent with the geometry the
// fast path will diff against. Returns true (deep-copying into *out) on a hit;
// false when the cache is invalid or belongs to a different group id (external
// undo/edit re-grouped the chart) — callers must then fall back to the full
// PP_DOC read + DocumentFromJson. Do NOT weaken the id check.
bool Gantt_TryGetCachedDoc(long chartRootShapeId, PpDocument* out);

// Cheap (no COM) accessor: hand back the cached pre-op document when the scene
// cache is valid, WITHOUT locating or id-verifying the CHART_ROOT shape. Use
// ONLY for RebuildChart's paint-lock structural heuristic (never a correctness
// path): the cache holds exactly the doc last written to PP_DOC — i.e. the
// pre-op state — so a COM walk to re-verify the group id would be pure overhead
// there. Returns false when the cache is invalid (caller falls back to the full
// id-checked read). Does NOT relax the op-path doc read's id check.
bool Gantt_TryPeekCachedDoc(PpDocument* out);

// Op-dispatch doc acquisition used by Overlay.cpp's mutation paths. When the
// scene cache is valid and the active slide id still matches g_cacheSlideId,
// copies the cached document via Gantt_TryGetCachedDoc WITHOUT walking every
// shape on the slide (SR-SMO-02 round4). Otherwise locates CHART_ROOT via the
// full walk, then either copies the cache (id-checked) or reads + parses PP_DOC.
// Timing: unless `accumulate` is true, this STARTS a fresh op doc-read window
// (zeroing docReadMs/dispatchTotalMs); the elapsed read time is added to
// g_lastOpPhases.docReadMs and docReadCached tracks whether every read in the
// window hit the cache. RebuildChart's structural pre-read passes accumulate =
// true so its (cache-served, ~0ms) read folds into the same op's docReadMs.
bool ReadGanttDocFromSlide(IDispatch* pApp, PpDocument* out, bool accumulate = false);

// N5: read each task shape's position back into dates (inverse projection via
// the PP_PROJ tag), update the document, and reflow the chart (re-emit) so
// dependent connectors/summary and PP_DOC stay in sync. Sets *outChanged.
HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged);

// Exact-fit primitive: locate the CHART_ROOT group on the active slide (built
// by a prior InsertGantt/UpdateGantt at some current size) and resize/
// reposition it to occupy PRECISELY the given target frame (left, top, width,
// height, in points) — an axis-independent resize (sx = width/currentWidth,
// sy = height/currentHeight, possibly != each other), the same semantics as a
// user dragging the group's resize handles to that exact rect. This makes the
// operation byte-exact/idempotent: calling it twice with the same rect
// reproduces the same rect (mod float rounding), which is what lets
// UpdateGantt use it to restore a chart's captured pre-edit frame exactly
// after a rebuild (see UpdateGantt's PreserveChartRootFrame). This function
// has NO aspect-ratio/no-distortion opinion of its own — that policy (uniform
// scale, letterboxing) lives in FitChartRootToSlide, which computes an
// already-uniform-scaled sub-rect and passes THAT here. GanttLayout/
// InsertGantt themselves are untouched (conformance fixtures stay
// byte-stable): this only moves/resizes the already-built group, then
// rewrites its PP_PROJ tag so the day<->point projection matches the new
// geometry, and finally calls ReflowFromSlide as a defensive re-sync
// (expected to be a no-op / changed false, since PP_PROJ was already
// corrected for the new scale). Returns S_FALSE if no CHART_ROOT is present,
// S_OK on success, or a failure HRESULT.
HRESULT FitChartRootToFrame(IDispatch* pApp, float left, float top, float width, float height);

// V3-1 fit-to-slide: computes the slide's content area (full width minus
// ~18pt side margins, height below a reserved top title zone of ~15% of
// slide height), then a UNIFORM scale s (single factor, never sx != sy, so
// text glyphs are never stretched/distorted — review finding #2) that fills
// the content width unless that would overflow the content height, in which
// case s fills height instead. The resulting sub-rect — already
// uniform-scaled, centered horizontally, top-aligned vertically within the
// content area (so a chart that doesn't fill the full content height
// letterboxes at the bottom rather than stretching) — is passed to
// FitChartRootToFrame. Returns S_FALSE if no CHART_ROOT is present, S_OK on
// success, or a failure HRESULT.
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

// Parsed form of the CHART_ROOT's PP_ROWY tag (row lane geometry in chart-local
// points — see BuildRowYJson in GanttScene.h).
struct PpRowYEntry {
	std::string id;
	float top = 0.0f;
	float bot = 0.0f;
	int lvl = 0;
	bool name = true;
};
struct PpRowY {
	float railL = 0.0f;
	float railR = 0.0f;
	float naturalW = 0.0f;
	float naturalH = 0.0f;
	std::vector<PpRowYEntry> rows;
};
bool ParseRowY(const std::string& rowYJson, PpRowY* out);
// Scale chart-local PP_ROWY coords after a group resize (sx/sy from
// FitChartRootToFrame). Returns the original string if parsing fails.
std::string ScaleRowYJson(const std::string& rowYJson, float sx, float sy);

// Last UpdateGantt phase breakdown for harness diagnostics (SR-SMO-02).
// Writes compact JSON into buf, e.g.
// {"sceneBuild":1,"keyCompare":0,"primWrites":12,"primWriteCount":4,...}
// Returns bytes written (excluding NUL) or 0 if buf is null/too small.
int Gantt_GetLastOpPhasesForTest(char* buf, int len);

// Record the total wall time of an app-bar op dispatch (Overlay.cpp brackets
// HandleAppBarCommand with an RAII timer). Stored into the op-phase diagnostics
// (dispatchTotalMs) so the OPPHASES trace shows the full outside-UpdateGantt
// cost, not just the phases measured inside UpdateGantt. Survives the
// ResetOpPhases done at UpdateGantt entry (set after it, at op end).
void Gantt_SetOpDispatchTotalMs(unsigned long long ms);
