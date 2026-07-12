#pragma once

#include <windows.h>
#include "GanttHitTest.h"
#include <vector>

// Custom theme-coherent popup menu (SR-THEME-01/03). Replaces Win32 TrackPopupMenu.
void ThemeMenu_Init(HINSTANCE inst, ULONG_PTR gdiplusToken, int (*scalePx)(int));

// Show menu at screen position. When block==true, pumps until dismissed and returns
// the chosen HtMenuCmd (0 = cancelled). When block==false, shows the menu and returns
// 0 immediately (harness captures while visible).
int ThemeMenu_Show(const std::vector<HtMenuItem>& items, POINT screenPt, HWND ownerHwnd, bool block = true);

void ThemeMenu_Dismiss(int resultCmd = 0);
bool ThemeMenu_IsVisible();
HWND ThemeMenu_Hwnd();
HWND ThemeMenu_FlyoutHwnd();
