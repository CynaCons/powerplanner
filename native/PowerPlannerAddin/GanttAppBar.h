#pragma once

#include "GanttModel.h"
#include "GanttHitTest.h"

#include <string>
#include <vector>

enum class AppBarSel { None, Task, Row, Milestone, Marker, Note };

enum class AppBarIcon {
	None, Row, Task, Milestone, Marker, Note, Edit, Swatch,
	MinusDay, PlusDay, Label, Link, Unlink, Delete, Rename,
	RowAbove, RowBelow, MoveUp, MoveDown, Indent, Outdent,
	LabelsToggle, Grid, Reanchor, ScaleSeg
};

struct AppBarItem {
	int cmd = HtCmd_None;
	std::string label;         // display text; "" for icon-only buttons
	AppBarIcon icon = AppBarIcon::None;
	bool enabled = true;
	bool danger = false;
	bool active = false;       // current item in a segmented/swatch set
	std::string data;          // swatch hex "#RRGGBB" for swatch items; else ""
};

struct AppBarGroup {
	std::string label;                 // UPPERCASE section label; "" = none
	std::vector<AppBarItem> items;
};

struct AppBarModel {
	std::string name;                  // italic selection name; "" when None
	std::vector<AppBarGroup> groups;   // left->right; LAST group is always global
};

inline const char* const kAppBarSwatches[8] = {
	"#4355E0", "#0E8D8A", "#7A4FA3", "#5B6C8F",
	"#B3552F", "#8B8E24", "#B23A6B", "#2E7D6E"
};

inline bool AppBarColorEquals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		char ca = a[i];
		char cb = b[i];
		if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
		if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
		if (ca != cb) return false;
	}
	return true;
}

inline bool AppBarRowRendersRail(const PpDocument& doc, const std::string& rowId) {
	if (doc.railLabels) return true;
	for (const auto& task : doc.tasks) {
		if (task.rowId == rowId &&
			(task.labelPlacement == "rail" || task.labelPlacement == "both")) {
			return true;
		}
	}
	return false;
}

inline bool AppBarAnyDepTouches(const PpDocument& doc, const std::string& id) {
	for (const auto& dep : doc.deps) {
		if (dep.from == id || dep.to == id) return true;
	}
	return false;
}

inline void AppBarAppendGlobalGroup(AppBarModel& model, const PpDocument& doc) {
	AppBarGroup g;
	g.label = "SCALE";
	const char* scales[] = { "day", "week", "month", "quarter", "year" };
	const char* labels[] = { "D", "W", "M", "Q", "Y" };
	const int cmds[] = {
		HtCmd_ScaleDay, HtCmd_ScaleWeek, HtCmd_ScaleMonth,
		HtCmd_ScaleQuarter, HtCmd_ScaleYear
	};
	for (int i = 0; i < 5; ++i) {
		AppBarItem item;
		item.cmd = cmds[i];
		item.label = labels[i];
		item.icon = AppBarIcon::ScaleSeg;
		item.active = (doc.scale == scales[i]);
		g.items.push_back(item);
	}
	AppBarItem labelsItem;
	labelsItem.cmd = HtCmd_ToggleRailLabels;
	labelsItem.label = "Labels";
	labelsItem.icon = AppBarIcon::LabelsToggle;
	labelsItem.active = doc.railLabels;
	g.items.push_back(labelsItem);
	AppBarItem gridItem;
	gridItem.cmd = HtCmd_CycleGrid;
	gridItem.label = "Grid";
	gridItem.icon = AppBarIcon::Grid;
	g.items.push_back(gridItem);
	model.groups.push_back(g);
}

inline void AppBarAppendInsertGroup(AppBarModel& model) {
	AppBarGroup g;
	g.label = "INSERT";
	g.items.push_back({ HtCmd_AddRow, "Row", AppBarIcon::Row });
	g.items.push_back({ HtCmd_InsertTask, "Task", AppBarIcon::Task });
	g.items.push_back({ HtCmd_InsertMilestone, "Milestone", AppBarIcon::Milestone });
	g.items.push_back({ HtCmd_InsertMarker, "Marker", AppBarIcon::Marker });
	g.items.push_back({ HtCmd_InsertNote, "Note", AppBarIcon::Note });
	model.groups.push_back(g);
}

inline void AppBarAppendRowOpsGroup(AppBarModel& model) {
	AppBarGroup g;
	g.label = "ROW";
	g.items.push_back({ HtCmd_AddRowAbove, "Above", AppBarIcon::RowAbove });
	g.items.push_back({ HtCmd_AddRowBelow, "Below", AppBarIcon::RowBelow });
	g.items.push_back({ HtCmd_MoveRowUp, "", AppBarIcon::MoveUp });
	g.items.push_back({ HtCmd_MoveRowDown, "", AppBarIcon::MoveDown });
	g.items.push_back({ HtCmd_IndentRow, "Indent", AppBarIcon::Indent });
	g.items.push_back({ HtCmd_OutdentRow, "Outdent", AppBarIcon::Outdent });
	AppBarItem del;
	del.cmd = HtCmd_DeleteRow;
	del.label = "Delete";
	del.icon = AppBarIcon::Delete;
	del.danger = true;
	g.items.push_back(del);
	model.groups.push_back(g);
}

inline std::string AppBarLabelPlacementWord(const std::string& labelPlacement) {
	if (labelPlacement == "rail") return "rail";
	if (labelPlacement == "both") return "both";
	return "bar";
}

