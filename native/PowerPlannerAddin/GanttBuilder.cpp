#include "pch.h"
#include "GanttBuilder.h"
#include "GanttLayout.h"
#include "GanttJson.h"
#include "Scene.h"
#include "GanttScene.h"   // pure scene pipeline (Widen, layout constants, BuildGanttScene, BuildProjectedScene)
#include "PptRenderer.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstring>

// ---- helpers ---------------------------------------------------------------

// Widen() and the layout constants (MARGIN/ROW_GUTTER/...) now live in
// GanttScene.h (shared with the ops harness). Narrow() stays here — it is only
// used by the COM read paths below.
static std::string Narrow(const wchar_t* w) {
	if (!w || !*w) return "";
	int len = (int)::wcslen(w);
	int n = ::WideCharToMultiByte(CP_UTF8, 0, w, len, NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, NULL, NULL);
	return s;
}

// Shared debug log (same file/format as Overlay.cpp's OvLog / Connect.cpp's
// PpLog, so all three interleave in one place: %TEMP%\powerplanner-addin.log).
static void GbLog(const wchar_t* msg) {
	wchar_t path[MAX_PATH];
	DWORD n = ::GetTempPathW(MAX_PATH, path);
	if (!n || n > MAX_PATH) return;
	::wcscat_s(path, MAX_PATH, L"powerplanner-addin.log");
	HANDLE h = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	wchar_t pidBuf[48];
	::swprintf_s(pidBuf, 48, L"[ganttbuilder %lu @%lu] ", ::GetCurrentProcessId(), ::GetTickCount());
	std::wstring line = std::wstring(pidBuf) + msg + L"\r\n";
	DWORD w = 0; ::WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
	::CloseHandle(h);
}

// ---- sample document -------------------------------------------------------

PpDocument MakeSampleDocument() {
	PpDocument doc;
	doc.title = "Q3 Launch Plan";
	doc.rows = {
		{ "r_phase", "Phase 1", "" },
		{ "r_research", "Research", "r_phase" },
		{ "r_design", "Design", "r_phase" },
		{ "r_build", "Build", "" },
		{ "r_launch", "Launch", "" },
	};
	doc.tasks = {
		{ "t1", "Discovery",      "2026-06-01", "2026-06-12", "r_research", "", 100 },
		{ "t2", "Interviews",     "2026-06-08", "2026-06-19", "r_research", "", 60 },
		{ "t3", "Wireframes",     "2026-06-15", "2026-06-26", "r_design",   "", 40 },
		{ "t4", "Visual design",  "2026-06-22", "2026-07-10", "r_design",   "", 10 },
		{ "t5", "Implementation", "2026-07-06", "2026-07-31", "r_build",    "", 0  },
		{ "t6", "QA + polish",    "2026-07-27", "2026-08-07", "r_build",    "", 0  },
	};
	doc.milestones = {
		{ "m1", "Design freeze", "2026-07-10", "r_design",  "" },
		{ "m2", "Ship",          "2026-08-10", "r_launch",  "" },
	};
	doc.brackets = {
		{ "b1", "Delivery", "2026-07-06", "2026-08-07", "", { "r_build" } },
	};
	doc.deps = {
		{ "d1", "t1", "t3", "finish-to-start" },
		{ "d2", "t3", "t5", "finish-to-start" },
		{ "d3", "t5", "t6", "finish-to-start" },
	};
	doc.markers = {
		{ "mk1", "deadline", "Board review", "2026-07-30", "" },
		{ "mk2", "today",    "Today",        "2026-06-25", "" }
	};
	return doc;
}

PpDocument MakeBlankDocument() {
	PpDocument doc;
	doc.title = "Untitled Plan";
	doc.rows = { { "r1", "Row 1", "" } };
	return doc;
}

// ---- emission --------------------------------------------------------------

// Writes PP_DOC + PP_PROJ + PP_ROWY on the chart root in one place so the
// three tags can never skew. Defined in the anonymous namespace further down
// (next to the PP_ROWY helpers); declared here because InsertGantt/
// ReconcileChartRoot call it first.
namespace {
void WriteChartRootTags(PowerPoint::ShapePtr group, const PpDocument& doc, const std::string& minD,
	long pad, float ptPerDay, float slideW);
}
// Global-scope helper (defined below, after the PP_ROWY anon-namespace block);
// declared here so the fast path can rewrite PP_ROWY when row geometry changes.
std::string RebaseRowYJson(const std::string& rowYJson, float left, float top, float w, float h);

namespace {
static void CommitSceneCacheFromGroup(PowerPoint::ShapePtr group, const Scene& sc, const PpDocument& doc,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay);
static float GetSlideWidthCached(const PowerPoint::_ApplicationPtr& app, IDispatch* pApp);
static void StoreCacheSlideId(const PowerPoint::_SlidePtr& slide);
}

HRESULT InsertGantt(IDispatch* pApp, const PpDocument& doc, int* outShapeCount, const std::string& selectId) {
	if (!pApp) return E_POINTER;
	int count = 0;
	PowerPoint::_ApplicationPtr app(pApp);
	try {
		// Route through the per-app cache so the FIRST fast-path op after a
		// fresh insert reuses this width instead of paying the COM chain again.
		const float slideW = GetSlideWidthCached(app, pApp);
		PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		// Date range -> projection (pt/day) -> Scene, via the SAME shared path
		// ReconcileChartRoot uses (GanttScene.h's BuildProjectedScene), so the
		// two emit routes can never skew — including the rows-only fallback
		// window (no dated tasks/milestones => today..today+30d, SR-CRE-01),
		// which used to make this function fail outright on empty documents.
		Scene sc; std::string minD, maxD; long pad = 0; float ptPerDay = 0.0f;
		if (!BuildProjectedScene(doc, slideW, &sc, &minD, &maxD, &pad, &ptPerDay)) return E_FAIL;
		std::vector<PowerPoint::ShapePtr> emitted = RenderScene(shapes, sc);
		count = (int)emitted.size();

		if (emitted.size() >= 2) {
			SAFEARRAY* saf = ::SafeArrayCreateVector(VT_BSTR, 0, (ULONG)emitted.size());
			for (LONG i = 0; i < (LONG)emitted.size(); ++i) {
				_bstr_t nm = emitted[i]->GetName();
				::SafeArrayPutElement(saf, &i, (void*)(BSTR)nm);
			}
			_variant_t idx; idx.vt = VT_ARRAY | VT_BSTR; idx.parray = saf;
			PowerPoint::ShapeRangePtr range = shapes->Range(idx);
			PowerPoint::ShapePtr group = range->Group();
			group->GetTags()->Add(_bstr_t(L"PP_KIND"), _bstr_t(L"CHART_ROOT"));
			group->GetTags()->Add(_bstr_t(L"PP_VERSION"), _bstr_t(L"1"));
			WriteChartRootTags(group, doc, minD, pad, ptPerDay, slideW);

			if (!selectId.empty()) {
				PowerPoint::GroupShapesPtr items = group->GetGroupItems();
				long m = items->GetCount();
				for (long i = 1; i <= m; ++i) {
					PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
					_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
					if (id.length() && Narrow((const wchar_t*)id) == selectId) {
						ch->Select(Office::msoTrue);
						break;
					}
				}
			}
			CommitSceneCacheFromGroup(group, sc, doc, minD, maxD, pad, ptPerDay);
			StoreCacheSlideId(slide);
		}
	}
	catch (const _com_error& e) {
		if (outShapeCount) *outShapeCount = count;
		return e.Error() ? e.Error() : E_FAIL;
	}
	if (outShapeCount) *outShapeCount = count;
	return S_OK;
}

