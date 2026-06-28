// N2: native shape emission. Maps the spec-conformant abstract layout
// (GanttLayout) to native PowerPoint shapes on the active slide.
#pragma once

#include "pch.h"
#include "GanttModel.h"

// A built-in sample chart (N3 replaces this with a parsed/round-tripped document).
PpDocument MakeSampleDocument();

// Emit the document as native shapes on the active slide, grouped under a tagged
// root carrying the serialized document (PP_DOC). pApp is the cached
// PowerPoint.Application (IDispatch). On success returns S_OK and sets *outShapeCount.
HRESULT InsertGantt(IDispatch* pApp, const PpDocument& doc, int* outShapeCount);

// Read the embedded document JSON back from the active slide's chart group
// (PP_DOC on the CHART_ROOT). Returns "" if no PowerPlanner chart is present.
std::string ReadGanttFromSlide(IDispatch* pApp);

// N5: read each task shape's position back into dates (inverse projection via
// the PP_PROJ tag), update the document, and reflow the chart (re-emit) so
// dependent connectors/summary and PP_DOC stay in sync. Sets *outChanged.
HRESULT ReflowFromSlide(IDispatch* pApp, bool* outChanged);
