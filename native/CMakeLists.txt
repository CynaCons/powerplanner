# Optional non-authoritative CMake for IDE / incremental builds (Phase 13 v2.8.5).
# The batch scripts under native/*.bat remain the authoritative build for release
# and harnesses. This file exists so the tree can be opened in CLion/VS CMake.
cmake_minimum_required(VERSION 3.20)
project(PowerPlannerNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Paths match the bat layout; link/register steps still use build.bat for COM.
set(ADDIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/PowerPlannerAddin)
set(BUILD_OUT ${CMAKE_CURRENT_SOURCE_DIR}/build)

add_library(PowerPlannerAddin_sources INTERFACE)
target_include_directories(PowerPlannerAddin_sources INTERFACE
  ${ADDIN_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

file(GLOB ADDIN_CPP
  ${ADDIN_DIR}/Connect.cpp
  ${ADDIN_DIR}/dllmain.cpp
  ${ADDIN_DIR}/GanttLayout.cpp
  ${ADDIN_DIR}/GanttJson.cpp
  ${ADDIN_DIR}/GanttOps.cpp
  ${ADDIN_DIR}/GanttHitTest.cpp
  ${ADDIN_DIR}/PptRenderer.cpp
  ${ADDIN_DIR}/GanttBuilder.cpp
  ${ADDIN_DIR}/Overlay.cpp
  ${ADDIN_DIR}/ThemeMenu.cpp
)

# Note: full COM DLL build requires ATL/Office type libraries resolved as in
# build.bat / sources.bat. Prefer `native\\build.bat` for a working binary.
message(STATUS "PowerPlanner native CMake is IDE-assist only; use native/build.bat for production builds.")
