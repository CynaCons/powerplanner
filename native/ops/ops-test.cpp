#include "../PowerPlannerAddin/GanttModel.h"
#include "../PowerPlannerAddin/GanttOps.h"

#include <cstdio>

int main() {
	PpDocument doc;
	doc.title = "Ops harness sample";
	doc.rows.push_back(PpRow{"row-1", "Row 1", "", false});
	doc.tasks.push_back(PpTask{"task-1", "Task 1", "2026-01-01", "2026-01-02", "row-1", "#4472c4", 50});

	if (OpsSelfTest() != 0 || doc.tasks.size() != 1) {
		std::printf("OPS HARNESS FAIL\n");
		return 1;
	}

	std::printf("OPS HARNESS OK\n");
	return 0;
}
