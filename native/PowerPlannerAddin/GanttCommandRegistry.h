#pragma once

// Shared command registry helpers: context menus derive from the same
// BuildAppBar model as the on-slide app bar (onslide-v4 S6 / spec B8.1–B8.3).
#include "GanttAppBar.h"
#include "GanttHitTest.h"

#include <string>
#include <vector>

inline AppBarSel HtZoneToAppBarSel(HtZone zone, HtItemKind kind, bool hasRowId) {
	switch (zone) {
	case HtZone::TaskBody:
	case HtZone::TaskEdgeL:
	case HtZone::TaskEdgeR:
		return AppBarSel::Task;
	case HtZone::Milestone:
		return AppBarSel::Milestone;
	case HtZone::Marker:
		return AppBarSel::Marker;
	case HtZone::Text:
		return AppBarSel::Note;
	case HtZone::RowBand:
		if (!hasRowId) return AppBarSel::None;
		return AppBarSel::Row;
	case HtZone::Label:
		if (kind == HtItemKind::RowLabel && hasRowId) return AppBarSel::Row;
		return AppBarSel::None;
	default:
		return AppBarSel::None;
	}
}

inline std::string MenuLabelForAppBarItem(const AppBarItem& item) {
	switch (item.cmd) {
	case HtCmd_NudgeMinus1:
		return "Nudge -1 day";
	case HtCmd_NudgePlus1:
		return "Nudge +1 day";
	default:
		break;
	}
	if (!item.label.empty()) return item.label;
	switch (item.icon) {
	case AppBarIcon::MoveUp:
		return "Move Up";
	case AppBarIcon::MoveDown:
		return "Move Down";
	case AppBarIcon::Swatch:
		return item.data.empty() ? "Color" : item.data;
	default:
		return "";
	}
}

inline void RegistryAppendScaleSubmenu(std::vector<HtMenuItem>& items, bool separatorBeforeHeader) {
	items.push_back({ HtCmd_None, "Change Scale", separatorBeforeHeader, "", true });
	items.push_back({ HtCmd_ScaleDay, "Day", false, "Change Scale", true });
	items.push_back({ HtCmd_ScaleWeek, "Week", false, "Change Scale", true });
	items.push_back({ HtCmd_ScaleMonth, "Month", false, "Change Scale", true });
	items.push_back({ HtCmd_ScaleQuarter, "Quarter", false, "Change Scale", true });
	items.push_back({ HtCmd_ScaleYear, "Year", false, "Change Scale", true });
}

inline void RegistryAppendGridSubmenu(std::vector<HtMenuItem>& items, bool separatorBeforeHeader) {
	items.push_back({ HtCmd_None, "Grid", separatorBeforeHeader, "", true });
	items.push_back({ HtCmd_GridAuto, "Auto", false, "Grid", true });
	items.push_back({ HtCmd_GridWeek, "Week", false, "Grid", true });
	items.push_back({ HtCmd_GridMonth, "Month", false, "Grid", true });
	items.push_back({ HtCmd_GridNone, "None", false, "Grid", true });
}

inline void RegistryAppendInsertSubmenu(std::vector<HtMenuItem>& items, const AppBarGroup& group, bool separatorBeforeHeader) {
	items.push_back({ HtCmd_None, "Insert", separatorBeforeHeader, "", true });
	for (const auto& item : group.items) {
		items.push_back({ item.cmd, MenuLabelForAppBarItem(item), false, "Insert", item.enabled });
	}
}

inline std::vector<HtMenuItem> AppBarModelToMenuItems(const AppBarModel& model) {
	std::vector<HtMenuItem> items;
	bool firstGroup = true;

	for (const auto& group : model.groups) {
		if (group.label == "INSERT") {
			RegistryAppendInsertSubmenu(items, group, !firstGroup);
			firstGroup = false;
			continue;
		}

		if (group.label == "SCALE") {
			RegistryAppendScaleSubmenu(items, !firstGroup);
			for (const auto& item : group.items) {
				if (item.cmd == HtCmd_ToggleRailLabels) {
					items.push_back({ item.cmd, "Labels", true, "", item.enabled });
				} else if (item.cmd == HtCmd_CycleGrid) {
					RegistryAppendGridSubmenu(items, false);
				}
			}
			firstGroup = false;
			continue;
		}

		bool firstInGroup = true;
		std::vector<AppBarItem> swatches;
		for (const auto& item : group.items) {
			if (item.icon == AppBarIcon::Swatch) {
				swatches.push_back(item);
				continue;
			}
			HtMenuItem menuItem;
			menuItem.cmdId = item.cmd;
			menuItem.label = MenuLabelForAppBarItem(item);
			menuItem.separatorBefore = !firstGroup && firstInGroup;
			menuItem.enabled = item.enabled;
			items.push_back(menuItem);
			firstInGroup = false;
		}

		if (!swatches.empty()) {
			items.push_back({ HtCmd_None, "Colors", !firstGroup, "", true });
			for (const auto& item : swatches) {
				items.push_back({ item.cmd, MenuLabelForAppBarItem(item), false, "Colors", item.enabled });
			}
		}

		firstGroup = false;
	}

	return items;
}

inline std::vector<HtMenuItem> BuildEmptyCellMenuItems() {
	return {
		{ HtCmd_EmptyCellAddTaskHere, "Add task here", false, "", true },
		{ HtCmd_EmptyCellAddMilestoneHere, "Add milestone here", false, "", true },
		{ HtCmd_EmptyCellAddNoteHere, "Add note here", false, "", true },
	};
}
