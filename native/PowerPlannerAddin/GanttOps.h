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
int OpsSelfTest();