// ---- diff-based (in-place) rebuild -----------------------------------------
//
// A Prim's (tagKind, tagId) is not a unique shape identity by itself: several
// prims can legitimately share both (e.g. a bracket emits two BRACKET_TICK
// prims for the same bracket id; the TITLE prim carries a kind but no id, so
// several kind-only prims share ("KIND", "")). The match key adds an ordinal
// that counts repeats of the same (tagKind, tagId) pair IN EMISSION ORDER, so
// as long as BuildGanttScene stays deterministic for a given document shape
// (it does — same std::vector iteration order every call), the Nth prim of a
// given (kind, id) in the old scene always corresponds to the Nth prim of
// that (kind, id) in the new scene, letting untagged/repeated-id shapes still
// diff safely alongside uniquely-tagged ones.
namespace {
struct MatchKey {
	std::string kind, id;
	int ordinal;
	bool operator<(const MatchKey& o) const {
		if (kind != o.kind) return kind < o.kind;
		if (id != o.id) return id < o.id;
		return ordinal < o.ordinal;
	}
	bool operator==(const MatchKey& o) const {
		return kind == o.kind && id == o.id && ordinal == o.ordinal;
	}
};

std::vector<MatchKey> BuildMatchKeys(const std::vector<std::pair<std::string, std::string>>& kindIds) {
	std::map<std::pair<std::string, std::string>, int> seen;
	std::vector<MatchKey> out;
	out.reserve(kindIds.size());
	for (const auto& ki : kindIds) {
		int ord = seen[ki]++;
		out.push_back({ ki.first, ki.second, ord });
	}
	return out;
}

using ShapeMapKey = std::tuple<std::string, std::string, int>;

static ShapeMapKey ToShapeMapKey(const MatchKey& k) {
	return { k.kind, k.id, k.ordinal };
}

// §1 persistent scene cache (i4a2-scene-diff). ShapePtr entries may dangle if
// shapes are deleted externally (undo, manual edit); PP_DOC-tag drift check
// plus COM-failure fallback to the full reconcile path are the guards.
static Scene g_lastScene;
static std::map<ShapeMapKey, PowerPoint::ShapePtr> g_shapeMap;
static bool g_sceneCacheValid = false;
static long g_chartRootShapeId = 0;
// Cached CHART_ROOT group ShapePtr + the id of the slide it lives on (same
// trust model / dangle guard as g_shapeMap). Lets UpdateGantt's fast path reuse
// the group WITHOUT re-walking the slide — but ONLY after confirming the ACTIVE
// slide id still matches g_cacheSlideId (guards against slide/presentation
// switches leaving a valid-but-foreign group ptr) AND GetId()==g_chartRootShapeId
// (dangling/re-grouped charts throw or fail and fall through to the full walk).
static PowerPoint::ShapePtr g_cacheGroup;
static long g_cacheSlideId = 0;
static std::string g_cacheDocJson;
// Parsed form of g_cacheDocJson (== the doc last written to PP_DOC). Op paths
// copy this out via Gantt_TryGetCachedDoc to skip a PP_DOC tag read + parse.
static PpDocument g_cacheDoc;
static std::string g_cacheMinD, g_cacheMaxD;
// Window equality is part of projection/cache identity. A window delta changes
// minDay/ptPerDay even when every item id and scene key remains stable.
static std::string g_cacheWinStart, g_cacheWinEnd;
static long g_cachePad = 0;
static float g_cachePtPerDay = 0.0f;
// Slide width in points, cached per Application (constant unless page setup
// changes). Frozen alongside the scene projection so consecutive fast-path ops
// skip the GetActivePresentation->GetPageSetup->GetSlideWidth COM chain; any
// cache invalidation (structural edit, fast-path miss) forces a refetch.
static IDispatch* g_cacheSlideWApp = nullptr;
static float g_cacheSlideW = 0.0f;
static std::string g_cacheRowYJson;
static std::string g_cacheRowYPreRebase; // BuildRowYJson output before rebase (pure-C++ change detector)
static std::string g_lastAppliedSelectId;
// Set by ReadGanttDocFromSlide when it resolves the active slide; consumed once
// by UpdateGantt on the same op thread to skip a duplicate GetActiveWindow chain.
static long g_opHandoffSlideId = 0;
static PowerPoint::_SlidePtr g_opHandoffSlide;

struct OpPhaseTimings {
	uint64_t sceneBuildMs = 0;
	uint64_t keyCompareMs = 0;
	uint64_t primWritesMs = 0;
	int primWriteCount = 0;
	uint64_t docTagWriteMs = 0;
	uint64_t framePreserveMs = 0;
	uint64_t reselectMs = 0;
	// W3 structural-reconcile phase split (window commits are always
	// structural): pre-ungroup child snapshot walk, ungroup + post-ungroup
	// identity re-derive, survivor sync + delete/create churn, regroup+retag.
	uint64_t childWalkMs = 0;
	uint64_t ungroupMs = 0;
	uint64_t churnMs = 0;
	uint64_t regroupMs = 0;
	bool fastPathHit = false;
	bool rowYRewritten = false;
	// Cost of obtaining the document(s) for the op in Overlay.cpp's dispatch
	// path (op-path read + RebuildChart's structural read). Set by
	// ReadGanttDocFromSlide BEFORE UpdateGantt runs, so ResetOpPhases (called
	// at UpdateGantt entry) deliberately preserves these three fields.
	uint64_t docReadMs = 0;
	bool docReadCached = false;   // every doc read this op was served from cache
	uint64_t dispatchTotalMs = 0; // full app-bar op dispatch wall time (0 = unmeasured)
};
static OpPhaseTimings g_lastOpPhases;

static void ResetOpPhases() {
	// Preserve the doc-read / dispatch fields: they are recorded by the
	// Overlay op-dispatch path BEFORE UpdateGantt (hence before this reset),
	// so zeroing them here would erase the very measurement we report.
	const uint64_t docRead = g_lastOpPhases.docReadMs;
	const bool docCached = g_lastOpPhases.docReadCached;
	const uint64_t dispatch = g_lastOpPhases.dispatchTotalMs;
	g_lastOpPhases = {};
	g_lastOpPhases.docReadMs = docRead;
	g_lastOpPhases.docReadCached = docCached;
	g_lastOpPhases.dispatchTotalMs = dispatch;
}

static uint64_t ElapsedMs(ULONGLONG t0) {
	return ::GetTickCount64() - t0;
}

static void InvalidateSceneCache() {
	g_sceneCacheValid = false;
	g_shapeMap.clear();
	g_lastScene.prims.clear();
	g_chartRootShapeId = 0;
	g_cacheGroup = nullptr;
	g_cacheSlideId = 0;
	g_cacheDocJson.clear();
	g_cacheDoc = PpDocument{};
	g_cacheMinD.clear();
	g_cacheMaxD.clear();
	g_cacheWinStart.clear();
	g_cacheWinEnd.clear();
	g_cachePad = 0;
	g_cachePtPerDay = 0.0f;
	g_cacheSlideWApp = nullptr;
	g_cacheSlideW = 0.0f;
	g_cacheRowYJson.clear();
	g_cacheRowYPreRebase.clear();
	GanttJson_InvalidateParsedCache();
}

// Slide width (points) cached per Application; refetched when the cache was
// invalidated or the app object changed. See g_cacheSlideW rationale above.
static float GetSlideWidthCached(const PowerPoint::_ApplicationPtr& app, IDispatch* pApp) {
	if (pApp && pApp == g_cacheSlideWApp && g_cacheSlideW > 0.0f) return g_cacheSlideW;
	const float w = (float)app->GetActivePresentation()->GetPageSetup()->GetSlideWidth();
	if (w > 0.0f) { g_cacheSlideWApp = pApp; g_cacheSlideW = w; }
	return w;
}

// Bind the cached CHART_ROOT group to the slide it was just committed on, so
// UpdateGantt's fast path only reuses it while that slide is the active one.
static void StoreCacheSlideId(const PowerPoint::_SlidePtr& slide) {
	try { g_cacheSlideId = slide ? slide->GetSlideID() : 0; } catch (...) { g_cacheSlideId = 0; }
}

static std::vector<MatchKey> BuildScenePrimKeys(const Scene& sc) {
	std::vector<std::pair<std::string, std::string>> kindIds;
	kindIds.reserve(sc.prims.size());
	for (const auto& p : sc.prims) kindIds.push_back({ p.tagKind, p.tagId });
	return BuildMatchKeys(kindIds);
}

static bool PrimKeyMultisetEqual(const Scene& a, const Scene& b) {
	std::vector<MatchKey> ka = BuildScenePrimKeys(a);
	std::vector<MatchKey> kb = BuildScenePrimKeys(b);
	if (ka.size() != kb.size()) return false;
	std::sort(ka.begin(), ka.end());
	std::sort(kb.begin(), kb.end());
	return ka == kb;
}

// O(n) fast-path key gate: emission order is stable for in-place reconcile ops
// (nudge/color/percent). Falls back to the sorted multiset check on mismatch so
// we never reject a valid fast path when only prim order permutes.
static bool PrimKeysFastPathEqual(const Scene& a, const Scene& b) {
	if (a.prims.size() != b.prims.size()) return false;
	for (size_t i = 0; i < a.prims.size(); ++i) {
		if (a.prims[i].tagKind != b.prims[i].tagKind || a.prims[i].tagId != b.prims[i].tagId)
			return PrimKeyMultisetEqual(a, b);
	}
	std::vector<MatchKey> ka = BuildScenePrimKeys(a);
	std::vector<MatchKey> kb = BuildScenePrimKeys(b);
	for (size_t i = 0; i < ka.size(); ++i) {
		if (ka[i] < kb[i] || kb[i] < ka[i]) return PrimKeyMultisetEqual(a, b);
	}
	return true;
}

static bool PrimVisualFieldsEqual(const Prim& a, const Prim& b) {
	if (a.kind != b.kind) return false;
	if (a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h || a.x2 != b.x2 || a.y2 != b.y2) return false;
	if (a.clippedL != b.clippedL || a.clippedR != b.clippedR) return false;
	if (a.text != b.text) return false;
	const Style& sa = a.style, sb = b.style;
	return sa.fill == sb.fill && sa.fillBgr == sb.fillBgr
		&& sa.line == sb.line && sa.lineBgr == sb.lineBgr && sa.lineWeight == sb.lineWeight
		&& sa.textBgr == sb.textBgr && sa.fontSize == sb.fontSize && sa.bold == sb.bold
		&& sa.align == sb.align && sa.arrowEnd == sb.arrowEnd && sa.corner == sb.corner
		&& sa.dash == sb.dash;
}

static bool LineDashStyle(const Prim& p) {
	return p.style.dash || p.tagKind == "DEADLINE" || p.tagKind == "TODAY_LINE";
}

// Write-only delta apply for the scene-diff fast path — no COM reads back.
static void ApplyPrimWriteDelta(PowerPoint::ShapePtr ch, const Prim& oldP, const Prim& newP) {
	const Style& os = oldP.style;
	const Style& ns = newP.style;

	const bool styleChanged = os.fill != ns.fill || os.fillBgr != ns.fillBgr
		|| os.line != ns.line || os.lineBgr != ns.lineBgr || os.lineWeight != ns.lineWeight
		|| os.textBgr != ns.textBgr || os.fontSize != ns.fontSize || os.bold != ns.bold
		|| os.align != ns.align || os.arrowEnd != ns.arrowEnd || os.corner != ns.corner
		|| os.dash != ns.dash;

	if (newP.kind == PrimKind::Line || newP.kind == PrimKind::Connector) {
		if (oldP.x != newP.x || oldP.y != newP.y || oldP.x2 != newP.x2 || oldP.y2 != newP.y2) {
			ch->PutLeft((float)std::min(newP.x, newP.x2));
			ch->PutTop((float)std::min(newP.y, newP.y2));
			float w = std::fabs(newP.x2 - newP.x), h = std::fabs(newP.y2 - newP.y);
			ch->PutWidth(w > 0.0f ? w : 0.01f);
			ch->PutHeight(h > 0.0f ? h : 0.01f);
		}
		if (styleChanged || LineDashStyle(oldP) != LineDashStyle(newP)) {
			PowerPoint::LineFormatPtr lf = ch->GetLine();
			if (os.lineBgr != ns.lineBgr)
				lf->GetForeColor()->PutPpRGB((Office::MsoRGBType)ns.lineBgr);
			if (os.lineWeight != ns.lineWeight)
				lf->PutWeight(ns.lineWeight);
			if (newP.kind == PrimKind::Connector && os.arrowEnd != ns.arrowEnd) {
				lf->PutBeginArrowheadStyle(Office::msoArrowheadNone);
				lf->PutEndArrowheadStyle(ns.arrowEnd ? Office::msoArrowheadTriangle : Office::msoArrowheadNone);
			}
			if (LineDashStyle(oldP) != LineDashStyle(newP))
				lf->PutDashStyle(LineDashStyle(newP) ? Office::msoLineDashDot : Office::msoLineDash);
		}
		return;
	}

	if (oldP.x != newP.x) ch->PutLeft(newP.x);
	if (oldP.y != newP.y) ch->PutTop(newP.y);
	if (oldP.w != newP.w) ch->PutWidth(newP.w > 0.0f ? newP.w : 0.01f);
	if (oldP.h != newP.h) ch->PutHeight(newP.h > 0.0f ? newP.h : 0.01f);

	if (styleChanged) {
		PowerPoint::FillFormatPtr fill = ch->GetFill();
		if (os.fill != ns.fill)
			fill->PutVisible(ns.fill ? Office::msoTrue : Office::msoFalse);
		if (ns.fill && os.fillBgr != ns.fillBgr)
			fill->GetForeColor()->PutPpRGB((Office::MsoRGBType)ns.fillBgr);

		PowerPoint::LineFormatPtr line = ch->GetLine();
		if (os.line != ns.line)
			line->PutVisible(ns.line ? Office::msoTrue : Office::msoFalse);
		if (ns.line) {
			if (os.lineBgr != ns.lineBgr)
				line->GetForeColor()->PutPpRGB((Office::MsoRGBType)ns.lineBgr);
			if (os.lineWeight != ns.lineWeight)
				line->PutWeight(ns.lineWeight);
		}
	}

	if (newP.kind == PrimKind::RoundRect && os.corner != ns.corner && ns.corner > 0.0f) {
		float shorter = (newP.w < newP.h ? newP.w : newP.h);
		if (shorter > 0.0f) {
			float adj = ns.corner / shorter;
			if (adj < 0.0f) adj = 0.0f; else if (adj > 0.5f) adj = 0.5f;
			try { ch->GetAdjustments()->PutItem(1, adj); } catch (...) {}
		}
	}

	bool textChanged = oldP.text != newP.text;
	bool fontChanged = os.fontSize != ns.fontSize || os.bold != ns.bold || os.textBgr != ns.textBgr || os.align != ns.align;
	if (textChanged || fontChanged) {
		PowerPoint::TextFramePtr tf = ch->GetTextFrame();
		tf->PutAutoSize(PowerPoint::ppAutoSizeNone);
		PowerPoint::TextRangePtr tr = tf->GetTextRange();
		if (textChanged) tr->PutText(_bstr_t(newP.text.c_str()));
		if (fontChanged) {
			PowerPoint::FontPtr font = tr->GetFont();
			if (os.fontSize != ns.fontSize) font->PutSize(ns.fontSize);
			if (os.bold != ns.bold) font->PutBold(ns.bold ? Office::msoTrue : Office::msoFalse);
			if (os.textBgr != ns.textBgr) font->GetColor()->PutPpRGB((Office::MsoRGBType)ns.textBgr);
			if (os.align != ns.align) {
				long a = (ns.align == TextAlign::Center) ? 2 : (ns.align == TextAlign::Right) ? 3 : 1;
				tr->GetParagraphFormat()->PutAlignment((PowerPoint::PpParagraphAlignment)a);
			}
		}
	} else if (oldP.w != newP.w || oldP.h != newP.h) {
		// Geometry-only retext path still needs autosize suppressed.
		ch->GetTextFrame()->PutAutoSize(PowerPoint::ppAutoSizeNone);
	}
}

static void CommitSceneCache(PowerPoint::ShapePtr group, const Scene& sc, const PpDocument& doc,
	const std::vector<PowerPoint::ShapePtr>& shapesByPrimIndex,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay,
	const std::string* precomputedDocJson = nullptr) {
	g_lastScene = sc;
	// Reuse the JSON already serialized for the PP_DOC tag write when the caller
	// has it (fast path) — avoids a second DocumentToJson of the same document.
	g_cacheDocJson = precomputedDocJson ? *precomputedDocJson : DocumentToJson(doc);
	GanttJson_CommitParsedCache(g_cacheDocJson);
	g_cacheDoc = doc;
	g_chartRootShapeId = group->GetId();
	g_cacheGroup = group;
	g_cacheMinD = minD;
	g_cacheMaxD = maxD;
	g_cacheWinStart = doc.windowStart;
	g_cacheWinEnd = doc.windowEnd;
	g_cachePad = pad;
	g_cachePtPerDay = ptPerDay;
	try {
		_bstr_t rowYTag = group->GetTags()->Item(_bstr_t(L"PP_ROWY"));
		g_cacheRowYJson = rowYTag.length() ? Narrow((const wchar_t*)rowYTag) : "";
	} catch (...) {
		g_cacheRowYJson.clear();
	}
	g_shapeMap.clear();
	std::vector<MatchKey> primKeys = BuildScenePrimKeys(sc);
	for (size_t p = 0; p < sc.prims.size() && p < shapesByPrimIndex.size(); ++p) {
		if (shapesByPrimIndex[p]) g_shapeMap[ToShapeMapKey(primKeys[p])] = shapesByPrimIndex[p];
	}
	g_sceneCacheValid = true;
}

static bool BuildSceneForUpdate(const PpDocument& doc, float slideW, Scene* outScene,
	std::string* outMinD, std::string* outMaxD, long* outPad, float* outPtPerDay) {
	// SetTimeWindow/ClearTimeWindow are pure model ops. A window delta must
	// never reuse the cached projection — BOTH frozen-window branches below
	// gate on exact windowStart/End equality with the cache (and the scene-diff
	// fast path refuses window deltas up front), so a changed window always
	// falls through to a fresh BuildProjectedScene. The cache itself is KEPT
	// (not invalidated): ReconcileChartRoot needs g_lastScene/g_shapeMap to
	// sync the window commit with write-only per-field deltas
	// (window_commit_budget, SR-WIN-28); CommitSceneCache overwrites it with
	// the new projection when the update lands.
	if (g_sceneCacheValid && !g_cacheMinD.empty() && !g_cacheMaxD.empty()
		&& HasExplicitTimeWindow(doc)
		&& doc.windowStart == g_cacheWinStart && doc.windowEnd == g_cacheWinEnd) {
		*outMinD = g_cacheMinD;
		*outMaxD = g_cacheMaxD;
		*outPad = g_cachePad;
		*outPtPerDay = g_cachePtPerDay;
		return BuildSceneWithProjection(doc, slideW, outScene, *outMinD, *outMaxD, *outPad, *outPtPerDay);
	}
	if (g_sceneCacheValid && !g_cacheMinD.empty() && !g_cacheMaxD.empty()
		&& !HasExplicitTimeWindow(doc)
		&& g_cacheWinStart.empty() && g_cacheWinEnd.empty()
		&& DocDatesFitPaddedWindow(doc, g_cacheMinD, g_cacheMaxD, g_cachePad)) {
		*outMinD = g_cacheMinD;
		*outMaxD = g_cacheMaxD;
		*outPad = g_cachePad;
		*outPtPerDay = g_cachePtPerDay;
		return BuildSceneWithProjection(doc, slideW, outScene, *outMinD, *outMaxD, *outPad, *outPtPerDay);
	}
	return BuildProjectedScene(doc, slideW, outScene, outMinD, outMaxD, outPad, outPtPerDay);
}

static void CommitSceneCacheFromGroup(PowerPoint::ShapePtr group, const Scene& sc, const PpDocument& doc,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay) {
	PowerPoint::GroupShapesPtr items = group->GetGroupItems();
	long childCount = items->GetCount();
	std::vector<std::pair<std::string, std::string>> childKindIds(childCount ? childCount : 0);
	std::vector<PowerPoint::ShapePtr> children(childCount ? childCount : 0);
	for (long i = 1; i <= childCount; ++i) {
		PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
		children[i - 1] = ch;
		_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
		_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
		childKindIds[i - 1] = { k.length() ? Narrow((const wchar_t*)k) : "", id.length() ? Narrow((const wchar_t*)id) : "" };
	}
	std::vector<MatchKey> childKeys = BuildMatchKeys(childKindIds);
	std::map<MatchKey, PowerPoint::ShapePtr> childByKey;
	for (long i = 0; i < (long)childKeys.size(); ++i) {
		if (childKindIds[i].first.empty()) continue;
		childByKey[childKeys[i]] = children[i];
	}
	std::vector<MatchKey> primKeys = BuildScenePrimKeys(sc);
	std::vector<PowerPoint::ShapePtr> byPrim(sc.prims.size());
	for (size_t p = 0; p < sc.prims.size(); ++p) {
		auto it = childByKey.find(primKeys[p]);
		if (it != childByKey.end()) byPrim[p] = it->second;
	}
	CommitSceneCache(group, sc, doc, byPrim, minD, maxD, pad, ptPerDay);
}

// Fast-path tag write: PP_DOC only. PP_PROJ/PP_ROWY are unchanged when prim
// keys are stable (color/nudge/percent within the frozen projection window).
// Takes the pre-serialized JSON so the caller can reuse it for the scene cache.
static void WriteChartRootDocTag(PowerPoint::ShapePtr group, const std::string& docJson) {
	group->GetTags()->Add(_bstr_t(L"PP_DOC"), _bstr_t(Widen(docJson).c_str()));
}

// §2 fast path: pure C++ scene diff + write-only COM deltas (~10 calls/edit).
static bool TryApplySceneDiffFast(PowerPoint::ShapePtr group, const PpDocument& doc, const Scene& newSc,
	const std::string& minD, const std::string& maxD, long pad, float ptPerDay,
	const std::string& selectId, float slideW) {
	try {
		// A window commit changes PP_PROJ + PP_ROWY; this fast path only writes
		// PP_DOC. Refuse before any scene/key work even if all item keys match.
		if (doc.windowStart != g_cacheWinStart || doc.windowEnd != g_cacheWinEnd) return false;
		// Scene-union bounds (pure C++). When the new scene's natural bounds
		// differ from the old (e.g. a drag ends a lane overlap and rows
		// re-pack), the child Y-writes below would move/resize the CHART_ROOT
		// group's bounding box and the whole chart drifts on the slide
		// (v2.6.2-fix-round2: chart crept upward after a lane-collapsing
		// drag). In that case capture the group frame BEFORE the writes and
		// re-pin it after — the COM cost is paid only by lane-changing ops;
		// nudge/color keep zero extra calls.
		auto sceneBounds = [](const Scene& s, float* l, float* t, float* r, float* b) {
			*l = *t = 1e9f; *r = *b = -1e9f;
			for (const auto& p : s.prims) {
				if (p.x < *l) *l = p.x;
				if (p.y < *t) *t = p.y;
				if (p.x + p.w > *r) *r = p.x + p.w;
				if (p.y + p.h > *b) *b = p.y + p.h;
			}
		};
		float ol, ot, orr, ob, nl, nt, nr, nb;
		sceneBounds(g_lastScene, &ol, &ot, &orr, &ob);
		sceneBounds(newSc, &nl, &nt, &nr, &nb);
		const bool boundsChanged = std::fabs(ol - nl) > 0.01f || std::fabs(ot - nt) > 0.01f
			|| std::fabs(orr - nr) > 0.01f || std::fabs(ob - nb) > 0.01f;
		if (boundsChanged) {
			// Bail to the full reconcile: child writes that change the union
			// bounds move/resize the CHART_ROOT group, and a write-only frame
			// re-pin SCALES the children (PPT group semantics) — the slow path
			// owns frame preservation + full tag rewrite for this op class.
			// (Follow-up perf item registered in PLAN: fast-path lane changes.)
			return false;
		}

		const ULONGLONG tPrim0 = ::GetTickCount64();
		std::vector<MatchKey> newKeys = BuildScenePrimKeys(newSc);
		std::map<ShapeMapKey, const Prim*> oldPrimByKey;
		std::vector<MatchKey> oldKeys = BuildScenePrimKeys(g_lastScene);
		for (size_t i = 0; i < g_lastScene.prims.size(); ++i)
			oldPrimByKey[ToShapeMapKey(oldKeys[i])] = &g_lastScene.prims[i];

		for (size_t p = 0; p < newSc.prims.size(); ++p) {
			ShapeMapKey sk = ToShapeMapKey(newKeys[p]);
			auto oldIt = oldPrimByKey.find(sk);
			auto shIt = g_shapeMap.find(sk);
			if (oldIt == oldPrimByKey.end() || shIt == g_shapeMap.end()) return false;
			const Prim& oldP = *oldIt->second;
			const Prim& newP = newSc.prims[p];
			if (!PrimVisualFieldsEqual(oldP, newP)) {
				ApplyPrimWriteDelta(shIt->second, oldP, newP);
				++g_lastOpPhases.primWriteCount;
			}
		}
		g_lastOpPhases.primWritesMs = ElapsedMs(tPrim0);

		const ULONGLONG tDoc0 = ::GetTickCount64();
		std::string docJson = GanttJson_TryPatchFast(g_cacheDoc, doc);
		if (docJson.empty()) docJson = TryPatchDocJson(g_cacheDoc, doc, g_cacheDocJson);
		if (docJson.empty()) docJson = DocumentToJson(doc);
		WriteChartRootDocTag(group, docJson);
		g_lastOpPhases.docTagWriteMs = ElapsedMs(tDoc0);

		// Same-bounds row-geometry drift (rare: lane redistribution with equal
		// union bounds) still needs a fresh PP_ROWY. Pure C++ compare gates the
		// COM reads + tag write.
		{
			std::string newRowY = BuildRowYJson(doc, slideW, minD);
			if (!g_cacheRowYPreRebase.empty() && newRowY != g_cacheRowYPreRebase) {
				std::string rebased = RebaseRowYJson(newRowY, group->GetLeft(), group->GetTop(), group->GetWidth(), group->GetHeight());
				group->GetTags()->Add(_bstr_t(L"PP_ROWY"), _bstr_t(Widen(rebased).c_str()));
				g_cacheRowYJson = rebased;
				g_lastOpPhases.rowYRewritten = true;
			}
			g_cacheRowYPreRebase = newRowY;
		}

		std::vector<PowerPoint::ShapePtr> byPrim(newSc.prims.size());
		for (size_t p = 0; p < newSc.prims.size(); ++p) {
			auto it = g_shapeMap.find(ToShapeMapKey(newKeys[p]));
			if (it != g_shapeMap.end()) byPrim[p] = it->second;
		}
		CommitSceneCache(group, newSc, doc, byPrim, minD, maxD, pad, ptPerDay, &docJson);

		// No native child re-select on the fast path: shapes were never
		// deleted/recreated, so whatever native selection existed persists.
		// Selecting a child here also fights the SR-SHP suppression contract
		// (children are overlay-only selections; the poller would Unselect it
		// within 150ms anyway) and costs a ~60ms COM Select() per op.
		// Full reconcile keeps its re-select (shapes there ARE recreated).
		g_lastAppliedSelectId = selectId;
		return true;
	} catch (const _com_error&) {
		return false;
	} catch (...) {
		return false;
	}
}

// Mirror PptRenderer.cpp's fill/line writes so in-place reconcile keeps shape
// colors in sync with the scene (e.g. task swatch changes via app-bar).
void SyncShapeStyle(PowerPoint::ShapePtr ch, const Prim& prim) {
	const Style& s = prim.style;
	if (prim.kind == PrimKind::Line || prim.kind == PrimKind::Connector) {
		PowerPoint::LineFormatPtr lf = ch->GetLine();
		if ((long)lf->GetForeColor()->GetPpRGB() != (long)s.lineBgr)
			lf->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.lineBgr);
		if (lf->GetWeight() != s.lineWeight)
			lf->PutWeight(s.lineWeight);
		if (prim.kind == PrimKind::Connector) {
			lf->PutBeginArrowheadStyle(Office::msoArrowheadNone);
			lf->PutEndArrowheadStyle(s.arrowEnd ? Office::msoArrowheadTriangle : Office::msoArrowheadNone);
		}
		return;
	}
	PowerPoint::FillFormatPtr fill = ch->GetFill();
	if (s.fill) {
		if (fill->GetVisible() != Office::msoTrue)
			fill->PutVisible(Office::msoTrue);
		if ((long)fill->GetForeColor()->GetPpRGB() != (long)s.fillBgr)
			fill->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.fillBgr);
	} else if (fill->GetVisible() != Office::msoFalse) {
		fill->PutVisible(Office::msoFalse);
	}
	PowerPoint::LineFormatPtr line = ch->GetLine();
	if (s.line) {
		if (line->GetVisible() != Office::msoTrue)
			line->PutVisible(Office::msoTrue);
		if ((long)line->GetForeColor()->GetPpRGB() != (long)s.lineBgr)
			line->GetForeColor()->PutPpRGB((Office::MsoRGBType)s.lineBgr);
		if (line->GetWeight() != s.lineWeight)
			line->PutWeight(s.lineWeight);
	} else if (line->GetVisible() != Office::msoFalse) {
		line->PutVisible(Office::msoFalse);
	}
}

