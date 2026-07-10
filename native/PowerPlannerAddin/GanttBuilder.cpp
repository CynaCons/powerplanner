#include "pch.h"
#include "GanttBuilder.h"
#include "GanttLayout.h"
#include "GanttJson.h"
#include "Scene.h"
#include "GanttScene.h"   // pure scene pipeline (Widen, layout constants, BuildGanttScene, BuildProjectedScene)
#include "PptRenderer.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

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

// ---- emission --------------------------------------------------------------

// Writes PP_DOC + PP_PROJ + PP_ROWY on the chart root in one place so the
// three tags can never skew. Defined in the anonymous namespace further down
// (next to the PP_ROWY helpers); declared here because InsertGantt/
// ReconcileChartRoot call it first.
namespace {
void WriteChartRootTags(PowerPoint::ShapePtr group, const PpDocument& doc, const std::string& minD,
	long pad, float ptPerDay, float slideW);
}

HRESULT InsertGantt(IDispatch* pApp, const PpDocument& doc, int* outShapeCount, const std::string& selectId) {
	if (!pApp) return E_POINTER;
	int count = 0;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::_PresentationPtr pres = app->GetActivePresentation();
		const float slideW = (float)pres->GetPageSetup()->GetSlideWidth();
		PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		// Date range -> projection (pt/day), padded 5% each side.
		std::string minD, maxD;
		auto consider = [&](const std::string& d) {
			if (d.empty()) return;
			if (minD.empty() || d < minD) minD = d;
			if (maxD.empty() || d > maxD) maxD = d;
		};
		for (const auto& t : doc.tasks) { consider(t.start); consider(t.end); }
		for (const auto& m : doc.milestones) consider(m.date);
		if (minD.empty()) return E_FAIL;

		const long totalDays = std::max(1L, (DateToDays(maxD) - DateToDays(minD)) + 1);
		const long pad = std::max(1L, (long)(totalDays * 0.05));
		const float chartContentW = (slideW - MARGIN * 2.0f) - ROW_GUTTER;
		const float ptPerDay = chartContentW / (float)(totalDays + pad * 2);

		GanttLayoutResult L = LayoutGantt(doc, minD);
		Scene sc = BuildGanttScene(doc, L, minD, maxD, pad, ptPerDay, slideW, MaterialLight());
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
		return;
	}
	ch->PutLeft(prim.x);
	ch->PutTop(prim.y);
	ch->PutWidth(prim.w > 0.0f ? prim.w : 0.01f);
	ch->PutHeight(prim.h > 0.0f ? prim.h : 0.01f);
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

	PowerPoint::GroupShapesPtr items = group->GetGroupItems();
	long childCount = items->GetCount();

	// Snapshot existing children + their match keys (COM: one pass, cheap).
	std::vector<PowerPoint::ShapePtr> children(childCount ? childCount : 0);
	std::vector<std::pair<std::string, std::string>> childKindIds(childCount ? childCount : 0);
	for (long i = 1; i <= childCount; ++i) {
		PowerPoint::ShapePtr ch = items->Item(_variant_t(i));
		children[i - 1] = ch;
		_bstr_t k = ch->GetTags()->Item(_bstr_t(L"PP_KIND"));
		_bstr_t id = ch->GetTags()->Item(_bstr_t(L"PP_ID"));
		childKindIds[i - 1] = { k.length() ? Narrow((const wchar_t*)k) : "", id.length() ? Narrow((const wchar_t*)id) : "" };
	}
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
		for (size_t p = 0; p < sc.prims.size(); ++p) {
			SyncShapeGeometryAndText(children[primMatchChildIdx[p]], sc.prims[p]);
		}
		WriteChartRootTags(group, doc, minD, pad, ptPerDay, slideW);

		if (!selectId.empty()) {
			for (size_t p = 0; p < sc.prims.size(); ++p) {
				if (sc.prims[p].tagId == selectId) { children[primMatchChildIdx[p]]->Select(Office::msoTrue); break; }
			}
		}
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

	try {
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
				SyncShapeGeometryAndText(ch, prim);
				finalOrder.push_back(ch);
			} else {
				Scene one; one.prims.push_back(prim);
				std::vector<PowerPoint::ShapePtr> emitted = RenderScene(shapes, one);
				for (auto& e : emitted) renderedThisAttempt.push_back(e);
				if (!emitted.empty()) finalOrder.push_back(emitted[0]);
			}
		}
		for (auto& ch : untaggedSurvivors) finalOrder.push_back(ch);

		if (finalOrder.size() < 2) { cleanupLoose(); return E_FAIL; } // nothing sane to regroup

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

		if (!selectId.empty()) {
			for (size_t p = 0; p < sc.prims.size(); ++p) {
				if (sc.prims[p].tagId == selectId) { finalOrder[p]->Select(Office::msoTrue); break; }
			}
		}
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

		const float kTol = 0.25f;
		bool driftedFrame = (std::fabs(group->GetLeft() - capLeft) > kTol)
			|| (std::fabs(group->GetTop() - capTop) > kTol)
			|| (std::fabs(group->GetWidth() - capWidth) > kTol)
			|| (std::fabs(group->GetHeight() - capHeight) > kTol);
		if (!driftedFrame) return;

		FitChartRootToFrame(pApp, capLeft, capTop, capWidth, capHeight);
	} catch (const _com_error&) {
	} catch (...) {}
}

