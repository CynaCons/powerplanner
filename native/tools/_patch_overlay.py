import pathlib
p = pathlib.Path(__file__).resolve().parents[1] / "PowerPlannerAddin" / "Overlay.cpp"
text = p.read_text(encoding="utf-8")
old1 = (
    "\t\t\tlong screenY = pt.y + g_windowOriginY;\n"
    "\t\t\tif (const RowBand* band = RowBandAtScreenY(screenY)) {\n"
    "\t\t\t\tg_dragTargetRowId = band->rowId;\n"
    "\t\t\t\tg_dragTargetRowRect = band.screenRect;\n"
    "\t\t\t}\n"
    "\t\t\t// Same \"keep the last valid target\" behavior as TaskBody when no\n"
    "\t\t\t// band is under the pointer (above/below the chart)."
)
new1 = "\t\t\tLatchDragTargetRow(pt.y + g_windowOriginY);"
old2 = (
    "\tif (g_dragKind == DragKind::TaskBody) {\n"
    "\t\tlong screenY = pt.y + g_windowOriginY;\n"
    "\t\tif (const RowBand* band = RowBandAtScreenY(screenY)) {\n"
    "\t\t\tg_dragTargetRowId = band->rowId;\n"
    "\t\t\tg_dragTargetRowRect = band.screenRect;\n"
    "\t\t}\n"
    "\t\t// If no band is under the pointer (above/below the chart), keep the\n"
    "\t\t// last valid target \u2014 the ghost stays at the last row it was over\n"
    "\t\t// rather than snapping away, and the horizontal shift still updates.\n"
    "\t}"
)
new2 = (
    "\tif (g_dragKind == DragKind::TaskBody) {\n"
    "\t\tLatchDragTargetRow(pt.y + g_windowOriginY);\n"
    "\t}"
)
changed = 0
if old1 in text:
    text = text.replace(old1, new1, 1)
    changed += 1
    print("replaced text branch")
else:
    print("text branch not found")
if old2 in text:
    text = text.replace(old2, new2, 1)
    changed += 1
    print("replaced taskbody branch")
else:
    print("taskbody branch not found")
if changed:
    p.write_text(text, encoding="utf-8")
print(f"changed {changed}")
