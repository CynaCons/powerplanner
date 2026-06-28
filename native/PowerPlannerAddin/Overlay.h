// N4: on-slide contextual overlay (think-cell style).
//
// A layered, click-through top-level window drawn OVER PowerPoint's slide-edit
// pane. A polling timer reads the current selection each tick; when a
// PowerPlanner shape (one carrying a PP_KIND tag) is selected, the overlay is
// positioned via DocumentWindow::PointsToScreenPixelsX/Y (which accounts for
// zoom + scroll) and paints a selection frame, handles, and a badge. It is
// inert/hidden when nothing relevant is selected.
#pragma once

#include <windows.h>

// Start the overlay: register the window class, create the (hidden) overlay,
// and begin the polling timer. `app` is the PowerPoint.Application (IDispatch).
void OverlayStart(IDispatch* app);

// Stop + tear down (kill timer, destroy window, release the app reference).
void OverlayStop();

// The overlay window handle (for tests/screen capture); NULL if not started.
HWND OverlayHwnd();