// Push one Prim's geometry (+ text, for non-line shapes) onto an EXISTING
// shape. Used by both the pure move/resize path and the survivor shapes in
// the structural (ungroup/regroup) path — geometry sync is identical either
// way, only what happens to the group afterward differs.
void SyncShapeGeometryAndText(PowerPoint::ShapePtr ch, const Prim& prim) {
	if (prim.kind == PrimKind::Line || prim.kind == PrimKind::Connector) {
		ch->PutLeft((float)std::min(prim.x, prim.x2));
		ch->PutTop((float)std::min(prim.y, prim.y2));
		float w = std::fabs(prim.x2 - prim.x), h = std::fabs(prim.y2 - prim.y);
		ch->PutWidth(w > 0.0f ? w : 0.01f);
		ch->PutHeight(h > 0.0f ? h : 0.01f);
		SyncShapeStyle(ch, prim);
		return;
	}
	ch->PutLeft(prim.x);
	ch->PutTop(prim.y);
	ch->PutWidth(prim.w > 0.0f ? prim.w : 0.01f);
	ch->PutHeight(prim.h > 0.0f ? prim.h : 0.01f);
	SyncShapeStyle(ch, prim);
	// Every AddShape/AddTextbox shape has a TextFrame; always reconcile text
	// (including clearing it if the prim's text became empty, e.g. a task
	// bar shrinking below the label-fit threshold).
	PowerPoint::TextFramePtr tf = ch->GetTextFrame();
	// AddShape/AddTextbox default the TextFrame to ppAutoSizeShapeToFitText:
	// left alone, PowerPoint grows the shape's Height to fit its text on a
	// LATER idle/redraw pass (not synchronously with PutText/PutHeight),
	// fighting the explicit Height set just above. This matters far more once
	// a shape is under UpdateGantt's diff-based reconcile: repeated
	// move/resize/retext edits each re-set Height, and a stray autosize-to-
	// fit-text growth on the NEXT tick silently corrupts row-band geometry
	// (observed empirically: ROW_LABEL boxes growing well past their intended
	// one-row height a tick after a reconcile, breaking row hit-testing).
	// ppAutoSizeNone makes Height/Width purely OURS to own; set it every sync
	// so it's re-asserted even if something external ever re-enabled it.
	tf->PutAutoSize(PowerPoint::ppAutoSizeNone);
	PowerPoint::TextRangePtr tr = tf->GetTextRange();
	_bstr_t cur = tr->GetText();
	std::wstring curText = cur.length() ? (const wchar_t*)cur : L"";
	while (!curText.empty() && (curText.back() == L'\r' || curText.back() == L'\n')) curText.pop_back();
	if (curText != prim.text) tr->PutText(_bstr_t(prim.text.c_str()));
}
} // namespace