inline AppBarModel BuildAppBar(AppBarSel sel, const PpDocument& doc, const std::string& selId) {
	AppBarModel model;

	if (sel == AppBarSel::None) {
		AppBarAppendInsertGroup(model);
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	if (sel == AppBarSel::Task) {
		const PpTask* task = nullptr;
		for (const auto& t : doc.tasks) {
			if (t.id == selId) { task = &t; break; }
		}
		if (!task) {
			AppBarAppendInsertGroup(model);
			// global only when truly None overall
			return model;
		}
		model.name = task->label;
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Edit, "Edit", AppBarIcon::Edit });
			g.items.push_back({ HtCmd_Rename, "Rename", AppBarIcon::Rename });
			model.groups.push_back(g);
		}
		{
			AppBarGroup g;
			const std::string effectiveColor = task->color.empty() ? "#4355E0" : task->color;
			const int swatchCmds[] = {
				HtCmd_Swatch1, HtCmd_Swatch2, HtCmd_Swatch3, HtCmd_Swatch4,
				HtCmd_Swatch5, HtCmd_Swatch6, HtCmd_Swatch7, HtCmd_Swatch8
			};
			for (int i = 0; i < 8; ++i) {
				AppBarItem item;
				item.cmd = swatchCmds[i];
				item.icon = AppBarIcon::Swatch;
				item.data = kAppBarSwatches[i];
				item.active = AppBarColorEquals(kAppBarSwatches[i], effectiveColor);
				g.items.push_back(item);
			}
			model.groups.push_back(g);
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_NudgeMinus1, "-1d", AppBarIcon::MinusDay });
			g.items.push_back({ HtCmd_NudgePlus1, "+1d", AppBarIcon::PlusDay });
			AppBarItem labelItem;
			labelItem.cmd = HtCmd_CycleLabelPlacement;
			labelItem.label = "Label: " + AppBarLabelPlacementWord(task->labelPlacement);
			labelItem.icon = AppBarIcon::Label;
			g.items.push_back(labelItem);
			model.groups.push_back(g);
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Link, "Link", AppBarIcon::Link });
			AppBarItem unlink;
			unlink.cmd = HtCmd_Unlink;
			unlink.label = "Unlink";
			unlink.icon = AppBarIcon::Unlink;
			unlink.enabled = AppBarAnyDepTouches(doc, selId);
			g.items.push_back(unlink);
			g.items.push_back({ HtCmd_AddNote, "Note", AppBarIcon::Note });
			AppBarItem del;
			del.cmd = HtCmd_Delete;
			del.label = "Delete";
			del.icon = AppBarIcon::Delete;
			del.danger = true;
			g.items.push_back(del);
			model.groups.push_back(g);
		}
		if (AppBarRowRendersRail(doc, task->rowId)) {
			AppBarAppendRowOpsGroup(model);
		}
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	if (sel == AppBarSel::Row) {
		const PpRow* row = nullptr;
		for (const auto& r : doc.rows) {
			if (r.id == selId) { row = &r; break; }
		}
		if (row) model.name = row->label;
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Rename, "Rename", AppBarIcon::Rename });
			model.groups.push_back(g);
		}
		AppBarAppendRowOpsGroup(model);
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	if (sel == AppBarSel::Milestone) {
		for (const auto& ms : doc.milestones) {
			if (ms.id == selId) { model.name = ms.label; break; }
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Edit, "Edit", AppBarIcon::Edit });
			model.groups.push_back(g);
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_NudgeMinus1, "-1d", AppBarIcon::MinusDay });
			g.items.push_back({ HtCmd_NudgePlus1, "+1d", AppBarIcon::PlusDay });
			g.items.push_back({ HtCmd_AddNote, "Note", AppBarIcon::Note });
			AppBarItem del;
			del.cmd = HtCmd_Delete;
			del.label = "Delete";
			del.icon = AppBarIcon::Delete;
			del.danger = true;
			g.items.push_back(del);
			model.groups.push_back(g);
		}
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	if (sel == AppBarSel::Marker) {
		for (const auto& mk : doc.markers) {
			if (mk.id == selId) { model.name = mk.label; break; }
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Rename, "Rename", AppBarIcon::Rename });
			model.groups.push_back(g);
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_NudgeMinus1, "-1d", AppBarIcon::MinusDay });
			g.items.push_back({ HtCmd_NudgePlus1, "+1d", AppBarIcon::PlusDay });
			AppBarItem del;
			del.cmd = HtCmd_Delete;
			del.label = "Delete";
			del.icon = AppBarIcon::Delete;
			del.danger = true;
			g.items.push_back(del);
			model.groups.push_back(g);
		}
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	if (sel == AppBarSel::Note) {
		for (const auto& txt : doc.texts) {
			if (txt.id == selId) {
				model.name = txt.label.empty() ? "Note" : txt.label;
				break;
			}
		}
		{
			AppBarGroup g;
			g.items.push_back({ HtCmd_Edit, "Edit", AppBarIcon::Edit });
			g.items.push_back({ HtCmd_ReanchorNote, "Re-anchor", AppBarIcon::Reanchor });
			AppBarItem del;
			del.cmd = HtCmd_Delete;
			del.label = "Delete";
			del.icon = AppBarIcon::Delete;
			del.danger = true;
			g.items.push_back(del);
			model.groups.push_back(g);
		}
		AppBarAppendGlobalGroup(model, doc);
		return model;
	}

	// None / fallback: overall component — includes global scale controls
	AppBarAppendInsertGroup(model);
	AppBarAppendGlobalGroup(model, doc);
	return model;
}
