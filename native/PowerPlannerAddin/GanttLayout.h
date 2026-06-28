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
};

// Days since epoch for an ISO "YYYY-MM-DD" date (exposed for the emitter).
long DateToDays(const std::string& iso);

// Inverse of DateToDays: day number -> ISO "YYYY-MM-DD" (for N5 reflow).
std::string DaysToDate(long days);

// Lay out the document. xDay is relative to viewStart; one day = one x-unit,
// one row slot = one y-unit (device scale = 1).
GanttLayoutResult LayoutGantt(const PpDocument& doc, const std::string& viewStart);