HRESULT UpdateGantt(IDispatch* pApp, const PpDocument& doc, const std::string& selectId) {
	if (!pApp) return E_POINTER;
	try {
		PowerPoint::_ApplicationPtr app(pApp);
		PowerPoint::DocumentWindowPtr win = app->GetActiveWindow();
		PowerPoint::_SlidePtr slide = win->GetView()->GetSlide();
		PowerPoint::ShapesPtr shapes = slide->GetShapes();

		PowerPoint::ShapePtr group;
		long n = shapes->GetCount();
		for (long i = 1; i <= n; ++i) {
			PowerPoint::ShapePtr sh = shapes->Item(_variant_t(i));
			_bstr_t kind = sh->GetTags()->Item(_bstr_t(L"PP_KIND"));
			if (kind.length() && Narrow((const wchar_t*)kind) == "CHART_ROOT") { group = sh; break; }
		}
		if (!group) {
			// No existing chart to reconcile against: full emit.
			int cnt = 0;
			return InsertGantt(pApp, doc, &cnt, selectId);
		}

		// Capture the CHART_ROOT's frame BEFORE reconciling — this is whatever
		// frame the chart currently has (fitted, user-resized, or natural), and
		// is what must survive the rebuild below (review finding #1).
		const float capLeft = group->GetLeft();
		const float capTop = group->GetTop();
		const float capWidth = group->GetWidth();
		const float capHeight = group->GetHeight();

		HRESULT hr = ReconcileChartRoot(app, slide, group, doc, selectId);
		if (SUCCEEDED(hr)) {
			PreserveChartRootFrame(pApp, slide, capLeft, capTop, capWidth, capHeight);
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
		int cnt = 0;
		return InsertGantt(pApp, doc, &cnt, selectId);
	}
	catch (const _com_error& e) {
		// Best-effort fallback even on an unexpected COM error during the
		// reconcile attempt itself.
		wchar_t buf[96];
		::swprintf_s(buf, 96, L"UpdateGantt: COM error 0x%08lX during reconcile, falling back to InsertGantt", (unsigned long)e.Error());
		GbLog(buf);
		try {
			int cnt = 0;
			return InsertGantt(pApp, doc, &cnt, selectId);
		} catch (...) {}
		return e.Error() ? e.Error() : E_FAIL;
	}
	catch (const std::exception&) {
		GbLog(L"UpdateGantt: std::exception during reconcile, falling back to InsertGantt");
		try {
			int cnt = 0;
			return InsertGantt(pApp, doc, &cnt, selectId);
		} catch (...) {}
		return E_FAIL;
	}
	catch (...) {
		GbLog(L"UpdateGantt: unknown exception during reconcile, falling back to InsertGantt");
		try {
			int cnt = 0;
			return InsertGantt(pApp, doc, &cnt, selectId);
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
HRESULT FitChartRootToFrame(IDispatch* pApp, float left, float top, float width, float height) {
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
		bool changed = false;
		ReflowFromSlide(pApp, &changed);
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
