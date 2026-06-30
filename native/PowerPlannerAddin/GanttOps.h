#pragma once

#include "GanttModel.h"
#include <string>

std::string AddRow(PpDocument& doc, const std::string& label, const std::string& afterRowId);
std::string AddTask(PpDocument& doc, const std::string& rowId, const std::string& label, const std::string& startISO, const std::string& endISO);
bool DeleteById(PpDocument& doc, const std::string& id);
bool MoveTaskToRow(PpDocument& doc, const std::string& taskId, const std::string& newRowId);
bool SetTaskPercent(PpDocument& doc, const std::string& taskId, int pct);
bool NudgeTask(PpDocument& doc, const std::string& taskId, long deltaDays);
bool SetTitle(PpDocument& doc, const std::string& title);
bool SetScale(PpDocument& doc, const std::string& scale);
int OpsSelfTest();