// Reconcile `doc`'s freshly-computed scene against the EXISTING CHART_ROOT
// group's children. Returns true (and leaves the group's identity/child-set
// alone) when the reconciliation could be done via property mutation alone
// (or add/remove of only the shapes that actually changed); returns false if
// the caller should fall back to a full InsertGantt re-emit (e.g. the group
// vanished from under us, or an unexpected COM failure).
static HRESULT ReconcileChartRoot(PowerPoint::_ApplicationPtr app, PowerPoint::_SlidePtr slide,
	PowerPoint::ShapePtr group, const PpDocument& doc, const std::string& selectId) {
	const float slideW = (float)app->GetActivePresentation()->GetPageSetup()->GetSlideWidth();
	Scene sc; std::string minD, maxD; long pad = 0; float ptPerDay = 0.0f;
	if (!BuildProjectedScene(doc, slideW, &sc, &minD, &maxD, &pad, &ptPerDay)) return E_FAIL;

	PowerPoint::GroupShapesPtr items;
	long childCount = 0;
	try {
		items = group->GetGroupItems();
		childCount = items->GetCount();
	} catch (const _com_error&) {
		// W3/M2 recovery: a CHART_ROOT restored by an EXTERNAL undo can come
		// back with a broken GroupItems dispatch (observed 0x800A01A8 after
		// undoing a window commit). Fail the reconcile cleanly — UpdateGantt's
		// InsertGantt fallback then rebuilds the chart from the restored
		// PP_DOC instead of the exception escaping past the fallback.
		GbLog(L"ReconcileChartRoot: GroupItems unavailable (external undo?) - deferring to re-emit");
		return E_FAIL;
	}

	// Snapshot existing children + their match keys. When the scene cache
	// still owns this group AND accounts for every child, the identity walk is
	// answered from the cache (pure C++ + the already-held ShapePtrs) instead
	// of ~3 COM calls per child — the walk was the single largest COM block of
	// a window commit (W3 window_commit_budget). Any count mismatch (external
	// paste into the group, partial cache) falls back to the real walk.
	const ULONGLONG tChildWalk0 = ::GetTickCount64();
	std::vector<PowerPoint::ShapePtr> children(childCount ? childCount : 0);
	std::vector<std::pair<std::string, std::string>> childKindIds(childCount ? childCount : 0);
	bool childSnapshotFromCache = false;
	if (g_sceneCacheValid && g_chartRootShapeId != 0
		&& (long)g_shapeMap.size() == childCount && childCount > 0) {
		bool groupIdMatches = false;
		try { groupIdMatches = group->GetId() == g_chartRootShapeId; } catch (...) {}
		if (groupIdMatches) {
			long idx = 0;
			for (const auto& kv : g_shapeMap) {
				children[idx] = kv.second;
				childKindIds[idx] = { std::get<0>(kv.first), std::get<1>(kv.first) };
				++idx;
			}
			childSnapshotFromCache = true;
		}
	}
	if (!childSnapshotFromCache) {
		try {
			for (long i = 1; i <= childCount; ++i) {
				PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
				children[i - 1] = ch;
				_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
				_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
				childKindIds[i - 1] = { k.length() ? Narrow((const wchar_t*)k) : "", id.length() ? Narrow((const wchar_t*)id) : "" };
			}
		} catch (const _com_error&) {
			// Same recovery rule as the GroupItems guard above.
			GbLog(L"ReconcileChartRoot: child walk failed - deferring to re-emit");
			return E_FAIL;
		}
	}
	g_lastOpPhases.childWalkMs = ElapsedMs(tChildWalk0);
	std::vector<MatchKey> childKeys = BuildMatchKeys(childKindIds);
	std::map<MatchKey, long> childIndexByKey; // 0-based into children/childKeys
	for (long i = 0; i < (long)childKeys.size(); ++i) {
		// Untagged children (empty PP_KIND) are not shapes UpdateGantt ever
		// emitted itself — e.g. a shape the user pasted directly into the
		// group. Generated prims always carry a non-empty tagKind (see the
		// BuildGanttScene call sites above), so such a child can never
		// legitimately match a new prim; excluding it from the lookup map
		// here (rather than relying on "no prim happens to match") makes that
		// guarantee explicit and keeps it out of the diff in both directions:
		// it's never selected as an add-target AND (via childMatched being
		// forced true below) never falls into the "removed" bucket either.
		if (childKindIds[i].first.empty()) continue;
		childIndexByKey[childKeys[i]] = i;
	}

	// New-scene match keys.
	std::vector<std::pair<std::string, std::string>> primKindIds;
	primKindIds.reserve(sc.prims.size());
	for (const auto& p : sc.prims) primKindIds.push_back({ p.tagKind, p.tagId });
	std::vector<MatchKey> primKeys = BuildMatchKeys(primKindIds);

	// W3 window_commit_budget (SR-WIN-28): when the scene cache still owns this
	// group, the cached g_lastScene IS the shapes' current visual truth (same
	// trust boundary as TryApplySceneDiffFast; the PP_DOC drift probe drops the
	// cache on external edits). Reuse it to sync surviving shapes with the fast
	// path's WRITE-ONLY per-field deltas (ApplyPrimWriteDelta) instead of the
	// full read+rewrite SyncShapeGeometryAndText: a window commit rescales
	// x/width on content prims but leaves rail/row geometry, all text and all
	// style untouched, so the delta form cuts ~10-15 COM calls per child to
	// 0-4 (measured 3.9s -> the 2s budget on the showcase doc). Cache miss or
	// unknown key falls back to the full sync — never a correctness trade.
	//
	// IN-FRAME reconcile (plan §2.4 "the chart KEEPS its slide footprint"): a
	// fitted/user-resized chart's shapes sit at an AFFINE TRANSFORM T of the
	// scene's natural coordinates (T maps the old scene union onto the group's
	// current frame). All geometry below is therefore written through T —
	// survivors, created shapes and PP_PROJ alike — so the reconciled chart
	// lands exactly on its previous footprint with NO post-hoc group refit
	// (the old natural-write + FitChartRootToFrame re-pin rescaled every child
	// per commit, which both blew the ≤2s budget and made re-emitted geometry
	// path-dependent, breaking SR-WIN-20 losslessness). When the cache is not
	// trusted, T degrades to identity: natural writes + UpdateGantt's frame
	// re-pin, the pre-W3 behavior.
	const bool reconcileCacheTrusted = g_sceneCacheValid && g_chartRootShapeId != 0
		&& [&]() { try { return group->GetId() == g_chartRootShapeId; } catch (...) { return false; } }();
	std::map<ShapeMapKey, const Prim*> oldPrimByKey;
	float tSx = 1.0f, tSy = 1.0f, tDx = 0.0f, tDy = 0.0f; // x' = x*tSx + tDx
	bool frameTransformValid = false;
	if (reconcileCacheTrusted) {
		std::vector<MatchKey> oldKeys = BuildScenePrimKeys(g_lastScene);
		for (size_t i = 0; i < g_lastScene.prims.size(); ++i)
			oldPrimByKey[ToShapeMapKey(oldKeys[i])] = &g_lastScene.prims[i];
		try {
			float ol = 1e9f, ot = 1e9f, orr = -1e9f, ob = -1e9f;
			for (const auto& p : g_lastScene.prims) {
				const bool isLine = (p.kind == PrimKind::Line || p.kind == PrimKind::Connector);
				ol = std::min(ol, isLine ? std::min(p.x, p.x2) : p.x);
				orr = std::max(orr, isLine ? std::max(p.x, p.x2) : p.x + p.w);
				ot = std::min(ot, isLine ? std::min(p.y, p.y2) : p.y);
				ob = std::max(ob, isLine ? std::max(p.y, p.y2) : p.y + p.h);
			}
			const float unionW = orr - ol, unionH = ob - ot;
			const float capL = group->GetLeft(), capT = group->GetTop();
			const float capW = group->GetWidth(), capH = group->GetHeight();
			if (unionW > 1.0f && unionH > 1.0f && capW > 1.0f && capH > 1.0f) {
				tSx = capW / unionW;
				tSy = capH / unionH;
				tDx = capL - ol * tSx;
				tDy = capT - ot * tSy;
				frameTransformValid = true;
			}
			// The union-derived X mapping is only as accurate as the scene's
			// NOMINAL prim rects — autosized TEXT overhangs make the group's
			// real bounding box wider than the nominal union, so capW/unionW
			// drifts off the true fitted scale (observed: empty-cell creates
			// landing 2 days off on a fit-to-slide chart, overlay_lifecycle
			// CREATEEMPTY). The group's live PP_PROJ was rewritten by the
			// exact fit that scaled the shapes, so ptPerDayOld/ptPerDayNatural
			// IS the true X scale and originXOld the true fitted origin —
			// prefer them whenever parseable. Y keeps the union estimate: no
			// Y projection tag exists, and vertical noise cannot move dates.
			if (frameTransformValid) {
				_bstr_t projTag = group->GetTags()->Item(_bstr_t(L"PP_PROJ"));
				PpProj projOld;
				if (projTag.length()
					&& ParseProj(Narrow((const wchar_t*)projTag), &projOld)
					&& projOld.ptPerDay > 0.0f && g_cachePtPerDay > 0.0f) {
					tSx = projOld.ptPerDay / g_cachePtPerDay;
					tDx = projOld.originX - (MARGIN + ROW_GUTTER) * tSx;
				}
			}
		} catch (...) {
			frameTransformValid = false;
			tSx = tSy = 1.0f; tDx = tDy = 0.0f;
		}
	}
	auto transformPrim = [&](const Prim& p) {
		if (!frameTransformValid) return p;
		Prim out = p;
		out.x = p.x * tSx + tDx;
		out.w = p.w * tSx;
		out.x2 = p.x2 * tSx + tDx;
		out.y = p.y * tSy + tDy;
		out.h = p.h * tSy;
		out.y2 = p.y2 * tSy + tDy;
		return out;
	};
	auto syncSurvivorShape = [&](PowerPoint::ShapePtr ch, size_t primIdx) {
		const Prim& newP = sc.prims[primIdx];
		if (reconcileCacheTrusted) {
			auto oldIt = oldPrimByKey.find(ToShapeMapKey(primKeys[primIdx]));
			if (oldIt != oldPrimByKey.end()) {
				if (!PrimVisualFieldsEqual(*oldIt->second, newP)) {
					ApplyPrimWriteDelta(ch, transformPrim(*oldIt->second), transformPrim(newP));
					++g_lastOpPhases.primWriteCount;
				}
				return;
			}
			// Trusted cache but unknown key: full sync IN FRAME SPACE so the
			// shape stays coherent with its delta-written siblings.
			SyncShapeGeometryAndText(ch, transformPrim(newP));
			++g_lastOpPhases.primWriteCount;
			return;
		}
		SyncShapeGeometryAndText(ch, newP);
		++g_lastOpPhases.primWriteCount;
	};
	// PP_PROJ must describe the same space the shapes were written in. The
	// natural-space tag write (WriteChartRootTags) is corrected through T for
	// the in-frame reconcile; PP_ROWY needs no correction (WriteChartRootTags
	// rebases it onto the group's LIVE frame after the writes).
	auto correctProjTagForFrame = [&](PowerPoint::ShapePtr tagGroup) {
		if (!frameTransformValid) return;
		if (std::fabs(tSx - 1.0f) < 0.0005f && std::fabs(tDx) < 0.01f) return;
		char proj[192];
		::sprintf_s(proj, "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%.6f,\"originX\":%.4f}",
			DateToDays(minD), pad, ptPerDay * tSx, (MARGIN + ROW_GUTTER) * tSx + tDx);
		tagGroup->GetTags()->Add(_bstr_t(L"PP_PROJ"), _bstr_t(Widen(proj).c_str()));
	};

	// Classify: which new prims match an existing child (update-in-place) vs.
	// are brand new (must be added); which existing children have no matching
	// new prim (must be removed).
	std::vector<bool> childMatched(childCount ? childCount : 0, false);
	// Untagged children always "survive": mark them matched up front so they
	// are never geometry-synced (they hold no prim) and never counted among
	// the removed set below; they still flow into finalOrder/regroup via the
	// structural-path carry-through added alongside this fix.
	for (long i = 0; i < childCount; ++i) {
		if (childKindIds[i].first.empty()) childMatched[i] = true;
	}
	std::vector<long> primMatchChildIdx(sc.prims.size(), -1); // -1 = needs add
	for (size_t p = 0; p < primKeys.size(); ++p) {
		auto it = childIndexByKey.find(primKeys[p]);
		if (it != childIndexByKey.end() && !childMatched[it->second]) {
			primMatchChildIdx[p] = it->second;
			childMatched[it->second] = true;
		}
	}
	bool anyRemoved = false;
	for (long i = 0; i < childCount; ++i) if (!childMatched[i]) { anyRemoved = true; break; }
	bool anyAdded = false;
	for (size_t p = 0; p < primMatchChildIdx.size(); ++p) if (primMatchChildIdx[p] < 0) { anyAdded = true; break; }

	if (!anyAdded && !anyRemoved) {
		// Pure move/resize/retext: mutate matched children in place, no
		// ungroup/regroup, no delete/recreate. Group identity is untouched.
		const ULONGLONG tPrim0 = ::GetTickCount64();
		for (size_t p = 0; p < sc.prims.size(); ++p) {
			syncSurvivorShape(children[primMatchChildIdx[p]], p);
		}
		g_lastOpPhases.primWritesMs = ElapsedMs(tPrim0);
		WriteChartRootTags(group, doc, minD, pad, ptPerDay, slideW);
		correctProjTagForFrame(group);

		{
			std::vector<PowerPoint::ShapePtr> byPrim(sc.prims.size());
			for (size_t p = 0; p < sc.prims.size(); ++p) byPrim[p] = children[primMatchChildIdx[p]];
			CommitSceneCache(group, sc, doc, byPrim, minD, maxD, pad, ptPerDay);
		}

		// UF-14: no native child re-select — it flashed PowerPoint grips over
		// our chrome until the suppression sink mirrored it. The overlay owns
		// item selection (ownSel); mirror the fast path's bookkeeping only.
		if (!selectId.empty()) g_lastAppliedSelectId = selectId;
		return S_OK;
	}

	// Structural change (adds and/or removes): ungroup, apply property
	// updates to the shapes that survive, delete the ones that didn't match,
	// add brand-new shapes for the unmatched new prims, then regroup and
	// retag. This still avoids the OLD behavior of nuking EVERY shape: only
	// the actually-added/removed elements churn identity.
	//
	// IMPORTANT: Shape::Ungroup() invalidates the pre-ungroup Shape references
	// obtained via GroupShapes::Item() (confirmed empirically — reusing them
	// after Ungroup() silently writes properties onto the WRONG now-top-level
	// shape, observed as ROW_LABEL shapes acquiring some unrelated shape's
	// height). Re-derive every surviving shape from the POST-ungroup range by
	// re-reading its PP_KIND/PP_ID tags (tags belong to the shape, not the
	// group, so they survive Ungroup intact) and re-keying with the SAME
	// MatchKey scheme used for the pre-ungroup snapshot.
	const ULONGLONG tUngroup0 = ::GetTickCount64();
	PowerPoint::ShapeRangePtr ungrouped = group->Ungroup();
	long ungroupedCount = ungrouped->GetCount();
	std::vector<PowerPoint::ShapePtr> postUngroupShapes(ungroupedCount ? ungroupedCount : 0);
	std::vector<std::pair<std::string, std::string>> postUngroupKindIds(ungroupedCount ? ungroupedCount : 0);
	for (long i = 1; i <= ungroupedCount; ++i) {
		PowerPoint::ShapePtr sh = ungrouped->Item(_variant_t(i));
		postUngroupShapes[i - 1] = sh;
		_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
		_bstr_t id = sh->GetTags()->Item(_bstr_t(L"PP_ID"));
		postUngroupKindIds[i - 1] = { k.length() ? Narrow((const wchar_t*)k) : "", id.length() ? Narrow((const wchar_t*)id) : "" };
	}

	// From here on, the group no longer exists as a single shape: its former
	// children are loose top-level shapes on the slide (and RenderScene below
	// adds more). Once Ungroup() above has succeeded, ANY exception thrown by
	// the diff/sync/render logic that follows must not be allowed to propagate
	// to UpdateGantt's catch block as-is: that would land in the InsertGantt
	// fallback with all these loose shapes still sitting on the slide (never
	// cleaned up, since they're no longer tagged CHART_ROOT as a group and so
	// aren't found by the fallback's "delete any remaining CHART_ROOT" pass),
	// producing orphaned litter plus a duplicate freshly-inserted chart. Catch
	// everything here, best-effort delete every post-ungroup shape (survivors
	// and to-be-removed alike) and anything newly rendered, then return E_FAIL
	// so InsertGantt starts from a clean slide. The finalOrder.size()<2 guard
	// below is folded into this same cleanup path for the same reason (it's
	// also a "the structural rebuild didn't come together" case).
	std::vector<PowerPoint::ShapePtr> renderedThisAttempt;
	auto cleanupLoose = [&]() {
		for (auto& sh : postUngroupShapes) {
			if (!sh) continue;
			try { sh->Delete(); } catch (...) {}
		}
		for (auto& sh : renderedThisAttempt) {
			if (!sh) continue;
			try { sh->Delete(); } catch (...) {}
		}
	};

	g_lastOpPhases.ungroupMs = ElapsedMs(tUngroup0);

	try {
		const ULONGLONG tChurn0 = ::GetTickCount64();
		std::vector<MatchKey> postUngroupKeys = BuildMatchKeys(postUngroupKindIds);
		std::map<MatchKey, long> postUngroupIndexByKey;
		for (long i = 0; i < (long)postUngroupKeys.size(); ++i) postUngroupIndexByKey[postUngroupKeys[i]] = i;

		// Map ORIGINAL child index (pre-ungroup) -> post-ungroup Shape, keyed by
		// the SAME (kind, id, ordinal) that identified it before the ungroup.
		std::map<long, PowerPoint::ShapePtr> survivorByOldIdx;
		// Untagged (PP_KIND-empty) survivors carry through to finalOrder as-is —
		// no prim ever maps to them (see childIndexByKey construction above), so
		// they'd otherwise never be picked up by the sc.prims-driven loop below
		// and would end up left behind, ungrouped, on the slide. Collected
		// separately and appended after the tagged loop so ordering of the
		// tagged/diffed shapes (which callers may rely on, e.g. selectId lookup
		// by prim index) is unaffected.
		std::vector<PowerPoint::ShapePtr> untaggedSurvivors;
		std::vector<bool> postUngroupConsumed(postUngroupShapes.size(), false);
		for (long i = 0; i < childCount; ++i) {
			if (childKindIds[i].first.empty()) {
				// Untagged child: never deleted, never geometry-synced, always
				// carried through. Re-derive its post-ungroup Shape the same way
				// as any other survivor (by re-keying on kind/id/ordinal).
				auto it = postUngroupIndexByKey.find(childKeys[i]);
				if (it != postUngroupIndexByKey.end() && !postUngroupConsumed[it->second]) {
					postUngroupConsumed[it->second] = true;
					untaggedSurvivors.push_back(postUngroupShapes[it->second]);
				}
				continue;
			}
			if (!childMatched[i]) {
				auto it = postUngroupIndexByKey.find(childKeys[i]);
				if (it != postUngroupIndexByKey.end() && !postUngroupConsumed[it->second]) {
					postUngroupConsumed[it->second] = true;
					try { postUngroupShapes[it->second]->Delete(); } catch (const _com_error&) {}
				}
				continue;
			}
			auto it = postUngroupIndexByKey.find(childKeys[i]);
			if (it != postUngroupIndexByKey.end() && !postUngroupConsumed[it->second]) {
				postUngroupConsumed[it->second] = true;
				survivorByOldIdx[i] = postUngroupShapes[it->second];
			}
		}

		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		std::vector<PowerPoint::ShapePtr> finalOrder;
		finalOrder.reserve(sc.prims.size() + untaggedSurvivors.size());
		for (size_t p = 0; p < sc.prims.size(); ++p) {
			const Prim& prim = sc.prims[p];
			auto survIt = (primMatchChildIdx[p] >= 0) ? survivorByOldIdx.find(primMatchChildIdx[p]) : survivorByOldIdx.end();
			if (survIt != survivorByOldIdx.end()) {
				PowerPoint::ShapePtr ch = survIt->second;
				syncSurvivorShape(ch, p);
				finalOrder.push_back(ch);
			} else {
				// Created shapes render IN FRAME SPACE (see transformPrim) so
				// they land coherently among the delta-written survivors.
				Scene one; one.prims.push_back(transformPrim(prim));
				std::vector<PowerPoint::ShapePtr> emitted = RenderScene(shapes, one);
				for (auto& e : emitted) renderedThisAttempt.push_back(e);
				if (!emitted.empty()) finalOrder.push_back(emitted[0]);
			}
		}
		for (auto& ch : untaggedSurvivors) finalOrder.push_back(ch);
		g_lastOpPhases.churnMs = ElapsedMs(tChurn0);

		if (finalOrder.size() < 2) { cleanupLoose(); return E_FAIL; } // nothing sane to regroup

		const ULONGLONG tRegroup0 = ::GetTickCount64();
		SAFEARRAY* saf = ::SafeArrayCreateVector(VT_BSTR, 0, (ULONG)finalOrder.size());
		for (LONG i = 0; i < (LONG)finalOrder.size(); ++i) {
			_bstr_t nm = finalOrder[i]->GetName();
			::SafeArrayPutElement(saf, &i, (void*)(BSTR)nm);
		}
		_variant_t idx; idx.vt = VT_ARRAY | VT_BSTR; idx.parray = saf;
		PowerPoint::ShapeRangePtr range = shapes->Range(idx);
		PowerPoint::ShapePtr newGroup = range->Group();
		newGroup->GetTags()->Add(_bstr_t(L"PP_KIND"), _bstr_t(L"CHART_ROOT"));
		newGroup->GetTags()->Add(_bstr_t(L"PP_VERSION"), _bstr_t(L"1"));
		WriteChartRootTags(newGroup, doc, minD, pad, ptPerDay, slideW);
		correctProjTagForFrame(newGroup);
		g_lastOpPhases.regroupMs = ElapsedMs(tRegroup0);

		{
			std::vector<PowerPoint::ShapePtr> byPrim(sc.prims.size());
			for (size_t p = 0; p < sc.prims.size(); ++p) byPrim[p] = finalOrder[p];
			CommitSceneCache(newGroup, sc, doc, byPrim, minD, maxD, pad, ptPerDay);
		}

		// UF-14: no native child re-select on the wholesale path either (see
		// the in-place branch above).
		if (!selectId.empty()) g_lastAppliedSelectId = selectId;
		return S_OK;
	} catch (...) {
		GbLog(L"ReconcileChartRoot: exception during post-ungroup rebuild, cleaning up loose shapes");
		cleanupLoose();
		return E_FAIL;
	}
}

