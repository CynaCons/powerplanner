#pragma once

#include "GanttModel.h"
#include <string>

std::string AddRow(PpDocument& doc, const std::string& label, const std::string& afterRowId);
std::string AddTask(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& startISO, const std::string& endISO);
bool DeleteById(PpDocument& doc, const std::string& id);
bool MoveTaskToRow(PpDocument& doc, const std::string& taskId, const std::string& newRowId);
bool SetTaskPercent(PpDocument& doc, const std::string& taskId, int pct);
// Absolute percent-complete setter (0-100, clamped) — same clamping behavior
// as SetTaskPercent (which is ALSO already absolute, not a delta; this name
// exists so callers that want an explicit "set to value" op, like the
// floating card editor, don't have to reason about which of the two
// percent ops is delta-based). Kept as a thin wrapper so there is exactly
// one clamping implementation.
bool SetTaskPercentValue(PpDocument& doc, const std::string& taskId, int pct);
bool NudgeTask(PpDocument& doc, const std::string& taskId, long deltaDays);
bool SetTaskDates(PpDocument& doc, const std::string& taskId, const std::string& startISO, const std::string& endISO);
bool SetTitle(PpDocument& doc, const std::string& title);
bool SetEntityLabel(PpDocument& doc, const std::string& id, const std::string& label);
bool SetScale(PpDocument& doc, const std::string& scale);
// Sets a task's bar color (hex string, e.g. "#4472c4"); empty string clears
// back to the renderer's default theme color. Returns false if taskId is not
// a task (mirrors SetTaskDates/SetTaskPercent's not-found semantics).
bool SetTaskColor(PpDocument& doc, const std::string& taskId, const std::string& color);

// Sets a task's label placement: "bar" | "rail" | "both" (empty string is
// accepted and normalized to "bar"). Returns false if `placement` is not one of
// those values, or if taskId is not a task.
bool SetLabelPlacement(PpDocument& doc, const std::string& taskId, const std::string& placement);

// Global all-rail override: when on, every task label renders in the left rail
// regardless of each task's own labelPlacement. Always returns true.
bool SetRailLabelsGlobal(PpDocument& doc, bool on);

// Axis separator-tick density: "auto" | year | quarter | month | week | day |
// none (empty string is accepted and normalized to "auto"). Returns false for
// any other value.
bool SetGridDensity(PpDocument& doc, const std::string& density);

// Bottom-tier tick style: "solid" | "dotted" (empty accepted, normalized to
// "solid"). Returns false for any other value.
bool SetGridStyle(PpDocument& doc, const std::string& style);

// Adds a marker (vertical date line + label chip), e.g. "today", "deadline",
// or "custom". Generates a unique id ("mk<N>") and returns it, mirroring
// AddRow/AddTask's id-return convention.
std::string AddMarker(PpDocument& doc, const std::string& type, const std::string& label, const std::string& dateISO);
// Updates a marker's date. Returns false if id is not a marker.
bool SetMarkerDate(PpDocument& doc, const std::string& id, const std::string& dateISO);
// Updates a marker's label. Returns false if id is not a marker.
bool SetMarkerLabel(PpDocument& doc, const std::string& id, const std::string& label);

// Adds a text annotation. If anchorId is non-empty, the text is anchored to
// that task/milestone id (rowId/date are ignored — anchored position is
// derived from the anchor at layout time); if anchorId is empty, the text is
// free-floating at the (rowId, date) cell origin. Generates a unique id
// ("txt<N>") and returns it, mirroring AddRow/AddTask/AddMarker's
// id-return convention.
std::string AddText(PpDocument& doc, const std::string& label, const std::string& anchorId,
	const std::string& rowId, const std::string& dateISO);
// Updates a text's label. Returns false if id is not a text.
bool SetTextLabel(PpDocument& doc, const std::string& id, const std::string& label);
// Moves a text by adjusting its (dx, dy) point offset; for a free (non-
// anchored) text, rowId/dateISO (if non-empty) also re-home the text to a new
// cell. Returns false if id is not a text.
bool MoveText(PpDocument& doc, const std::string& id, float dx, float dy,
	const std::string& rowId = "", const std::string& dateISO = "");

int OpsSelfTest();
