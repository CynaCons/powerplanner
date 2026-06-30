// N4: on-slide contextual overlay (think-cell style).
//
// A layered, click-through top-level window drawn OVER PowerPoint's slide-edit
// pane. A polling timer finds the CHART_ROOT on the active slide each tick and
// positions the overlay over the whole chart via DocumentWindow::
// PointsToScreenPixelsX/Y (which accounts for zoom + scroll). Selection chrome
// is painted only when a PowerPlanner shape is selected; the overlay hides only
// when no chart root is present.
#pragma once

#include <windows.h>

// Start the overlay: register the window class, create the (hidden) overlay,
// and begin the polling timer. `app` is the PowerPoint.Application (IDispatch).
void OverlayStart(IDispatch* app);

// Stop + tear down (kill timer, destroy window, release the app reference).
void OverlayStop();

// The overlay window handle (for tests/screen capture); NULL if not started.
HWND OverlayHwnd();