// Frame-preserving rebuild (review finding #1, see GanttBuilder.h): re-find
// the CHART_ROOT (its Shape identity may have changed if ReconcileChartRoot
// took the structural ungroup/regroup path) and, if its frame drifted from
// `captured` (BuildProjectedScene/ReconcileChartRoot always (re)write NATURAL,
// unscaled coordinates), re-apply the captured frame via FitChartRootToFrame
// so a fitted/user-resized chart's footprint survives the rebuild instead of
// visibly snapping back to natural size. A tolerance of 0.25pt avoids
// re-applying (and re-triggering a defensive ReflowFromSlide) for float noise
// when the frame in fact didn't change.
static void PreserveChartRootFrame(IDispatch* pApp, PowerPoint::_SlidePtr slide,
	float capLeft, float capTop, float capWidth, float capHeight) {
	try {
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		PowerPoint::ShapePtr group;
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) return;

		// W3: re-created TEXT children autosize to their content, so a lossless
		// re-emission can grow the group's union a few points past the exact
		// in-frame target (e.g. an anchored note re-appearing at a fitted
		// scale). Re-pinning for that noise would uniformly rescale EVERY
		// child (breaking SR-WIN-20's exact restore); only a REAL footprint
		// drift (untrusted-cache natural rebuild, user-visible movement) may
		// refit. Tolerance: 1.5% of the frame dimension, floored at 4pt.
		const float kTolX = std::max(4.0f, capWidth * 0.015f);
		const float kTolY = std::max(4.0f, capHeight * 0.015f);
		bool driftedFrame = (std::fabs(group->GetLeft() - capLeft) > kTolX)
			|| (std::fabs(group->GetTop() - capTop) > kTolY)
			|| (std::fabs(group->GetWidth() - capWidth) > kTolX)
			|| (std::fabs(group->GetHeight() - capHeight) > kTolY);
		{
			wchar_t dbg[192];
			::swprintf_s(dbg, 192, L"frame-preserve: actual=(%.2f,%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f,%.2f) refit=%d",
				group->GetLeft(), group->GetTop(), group->GetWidth(), group->GetHeight(),
				capLeft, capTop, capWidth, capHeight, driftedFrame ? 1 : 0);
			GbLog(dbg);
		}
		if (!driftedFrame) return;

		// No defensive reflow here: the reconcile that just ran wrote coherent
		// PP_DOC/PP_PROJ/PP_ROWY for these shapes, and FitChartRootToFrame
		// itself rescales PP_PROJ/PP_ROWY for the refit — the read-back could
		// only re-derive identical dates at full child-walk cost (and under an
		// explicit window, fewer ReflowFromSlide passes = less C1 surface).
		FitChartRootToFrame(pApp, capLeft, capTop, capWidth, capHeight, /*defensiveReflow=*/false);
	} catch (const _com_error&) {
	} catch (...) {}
}

