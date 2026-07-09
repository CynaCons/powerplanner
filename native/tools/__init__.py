"""
Native tools for agent-driven harness execution and feedback.

See harness_driver.py for the main entry point.
"""
from .harness_driver import (
    run_harness,
    run_scenario,
    run_appbar_matrix,
    run_full_overlay_gate,
    run_reflow_check,
    run_row_selection_scenario,
    compare_to_golden,
    HarnessReport,
)
from .coordinator_bridge import run_feedback_for_unit

__all__ = [
    "run_harness",
    "run_scenario",
    "run_appbar_matrix",
    "run_full_overlay_gate",
    "run_reflow_check",
    "run_row_selection_scenario",
    "compare_to_golden",
    "HarnessReport",
    "run_feedback_for_unit",
]
