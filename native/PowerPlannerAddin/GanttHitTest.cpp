// Pure, COM-free semantic hit-testing for the on-slide Gantt overlay.
// See GanttHitTest.h for the contract. No Windows headers here on purpose —
// this file is compiled into the PowerPoint-free ops harness.
#include "GanttHitTest.h"

namespace {

bool InRect(const HtRect& r, long x, long y) {
	return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

long AbsLong(long v) {
	return v < 0 ? -v : v;
}

} // namespace

int HtScalePx(int basePx, int dpi) {
	if (dpi <= 0) dpi = kHtBaseDpi;
	// Round-half-away-from-zero, matching MulDiv(basePx, dpi, 96)'s rounding
	// for the positive values this is ever called with.
	long long num = (long long)basePx * (long long)dpi;
	long long den = (long long)kHtBaseDpi;
	if (num >= 0) return (int)((num + den / 2) / den);
	return (int)((num - den / 2) / den);
}

HtHit GanttHitTestPoint(const HtSnapshot& snap, long x, long y) {
	HtHit hit;
	if (!InRect(snap.chartRect, x, y)) {
		hit.zone = HtZone::Outside;
		return hit;
	}

	// 1) Task resize edges win over everything else (thin bars stay resizable
	//    even when a neighbour's body or a milestone overlaps the band). Across
	//    tasks, the nearest edge wins. The edge band's half-width is DPI-scaled
	//    (snap.edgeBandPx), defaulting to kHtEdgePx (4px @ 96 DPI) so existing
	//    callers that never set it keep the original behavior.
	long edgeBandPx = snap.edgeBandPx;
	const HtItem* edgeItem = nullptr;
	bool edgeIsLeft = false;
	long edgeDist = edgeBandPx + 1;
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Task) continue;
		if (y < it.rect.top || y >= it.rect.bottom) continue;
		long dl = AbsLong(x - it.rect.left);
		long dr = AbsLong(x - it.rect.right);
		if (dl <= edgeBandPx && dl < edgeDist) { edgeItem = &it; edgeIsLeft = true; edgeDist = dl; }
		if (dr <= edgeBandPx && dr < edgeDist) { edgeItem = &it; edgeIsLeft = false; edgeDist = dr; }
	}
	if (edgeItem) {
		hit.zone = edgeIsLeft ? HtZone::TaskEdgeL : HtZone::TaskEdgeR;
		hit.kind = HtItemKind::Task;
		hit.id = edgeItem->id;
		return hit;
	}

	// 2) Milestones (small markers drawn over the band).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Milestone) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Milestone;
			hit.kind = HtItemKind::Milestone;
			hit.id = it.id;
			return hit;
		}
	}

	// 3) Task bodies.
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::Task) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::TaskBody;
			hit.kind = HtItemKind::Task;
			hit.id = it.id;
			return hit;
		}
	}

	// 4) Labels (row labels + title).
	for (const auto& it : snap.items) {
		if (it.kind != HtItemKind::RowLabel && it.kind != HtItemKind::Title) continue;
		if (InRect(it.rect, x, y)) {
			hit.zone = HtZone::Label;
			hit.kind = it.kind;
			hit.id = it.id;
			return hit;
		}
	}

	// 5) Row bands: right of the row's label column is an empty timeline cell;
	//    left of / around the label column is the generic row band.
	for (const auto& band : snap.rowBands) {
		if (y < band.yTop || y >= band.yBottom) continue;
		long labelRight = snap.chartRect.left;
		for (const auto& it : snap.items) {
			if (it.kind == HtItemKind::RowLabel && it.id == band.rowId) {
				labelRight = it.rect.right;
				break;
			}
		}
		hit.zone = (x >= labelRight) ? HtZone::EmptyCell : HtZone::RowBand;
		hit.id = band.rowId;
		hit.rowId = band.rowId;
		return hit;
	}

	// Inside the chart but in no band (e.g. the strip around the title):
	// chart background — reported as a RowBand with an empty rowId.
	hit.zone = HtZone::RowBand;
	return hit;
}