HRESULT UpdateGantt(IDispatch* pApp, const PpDocument& doc, const std::string& selectId) {
	if (!pApp) return E_POINTER;
	PowerPoint::_ApplicationPtr app(pApp);
	ResetOpPhases();

	try {
		// Resolve the ACTIVE slide up front — needed both to gate cached-group
		// reuse and by the slow path. Reuse the slide id ReadGanttDocFromSlide
		// already fetched on this op thread when available (SR-SMO-02 round4).
		long activeSlideId = 0;
		PowerPoint::_SlidePtr slide;
		if (g_opHandoffSlide) {
			slide = g_opHandoffSlide;
			g_opHandoffSlide = nullptr;
			activeSlideId = g_opHandoffSlideId;
			g_opHandoffSlideId = 0;
		}
		PowerPoint::DocumentWindowPtr win;
		if (!slide) {
			win = app->GetActiveWindow();
			slide = win->GetView()->GetSlide();
		}
		if (activeSlideId == 0) {
			try { activeSlideId = slide->GetSlideID(); } catch (...) { activeSlideId = 0; }
		}

		PowerPoint::ShapesPtr shapes;

		// Fast-path group reuse (SR-SMO-02): reuse the cached CHART_ROOT ShapePtr
		// (skip the per-shape slide walk) ONLY when it is (a) on the ACTIVE slide
		// (activeSlideId == g_cacheSlideId — guards slide/presentation switches
		// that leave a valid-but-foreign group ptr) and (b) still the same live
		// chart (GetId()==g_chartRootShapeId; a dangling ptr throws and falls
		// through). Same COM-failure-fallback trust model as g_shapeMap; never
		// weakens correctness — any miss takes the authoritative walk below.
		PowerPoint::ShapePtr group;
		bool groupIdValidated = false; // true when GetId() was checked THIS op (skip re-check at the fast-path gate)
		if (g_sceneCacheValid && g_cacheGroup && activeSlideId != 0 && activeSlideId == g_cacheSlideId) {
			try {
				if (g_cacheGroup->GetId() == g_chartRootShapeId) { group = g_cacheGroup; groupIdValidated = true; }
			} catch (const _com_error&) { group = nullptr; }
		}
		if (!group) {
			shapes = slide->GetShapes();
			long n = shapes->GetCount();
			for (long i = 1; i <= n; ++i) {
				PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
				_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") { group = sh; break; }
			}
			if (!group) {
				// No existing chart to reconcile against: full emit.
				int cnt = 0;
				HRESULT hr = InsertGantt(pApp, doc, &cnt, selectId);
				return hr;
			}
		}

		const float slideW = GetSlideWidthCached(app, pApp);
		Scene newSc; std::string minD, maxD; long pad = 0; float ptPerDay = 0.0f;
		{
			const ULONGLONG tScene0 = ::GetTickCount64();
			if (!BuildSceneForUpdate(doc, slideW, &newSc, &minD, &maxD, &pad, &ptPerDay)) return E_FAIL;
			g_lastOpPhases.sceneBuildMs = ElapsedMs(tScene0);
		}

		// §2 scene-diff fast path: skip full reconcile when prim keys are stable.
		// Trust the in-memory scene cache (no per-op PP_DOC tag re-read — SR-SMO-02
		// v2.5.3-latency-green); stale ShapePtrs or external tag edits fall back via
		// TryApplySceneDiffFast / ReconcileChartRoot failure.
		bool fastPathEligible = false;
		{
			const ULONGLONG tKey0 = ::GetTickCount64();
			fastPathEligible = g_sceneCacheValid
				&& (groupIdValidated || group->GetId() == g_chartRootShapeId)
				&& PrimKeysFastPathEqual(newSc, g_lastScene);
			g_lastOpPhases.keyCompareMs = ElapsedMs(tKey0);
		}
		if (fastPathEligible) {
			if (TryApplySceneDiffFast(group, doc, newSc, minD, maxD, pad, ptPerDay, selectId, slideW)) {
				g_lastOpPhases.fastPathHit = true;
				g_cacheSlideId = activeSlideId; // keep group<->slide binding coherent
				// Child-only deltas never move the CHART_ROOT frame — skip the
				// slide-wide scan + frame read that PreserveChartRootFrame does.
				return S_OK;
			}
			InvalidateSceneCache();
		}

		// Slow path (reconcile / re-emit) needs the live shapes; if the group came
		// from the cache we skipped GetShapes above, so fetch it now. This is the
		// rare path (structural edits / fast-path misses).
		if (!shapes) shapes = slide->GetShapes();

		// SR-WIN-20 losslessness on window CLEAR: clearing a window re-spans the
		// full-width derived prims (AXIS_BANDDIV and the grid) from the windowed
		// projection back to the full auto-fit extent. Under the trusted-cache
		// reconcile those re-spanned prims land via the PP_PROJ-derived transform
		// T, whose rounding diverges from the FIT path (FitChartRootToFrame) that
		// built the pre-window state — the divergence is invisible on date-anchored
		// bars (T pins date->x exactly) but accumulates at the axis divider's far
		// end (observed: AXIS_BANDDIV ~1pt origin / ~5.5pt width drift, breaking
		// window-clip-rerender's window_lossless_reemission at 0.1pt tol). A window
		// clear is a cold, rare op, so drop the scene cache for this one reconcile:
		// that degrades T to identity (natural writes) + PreserveChartRootFrame's
		// FitChartRootToFrame re-pin — byte-identical math to the fresh InsertGantt
		// build that produced the pre state — so the cleared chart is lossless.
		// The window COMMIT path keeps the cache (SR-WIN-28 delta budget); only the
		// windowed->unwindowed transition takes this clean-rebuild route.
		if (!HasExplicitTimeWindow(doc) && !g_cacheWinStart.empty()) {
			InvalidateSceneCache();
		}

		// Capture the CHART_ROOT's frame BEFORE reconciling — this is whatever
		// frame the chart currently has (fitted, user-resized, or natural), and
		// is what must survive the rebuild below (review finding #1).
		const float capLeft = group->GetLeft();
		const float capTop = group->GetTop();
		const float capWidth = group->GetWidth();
		const float capHeight = group->GetHeight();

		// W3 (SR-WIN-20/28) post-mortem: an earlier revision transformed this
		// target by the old-scene->new-scene PRIM-UNION delta so a union change
		// (window commit hiding an overhanging element, row add) would move the
		// frame with it. That trusted the union as the chart's footprint — but
		// out-of-window MARKERS/notes are a legitimate scene state under the
		// auto-fit projection (ComputeDocDateExtents spans tasks+milestones
		// only), and their overhang made the frame WALK (observed: rows-only
		// chart at Left=-475pt, width 2x, overlay_lifecycle CREATEEMPTY). The
		// captured frame verbatim is the correct target: the natural chart-body
		// X extent is invariant by construction (a window/doc delta rescales
		// ptPerDay into the SAME content width), and a refit that squeezes new
		// overhang back into the footprint stays fully consistent because
		// FitChartRootToFrame rescales PP_PROJ with the shapes and the
		// reconcile's T is derived from PP_PROJ (not from union estimates).
		const float targetLeft = capLeft, targetTop = capTop, targetWidth = capWidth, targetHeight = capHeight;

		HRESULT hr = ReconcileChartRoot(app, slide, group, doc, selectId);
		if (SUCCEEDED(hr)) {
			const ULONGLONG tFrame0 = ::GetTickCount64();
			PreserveChartRootFrame(pApp, slide, targetLeft, targetTop, targetWidth, targetHeight);
			g_lastOpPhases.framePreserveMs = ElapsedMs(tFrame0);
			g_cacheSlideId = activeSlideId; // ReconcileChartRoot re-committed the cache on this slide
			if (!selectId.empty()) g_lastAppliedSelectId = selectId;
			return hr;
		}

		wchar_t buf[96];
		::swprintf_s(buf, 96, L"UpdateGantt: ReconcileChartRoot failed hr=0x%08lX, falling back to InsertGantt", (unsigned long)hr);
		GbLog(buf);

		// Reconciliation failed for some reason: fall back to a full re-emit.
		// The group may be in a partially-ungrouped state if ReconcileChartRoot
		// threw mid-way; best effort cleanup: delete any remaining CHART_ROOT
		// before re-inserting so we never end up with two.
		try {
			long n2 = shapes->GetCount();
			for (long i = 1; i <= n2; ++i) {
				PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
				_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
				if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") { sh->Delete(); break; }
			}
		} catch (const _com_error&) {}
		InvalidateSceneCache();
		int cnt = 0;
		HRESULT hr2 = InsertGantt(pApp, doc, &cnt, selectId);
		return hr2;
	}
	catch (const _com_error& e) {
		// Best-effort fallback even on an unexpected COM error during the
		// reconcile attempt itself.
		wchar_t buf[96];
		::swprintf_s(buf, 96, L"UpdateGantt: COM error 0x%08lX during reconcile, falling back to InsertGantt", (unsigned long)e.Error());
		GbLog(buf);
		InvalidateSceneCache();
		try {
			int cnt = 0;
			HRESULT hr = InsertGantt(pApp, doc, &cnt, selectId);
			return hr;
		} catch (...) {}
		return e.Error() ? e.Error() : E_FAIL;
	}
	catch (const std::exception&) {
		GbLog(L"UpdateGantt: std::exception during reconcile, falling back to InsertGantt");
		InvalidateSceneCache();
		try {
			int cnt = 0;
			HRESULT hr = InsertGantt(pApp, doc, &cnt, selectId);
			return hr;
		} catch (...) {}
		return E_FAIL;
	}
	catch (...) {
		GbLog(L"UpdateGantt: unknown exception during reconcile, falling back to InsertGantt");
		InvalidateSceneCache();
		try {
			int cnt = 0;
			HRESULT hr = InsertGantt(pApp, doc, &cnt, selectId);
			return hr;
		} catch (...) {}
		return E_FAIL;
	}
}

