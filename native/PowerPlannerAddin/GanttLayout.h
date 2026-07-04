// Pure, spec-conformant layout (spec/layout.md) in ABSTRACT coordinates:
// time in days (xDay), vertical in row slots. No PowerPoint/COM dependencies, so
// it is unit-testable against spec/fixtures at scale = 1. The emitter maps these
// abstract values to slide points (ptPerDay) as a separate step.
#pragma once

#include "GanttModel.h"
#include <string>
#include <vector>

struct LaidTask {
	std::string id;
	int rowIndex = 0, subRow = 0;
	long xDay = 0, widthDays = 0;
};
struct LaidMilestone {
	std::string id;
	int rowIndex = 0;
	long xDay = 0;
};
struct LaidSummary {
	std::string rowId;
	int rowIndex = 0;
	long xDay = 0, widthDays = 0;
};
struct LaidBracket {
	std::string id;
	long xDay = 0, widthDays = 0;
	int topRow = 0, bottomRow = 0;
};
struct LaidDependency {
	std::string id;
	long fromXDay = 0, toXDay = 0;
};
// Laid-out position of a PpText, in the same abstract units as the rest of
// the layout (xDay / rowIndex / subRow) so the emitter can map it through the
// same xToPt/slotTop projection it already uses for tasks/milestones. The
// (dx, dy) offset (in points, copied straight from the PpText — layout does
// not scale it) is applied by the emitter AFTER projecting anchorXDay/
// rowIndex to points, so it is a device-space nudge on top of the abstract
// anchor position.
//
// anchored: true when this text follows a task/milestone (anchorId was
// non-empty); the anchor's current xDay/widthDays/rowIndex/subRow are carried
// so the emitter can compute the anchor shape's top-right corner even though
// this layout pass does not itself know point-space geometry. When false,
// xDay/rowIndex/subRow describe the (rowId, date) free-placement cell origin
// directly (subRow is always 0 for free text) and widthDays is unused.
struct LaidText {
	std::string id;
	bool anchored = false;
	int rowIndex = 0, subRow = 0;
	long xDay = 0, widthDays = 0;
	float dx = 0, dy = 0;
};
struct GanttLayoutResult {
	std::vector<std::string> visibleRowIds;
	std::vector<int> rowSlots;
	std::vector<int> rowOffsets;
	int chartRows = 0;
	std::vector<LaidTask> tasks;
	std::vector<LaidMilestone> milestones;
	std::vector<LaidSummary> summaries;
	std::vector<LaidBracket> brackets;
	std::vector<LaidDependency> dependencies;
	std::vector<LaidText> texts;
};

// Days since epoch for an ISO "YYYY-MM-DD" date (exposed for the emitter).
long DateToDays(const std::string& iso);

// Inverse of DateToDays: day number -> ISO "YYYY-MM-DD" (for N5 reflow).
std::string DaysToDate(long days);

// Lay out the document. xDay is relative to viewStart; one day = one x-unit,
// one row slot = one y-unit (device scale = 1).
GanttLayoutResult LayoutGantt(const PpDocument& doc, const std::string& viewStart);
