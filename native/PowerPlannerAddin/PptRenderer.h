// Backend that emits a Scene as native PowerPoint shapes.
#pragma once

#include "pch.h"
#include "Scene.h"
#include <vector>

// Render every primitive in `sc` onto `shapes`, applying style + tags.
// Returns the emitted shapes (in scene order) so the caller can group them.
std::vector<PowerPoint::ShapePtr> RenderScene(PowerPoint::ShapesPtr shapes, const Scene& sc);