std::string ReadGanttFromSlide(IDispatch* pApp) {
	if (!pApp) return "";
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT")
				return Narrow((const wchar_t*)sh->GetTags()->Item(_bstr_t(L"PP_DOC")));
		}
	} catch (const _com_error&) { return ""; }
	return "";
}

bool Gantt_TryGetCachedDoc(long chartRootShapeId, PpDocument* out) {
	if (!out) return false;
	// Same trust boundary as UpdateGantt's scene-diff fast path: valid cache
	// AND owned by this exact group id. Deep-copies so the caller may mutate
	// freely without touching the cache.
	if (!g_sceneCacheValid || chartRootShapeId != g_chartRootShapeId) return false;
	*out = g_cacheDoc;
	return true;
}

bool Gantt_TryPeekCachedDoc(PpDocument* out) {
	if (!out || !g_sceneCacheValid) return false;
	*out = g_cacheDoc;
	return true;
}

void Gantt_SetOpDispatchTotalMs(unsigned long long ms) {
	g_lastOpPhases.dispatchTotalMs = (uint64_t)ms;
}

bool ReadGanttDocFromSlide(IDispatch* pApp, PpDocument* out, bool accumulate) {
	if (!out) return false;
	const ULONGLONG t0 = ::GetTickCount64();
	if (!accumulate) {
		// Start a fresh op doc-read window. dispatchTotalMs is (re)set here too
		// so an op that never reaches HandleAppBarCommand's RAII timer (hotkey,
		// drag commit, ...) honestly reports 0 rather than a stale value; the
		// timer overwrites it at op end for app-bar dispatches.
		g_lastOpPhases.docReadMs = 0;
		g_lastOpPhases.docReadCached = true;
		g_lastOpPhases.dispatchTotalMs = 0;
	}
	bool ok = false;
	bool cached = false;
	if (pApp) {
		try {
			// Fast path: scene cache owns the last-written doc for this chart on
			// this slide — skip the per-shape tag walk when the active slide id
			// still matches (same guard as UpdateGantt's cached-group reuse).
			if (g_sceneCacheValid && g_chartRootShapeId != 0 && g_cacheSlideId != 0) {
				PowerPoint::_ApplicationPtr app(pApp);
				PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
				long activeSlideId = 0;
				try { activeSlideId = slide->GetSlideID(); } catch (...) {}
				if (activeSlideId != 0 && activeSlideId == g_cacheSlideId) {
					g_opHandoffSlideId = activeSlideId;
					g_opHandoffSlide = slide;
					// M2 / SR-WIN-27 PP_DOC drift probe: an EXTERNAL undo/redo or
					// manual tag edit rewrites PP_DOC without passing through
					// CommitSceneCache, so serving g_cacheDoc here would resurrect
					// the undone document (e.g. re-apply an undone time window on
					// the next nudge). One cheap tag read on the cached group ptr
					// (no shape walk) confirms the cache still owns the slide's
					// truth; any mismatch OR COM failure (dangling group after a
					// structural undo) drops the whole scene cache and falls
					// through to the full read below. Probed once per op: the
					// accumulate read (RebuildChart's second read inside the SAME
					// synchronous dispatch) cannot observe a world the op-start
					// read did not — skipping it keeps the nudge fast path inside
					// its 200ms budget.
					bool docTagMatches = accumulate;
					if (!docTagMatches) {
						try {
							_bstr_t docTag = g_cacheGroup->GetTags()->Item(_bstr_t(L"PP_DOC"));
							docTagMatches = docTag.length()
								&& Narrow((const wchar_t*)docTag) == g_cacheDocJson;
						} catch (...) {
							docTagMatches = false;
						}
					}
					if (!docTagMatches) {
						InvalidateSceneCache();
					} else if (Gantt_TryGetCachedDoc(g_chartRootShapeId, out)) {
						ok = true;
						cached = true;
					}
				}
			}
			if (!ok) {
				g_opHandoffSlideId = 0;
				g_opHandoffSlide = nullptr;
				PowerPoint::_ApplicationPtr app(pApp);
				PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
				long activeSlideId = 0;
				try { activeSlideId = slide->GetSlideID(); } catch (...) {}
				if (activeSlideId != 0) {
					g_opHandoffSlideId = activeSlideId;
					g_opHandoffSlide = slide;
				}
				PowerPoint::ShapesPtr shapes = slide->GetShapes();
				long n = shapes->GetCount();
				for (long i = 1; i <= n; ++i) {
					PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
					_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
					if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") {
						const long id = sh->GetId();
						// M2 drift probe (same rule as the fast branch above): only
						// serve the cached doc when the slide's PP_DOC still equals
						// the JSON the cache was committed with; otherwise the tag
						// was rewritten externally (undo/redo) — drop the cache and
						// parse the tag truth.
						std::string json;
						try {
							json = Narrow((const wchar_t*)sh->GetTags()->Item(_bstr_t(L"PP_DOC")));
						} catch (...) {}
						if (g_sceneCacheValid && id == g_chartRootShapeId && json != g_cacheDocJson)
							InvalidateSceneCache();
						if (Gantt_TryGetCachedDoc(id, out)) {
							ok = true;
							cached = true;
						} else if (!json.empty()) {
							// Cache miss (invalid, drifted, or a different group id
							// after an external undo/edit): full parse fallback.
							*out = DocumentFromJson(json);
							ok = true;
						}
						break;
					}
				}
			}
		} catch (const _com_error&) {
			g_opHandoffSlideId = 0;
			g_opHandoffSlide = nullptr;
			ok = false;
			cached = false;
		}
	}
	g_lastOpPhases.docReadMs += ElapsedMs(t0);
	// docReadCached == "were ALL reads this op served from cache" (AND across
	// the op-path read and RebuildChart's read).
	g_lastOpPhases.docReadCached = g_lastOpPhases.docReadCached && cached;
	return ok;
}

bool ParseProj(const std::string& projJson, PpProj* out) {
	if (!out || projJson.empty()) return false;
	PpProj p;
	int n = ::sscanf_s(projJson.c_str(), "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%f,\"originX\":%f}",
		&p.minDay, &p.pad, &p.ptPerDay, &p.originX);
	if (n != 4 || p.ptPerDay <= 0.0f) return false;
	*out = p;
	return true;
}

bool ParseRowY(const std::string& rowYJson, PpRowY* out) {
	if (!out || rowYJson.empty()) return false;
	PpRowY ry;
	int n = ::sscanf_s(rowYJson.c_str(), "{\"railL\":%f,\"railR\":%f,\"naturalW\":%f,\"naturalH\":%f",
		&ry.railL, &ry.railR, &ry.naturalW, &ry.naturalH);
	if (n < 4 || ry.naturalH <= 0.0f || ry.naturalW <= 0.0f) return false;

	const char* rowsKey = ::strstr(rowYJson.c_str(), "\"rows\":[");
	if (!rowsKey) return false;
	const char* p = rowsKey + 8;
	while (*p && *p != ']') {
		if (*p == '{') {
			PpRowYEntry e;
			char idBuf[128] = {};
			float top = 0.0f, bot = 0.0f;
			int lvl = 0;
			char nameTok[8] = {};
			int rn = ::sscanf_s(p, "{\"id\":\"%127[^\"]\",\"top\":%f,\"bot\":%f,\"lvl\":%d,\"name\":%7s}",
				idBuf, (unsigned)sizeof(idBuf), &top, &bot, &lvl, nameTok, (unsigned)sizeof(nameTok));
			if (rn >= 5 && idBuf[0]) {
				e.id = idBuf;
				e.top = top;
				e.bot = bot;
				e.lvl = lvl;
				e.name = (nameTok[0] == 't');
				ry.rows.push_back(e);
			}
			while (*p && *p != '}') ++p;
		}
		++p;
	}
	if (ry.rows.empty()) return false;
	*out = ry;
	return true;
}

namespace {
std::string SerializeRowYJson(const PpRowY& ry) {
	std::string out;
	char head[192];
	::sprintf_s(head, "{\"railL\":%.4f,\"railR\":%.4f,\"naturalW\":%.4f,\"naturalH\":%.4f,\"rows\":[",
		ry.railL, ry.railR, ry.naturalW, ry.naturalH);
	out = head;
	for (size_t i = 0; i < ry.rows.size(); ++i) {
		if (i) out += ",";
		const auto& e = ry.rows[i];
		char rowBuf[160];
		::sprintf_s(rowBuf, "{\"id\":\"%s\",\"top\":%.4f,\"bot\":%.4f,\"lvl\":%d,\"name\":%s}",
			e.id.c_str(), e.top, e.bot, e.lvl, e.name ? "true" : "false");
		out += rowBuf;
	}
	out += "]}";
	return out;
}
} // namespace

std::string ScaleRowYJson(const std::string& rowYJson, float sx, float sy) {
	PpRowY ry;
	if (!ParseRowY(rowYJson, &ry)) return rowYJson;
	ry.railL *= sx;
	ry.railR *= sx;
	ry.naturalW *= sx;
	ry.naturalH *= sy;
	for (auto& e : ry.rows) {
		e.top *= sy;
		e.bot *= sy;
	}
	return SerializeRowYJson(ry);
}

std::string RebaseRowYJson(const std::string& rowYJson, float left, float top, float w, float h) {
	PpRowY ry;
	if (!ParseRowY(rowYJson, &ry)) return rowYJson;
	ry.railL -= left;
	ry.railR -= left;
	ry.naturalW = w;
	for (auto& e : ry.rows) {
		e.top -= top;
		e.bot -= top;
	}
	// After frame-preserving rebuilds the group's height can differ from the
	// layout footprint BuildRowYJson emitted (row add/delete while the fitted
	// frame is held). Map row bands through the live bbox so every row stays
	// non-degenerate and the stack still fills naturalH.
	float contentH = 0.0f;
	for (const auto& e : ry.rows) contentH = std::max(contentH, e.bot);
	if (contentH > 0.0f && h > 0.0f && std::fabs(contentH - h) > 0.25f) {
		const float sy = h / contentH;
		for (auto& e : ry.rows) {
			e.top *= sy;
			e.bot *= sy;
		}
	}
	ry.naturalH = h;
	return SerializeRowYJson(ry);
}

namespace {
void WriteChartRootTags(PowerPoint::ShapePtr group, const PpDocument& doc, const std::string& minD,
	long pad, float ptPerDay, float slideW) {
	group->GetTags()->Add(_bstr_t(L"PP_DOC"), _bstr_t(Widen(DocumentToJson(doc)).c_str()));
	char proj[192];
	::sprintf_s(proj, "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%.6f,\"originX\":%.4f}",
		DateToDays(minD), pad, ptPerDay, MARGIN + ROW_GUTTER);
	group->GetTags()->Add(_bstr_t(L"PP_PROJ"), _bstr_t(Widen(proj).c_str()));
	std::string rowY = BuildRowYJson(doc, slideW, minD);
	rowY = RebaseRowYJson(rowY, group->GetLeft(), group->GetTop(), group->GetWidth(), group->GetHeight());
	group->GetTags()->Add(_bstr_t(L"PP_ROWY"), _bstr_t(Widen(rowY).c_str()));
	g_cacheRowYJson = rowY;
}
} // namespace

HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged) {
	if (outChanged) *outChanged = false;
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();
		PowerPoint::ShapePtr group;
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && Narrow((const wchar_t*)k) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) return S_FALSE;

		std::string docJson = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_DOC")));
		std::string projJson = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		if (docJson.empty() || projJson.empty()) return S_FALSE;

		PpProj proj;
		if (!ParseProj(projJson, &proj)) return S_FALSE;
		long minDay = proj.minDay, pad = proj.pad; float ptPerDay = proj.ptPerDay, originX = proj.originX;

		PpDocument doc = DocumentFromJson(docJson);
		std::map<std::string, std::pair<float, float>> pos;
		std::map<std::string, float> posMilestone;
		PowerPoint::GroupShapesPtr items = group->GetGroupItems();
		long m = items->GetCount();
		for (long i = 1; i <= m; ++i) {
			PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
			_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (!k.length()) continue;
			std::string kindStr = Narrow((const wchar_t*)k);
			_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
			if (kindStr == "TASK") {
				pos[Narrow((const wchar_t*)id)] = { ch->GetLeft(), ch->GetWidth() };
			} else if (kindStr == "MILESTONE") {
				posMilestone[Narrow((const wchar_t*)id)] = ch->GetLeft();
			}
		}

		bool changed = false;
		std::string changedId;

		for (auto& t : doc.tasks) {
			// Under an explicit window, a clipped or hidden bar is not a geometry
			// source of truth. Back-projecting it would rewrite the task to the
			// visible span when Repair layout/FitChartRootToFrame calls this path.
			if (HasExplicitTimeWindow(doc)
				&& (DateToDays(t.start) < DateToDays(doc.windowStart) || DateToDays(t.end) > DateToDays(doc.windowEnd)))
				continue;
			auto it = pos.find(t.id);
			if (it == pos.end()) continue;
			const float left = it->second.first, width = it->second.second;
			long startDay = minDay - pad + (long)::llround((left - originX) / ptPerDay);
			long widthDays = (long)::llround(width / ptPerDay);
			if (widthDays < 1) widthDays = 1;
			std::string ns = DaysToDate(startDay), ne = DaysToDate(startDay + widthDays - 1);
			if (ns != t.start || ne != t.end) {
				t.start = ns;
				t.end = ne;
				changed = true;
				changedId = t.id;
			}
		}

		for (auto& ms : doc.milestones) {
			if (HasExplicitTimeWindow(doc)
				&& (DateToDays(ms.date) < DateToDays(doc.windowStart) || DateToDays(ms.date) > DateToDays(doc.windowEnd)))
				continue;
			auto it = posMilestone.find(ms.id);
			if (it == posMilestone.end()) continue;
			const float left = it->second;
			float sz = 13.0f;
			float targetX = left + sz / 2.0f - ptPerDay / 2.0f;
			long mDay = minDay - pad + (long)::llround((targetX - originX) / ptPerDay);
			std::string nd = DaysToDate(mDay);
			if (nd != ms.date) {
				ms.date = nd;
				changed = true;
				changedId = ms.id;
			}
		}

		if (changed) {
			group->Delete();
			int cnt = 0;
			InsertGantt(pApp, doc, &cnt, changedId);
		}
		if (outChanged) *outChanged = changed;
		return S_OK;
	}
	catch (const _com_error&) { return E_FAIL; }
	catch (const std::exception&) { return E_FAIL; }
	catch (...) { return E_FAIL; }
}

// V3-1 fit-to-slide (see GanttBuilder.h doc comment). Side margin reserved on
// the left/right of the content area, and also used as the bottom margin so
// the fitted chart doesn't touch the slide edge.
static const float kFitSideMargin = 18.0f;
// Fraction of slide height reserved at the top for a native title placeholder.
static const float kFitTitleZoneFrac = 0.15f;

// Exact-fit primitive: resize/reposition the CHART_ROOT group so it occupies
// PRECISELY the given (left, top, width, height) rect, then rewrite PP_PROJ so
// the day<->point projection matches the new geometry. This is a plain
// axis-independent resize (sx = width/currentWidth, sy = height/currentHeight
// — the same semantics as a user dragging the group's resize handles to that
// exact rect), so it is byte-exact/idempotent: calling it twice with the same
// rect reproduces the same rect (mod float rounding), which is what makes it
// safe to use for "restore the chart to its previously-captured frame" (see
// UpdateGantt's PreserveChartRootFrame, GanttBuilder.h doc comment). Uniform-
// scale/no-distortion/letterbox policy is NOT this function's job — it lives
// in FitChartRootToSlide, which computes an already-uniform-scaled target
// rect and passes THAT (not the raw slide content area) here.
HRESULT FitChartRootToFrame(IDispatch* pApp, float left, float top, float width, float height,
	bool defensiveReflow) {
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		PowerPoint::ShapePtr group;
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && Narrow((const wchar_t*)k) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) return S_FALSE;

		std::string projJson = Narrow((const wchar_t*)group->GetTags()->Item(_bstr_t(L"PP_PROJ")));
		PpProj proj;
		bool haveProj = ParseProj(projJson, &proj);

		const float curLeft = group->GetLeft();
		const float curTop = group->GetTop();
		const float curW = group->GetWidth();
		const float curH = group->GetHeight();
		if (curW <= 0.0f || curH <= 0.0f) return E_FAIL;

		const float newLeft = left;
		const float newTop = top;
		const float newW = std::max(1.0f, width);
		const float newH = std::max(1.0f, height);
		const float sx = newW / curW;
		const float sy = newH / curH;

		group->PutWidth(newW);
		group->PutHeight(newH);
		group->PutLeft(newLeft);
		group->PutTop(newTop);

		// Rewrite PP_PROJ so the day<->point projection matches the new
		// geometry (children were proportionally rescaled by PowerPoint's
		// group-resize semantics: for a child originally at (L0,W0) relative
		// to the pre-resize group frame, its new absolute Left/Width are
		// newLeft + (L0-curLeft)*sx and W0*sx — only the X axis matters for
		// PP_PROJ, since the day<->point projection is horizontal-only).
		// Without this, ReflowFromSlide would back-project the now-scaled
		// bar geometry through the STALE pre-resize ptPerDay/originX and
		// derive distorted dates purely from this resize.
		if (haveProj) {
			const float ptPerDayNew = proj.ptPerDay * sx;
			const float originXNew = newLeft + (proj.originX - curLeft) * sx;
			char projBuf[192];
			::sprintf_s(projBuf, "{\"minDay\":%ld,\"pad\":%ld,\"ptPerDay\":%.6f,\"originX\":%.4f}",
				proj.minDay, proj.pad, ptPerDayNew, originXNew);
			group->GetTags()->Add(_bstr_t(L"PP_PROJ"), _bstr_t(Widen(projBuf).c_str()));
			_bstr_t rowYTag = group->GetTags()->Item(_bstr_t(L"PP_ROWY"));
			if (rowYTag.length()) {
				std::string rowYJson = Narrow((const wchar_t*)rowYTag);
				std::string scaled = ScaleRowYJson(rowYJson, sx, sy);
				group->GetTags()->Add(_bstr_t(L"PP_ROWY"), _bstr_t(Widen(scaled).c_str()));
			}
		}

		// Defensive re-sync: with PP_PROJ corrected above, this is expected to
		// read back the same dates (changed=false) and simply return. If it
		// ever does find drift (e.g. rounding at extreme scales), it re-emits
		// from the corrected doc — but note a re-emit rebuilds at NATURAL
		// (unscaled, full-slide-width) size, so this path intentionally does
		// not re-fit; callers that need the fit to survive a drifting reflow
		// should re-invoke FitChartRootToFrame/FitChartRootToSlide afterward.
		if (defensiveReflow) {
			bool changed = false;
			ReflowFromSlide(pApp, &changed);
		}
		return S_OK;
	}
	catch (const _com_error&) { return E_FAIL; }
	catch (const std::exception&) { return E_FAIL; }
	catch (...) { return E_FAIL; }
}

HRESULT FitChartRootToSlide(IDispatch* pApp) {
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_PresentationPtr pres = app->GetActivePresentation();
		const float slideW = (float)pres->GetPageSetup()->GetSlideWidth();
		const float slideH = (float)pres->GetPageSetup()->GetSlideHeight();
		PowerPoint::_SlidePtr slide = app->GetActiveWindow()->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		PowerPoint::ShapePtr group;
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t k = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (k.length() && Narrow((const wchar_t*)k) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) return S_FALSE;

		const float naturalW = group->GetWidth();
		const float naturalH = group->GetHeight();
		if (naturalW <= 0.0f || naturalH <= 0.0f) return E_FAIL;

		// Content area: full width minus side margins, full height below the
		// reserved top title zone (and above a matching bottom margin).
		const float contentLeft = kFitSideMargin;
		const float contentTop = slideH * kFitTitleZoneFrac;
		const float contentW = std::max(1.0f, slideW - kFitSideMargin * 2.0f);
		const float contentH = std::max(1.0f, slideH - contentTop - kFitSideMargin);

		// UNIFORM scale only — a single factor s applied to both axes, never
		// sx != sy, so text glyphs are never stretched/distorted (review
		// finding #2). s fills the content width unless that would overflow
		// the content height, in which case s fills height instead
		// (letterboxing the chart within the content area rather than
		// distorting it). The resulting already-uniform-scaled sub-rect
		// (centered horizontally, top-aligned vertically) is what actually
		// gets passed to FitChartRootToFrame — that function itself has no
		// aspect-ratio opinion, it just resizes to exactly the rect it's given.
		const float sWidthFit = contentW / naturalW;
		const float hAtWidthFit = naturalH * sWidthFit;
		const float s = (hAtWidthFit <= contentH) ? sWidthFit : (contentH / naturalH);

		const float targetW = naturalW * s;
		const float targetH = naturalH * s;
		const float targetLeft = contentLeft + (contentW - targetW) / 2.0f; // center horizontally
		const float targetTop = contentTop; // top-align vertically (letterbox at bottom)

		return FitChartRootToFrame(pApp, targetLeft, targetTop, targetW, targetH);
	}
	catch (const _com_error& e) { return e.Error() ? e.Error() : E_FAIL; }
	catch (const std::exception&) { return E_FAIL; }
	catch (...) { return E_FAIL; }
}

int Gantt_GetLastOpPhasesForTest(char* buf, int len) {
	if (!buf || len < 2) return 0;
	const OpPhaseTimings& p = g_lastOpPhases;
	char tmp[512];
	::sprintf_s(tmp, sizeof(tmp),
		"{\"sceneBuild\":%llu,\"keyCompare\":%llu,\"primWrites\":%llu,\"primWriteCount\":%d,"
		"\"docTagWrite\":%llu,\"framePreserve\":%llu,\"reselect\":%llu,"
		"\"childWalk\":%llu,\"ungroup\":%llu,\"churn\":%llu,\"regroup\":%llu,\"fastPath\":%s,"
		"\"rowYRewritten\":%s,"
		"\"docRead\":%llu,\"docReadCached\":%s,\"dispatchTotal\":%llu}",
		(unsigned long long)p.sceneBuildMs,
		(unsigned long long)p.keyCompareMs,
		(unsigned long long)p.primWritesMs,
		p.primWriteCount,
		(unsigned long long)p.docTagWriteMs,
		(unsigned long long)p.framePreserveMs,
		(unsigned long long)p.reselectMs,
		(unsigned long long)p.childWalkMs,
		(unsigned long long)p.ungroupMs,
		(unsigned long long)p.churnMs,
		(unsigned long long)p.regroupMs,
		p.fastPathHit ? "true" : "false",
		p.rowYRewritten ? "true" : "false",
		(unsigned long long)p.docReadMs,
		p.docReadCached ? "true" : "false",
		(unsigned long long)p.dispatchTotalMs);
	const int n = (int)::strlen(tmp);
	if (n >= len) return 0;
	::memcpy(buf, tmp, (size_t)n + 1);
	return n;
}
