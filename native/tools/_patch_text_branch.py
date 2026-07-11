import pathlib
p = pathlib.Path(__file__).resolve().parents[1] / "PowerPlannerAddin" / "Overlay.cpp"
text = p.read_text(encoding="utf-8")
old = (
    "\t\t\tlong screenY = pt.y + g_windowOriginY;\n"
    "\t\t\tif (const RowBand* band = RowBandAtScreenY(screenY)) {\n"
    "\t\t\t\tg_dragTargetRowId = band->rowId;\n"
    "\t\t\t\tg_dragTargetRowRect = band.screenRect;\n"
    "\t\t\t}\n"
    "\t\t\t// Same \"keep the last valid target\" behavior as TaskBody when no\n"
    "\t\t\t// band is under the pointer (above/below the chart)."
)
new = "\t\t\tLatchDragTargetRow(pt.y + g_windowOriginY);"
if old in text:
    text = text.replace(old, new, 1)
    p.write_text(text, encoding="utf-8")
    print("OK")
else:
    print("NOT FOUND")
    idx = text.find("g_dragDeltaDays = (g_dragPxPerDay")
    print(repr(text[idx:idx+400]))
