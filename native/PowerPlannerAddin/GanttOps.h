#pragma once

#include "GanttModel.h"
#include <string>

std::string AddRow(PpDocument& doc, const std::string& label, const std::string& afterRowId);
// --- S3 row operations (pure) ---
// Insert a new row with `label` immediately ABOVE / BELOW refRowId at the SAME
// hierarchy level (same groupId as refRowId). AddRowBelow inserts after
// refRowId AND after all of refRowId's child rows (keeps a group contiguous);
// AddRowAbove inserts immediately before refRowId. Returns the new row id, or
// "" if refRowId is not a row.
std::string AddRowAbove(PpDocument& doc, const std::string& refRowId, const std::string& label);
std::string AddRowBelow(PpDocument& doc, const std::string& refRowId, const std::string& label);

// Swap rowId with the adjacent row in the flat doc.rows vector (a plain
// positional swap — no group re-parenting). Returns false if rowId is not found
// or is already at the top / bottom edge.
bool MoveRowUp(PpDocument& doc, const std::string& rowId);
bool MoveRowDown(PpDocument& doc, const std::string& rowId);

// Indent rowId: make it a child of the nearest PRECEDING top-level row.
// Guards (return false, NO change): rowId not found; no preceding row exists;
// rowId currently HAS children (would exceed the 2-level max); the nearest
// preceding top-level row is rowId's own current parent already (no-op).
bool IndentRow(PpDocument& doc, const std::string& rowId);

// Outdent rowId: promote a child row to top-level (groupId = ""). Returns false
// if rowId is not found or is already top-level.
bool OutdentRow(PpDocument& doc, const std::string& rowId);

// Delete a row with the full cascade AND re-parent any of its child rows to
// top-level (their groupId cleared). Returns false if rowId is not a row.
bool DeleteRow(PpDocument& doc, const std::string& rowId);

std::string AddTask(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& startISO, const std::string& endISO);
// Adds a milestone on rowId at dateISO. Generates a unique id ("ms<N>") and
// returns it, mirroring AddTask's id-return convention.
std::string AddMilestone(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& dateISO);
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

// Bottom-axis label numbering: "day" | "cw" (empty accepted and normalized
// to "day"). Returns false for any other value.
bool SetAxisNumbering(PpDocument& doc, const std::string& numbering);

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

// --- S5 dependency operations (pure) ---
// Adds a dependency edge between two EXISTING task or milestone ids. `type`
// is one of finish-to-start | start-to-start | finish-to-finish |
// start-to-finish (empty string is accepted and normalized to
// "finish-to-start"). Rejections (return ""): from == to; either endpoint is
// not an existing task/milestone id; a dep with the same from AND to already
// exists (one edge per pair, regardless of type); any other type string.
// On success generates a unique id ("dep<N>") and returns it, mirroring
// AddMarker/AddText's id-return convention.
std::string AddDependency(PpDocument& doc, const std::string& from, const std::string& to,
	const std::string& type = "finish-to-start");
// Erases every dependency whose from OR to equals id. Returns the number of
// dependencies removed (0 when none touch the id).
int RemoveDependenciesTouching(PpDocument& doc, const std::string& id);
bool RemoveDependencyById(PpDocument& doc, const std::string& depId);

int OpsSelfTest();
