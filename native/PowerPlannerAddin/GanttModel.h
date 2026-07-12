// The Gantt data model (mirrors spec/data-model.md). UTF-8 std::string fields so
// the model + layout are free of COM/PowerPoint types and JSON-friendly; the
// emitter converts to wide strings at the PowerPoint boundary.
#pragma once

#include <string>
#include <vector>

struct PpTask {
	std::string id, label, start, end, rowId, color;
	int percent = 0;
	// On-bar vs rail label placement: "" (= "bar", default) | "bar" | "rail" |
	// "both". "rail" moves the label off the bar into the left rail (dot +
	// label at the task's lane); "both" shows it in both places. A document-wide
	// PpDocument.railLabels override forces all tasks to rail regardless.
	std::string labelPlacement;
};
struct PpMilestone {
	std::string id, label, date, rowId, color;
};
struct PpDependency {
	std::string id, from, to, type;  // type: finish-to-start | start-to-start | finish-to-finish | start-to-finish
};
struct PpMarker {
	std::string id, type, label, date, color;  // type: deadline | today
};
struct PpBracket {
	std::string id, label, start, end, color;
	std::vector<std::string> rowIds;
};
struct PpRow {
	std::string id, label, groupId;  // groupId empty => top-level
	bool collapsed = false;
};
// A free-floating (or anchored) text annotation. When anchorId is non-empty,
// the text is anchored to that task/milestone id: its position is derived
// from the anchor shape's current layout (top-right corner) plus (dx, dy) in
// points, so it automatically follows the anchor across a rebuild (e.g. the
// anchor's dates shifting) without needing its own stored position. When
// anchorId is empty, the text is free: its position is the (rowId, date)
// cell origin plus (dx, dy) in points.
struct PpText {
	std::string id, label, anchorId, rowId, date, color;
	float dx = 0, dy = 0;
};
struct PpDocument {
	std::string title;
	std::string scale = "week";
	bool railLabels = false;  // global override: render ALL task labels in the rail
	// Axis separator-tick tier override (labels unchanged): "" = auto (follows
	// scale) | year | quarter | month | week | day | none. "none" keeps band
	// labels but draws no plot ticks.
	std::string gridDensity;
	// Bottom axis label numbering: "day" (the backward-compatible default) |
	// "cw" (ISO calendar-week numbers). The canonical JSON omits "day".
	std::string axisNumbering = "day";
	// Explicit visible time window. Empty fields preserve the legacy auto-fit
	// projection. Canonical JSON omits either empty field.
	std::string windowStart, windowEnd;
	// Bottom-tier tick style: "" = solid | solid | dotted.
	std::string gridStyle;
	std::vector<PpRow> rows;
	std::vector<PpTask> tasks;
	std::vector<PpMilestone> milestones;
	std::vector<PpBracket> brackets;
	std::vector<PpDependency> deps;
	std::vector<PpMarker> markers;
	std::vector<PpText> texts;
};
