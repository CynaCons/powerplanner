"""
Coordinator Bridge for Native Feedback Loop.

Example integration for the onslide-coordinator.

After a sub-agent completes a unit (especially S3 row-selection, S4 context etc.),
the coordinator can call this to get structured feedback.

Usage in coordinator context (via run_terminal_command or direct import if Python sub-agent):
    from native.tools.coordinator_bridge import run_feedback_for_unit
    feedback = run_feedback_for_unit("s3-row-selection", scenario="row_selection")
    # feedback is dict with report + summary. Append to coordinator log.
    # Use powerplan skill or vision for PNG review if available.

This keeps the loop using only new tooling + existing harness exes.
"""

from __future__ import annotations
from pathlib import Path
from typing import Dict, Any
import json

try:
    from .harness_driver import run_scenario, HarnessReport
except ImportError:
    from harness_driver import run_scenario, HarnessReport  # for direct run / CLI

NATIVE_ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = NATIVE_ROOT / "build"


def run_feedback_for_unit(unit_id: str, scenario: str = "row_selection") -> Dict[str, Any]:
    """
    Run feedback for a specific unit id (e.g. 's3-row-selection').
    Returns structured dict suitable for coordinator log or sub-agent handoff.
    """
    report: HarnessReport = run_scenario(scenario)
    summary = {
        "unit_id": unit_id,
        "scenario": scenario,
        "status": report.status,
        "duration_s": report.duration_s,
        "markers": report.markers_found,
        "artifacts": report.artifacts,
        "golden_results": report.golden_comparisons,
        "harness_report": report.harness_report,
        "report_path": str((BUILD_DIR / f"{report.exe}_report.json").relative_to(NATIVE_ROOT)),
        "stdout_tail": report.stdout_tail[:1500],
        "notes": report.notes,
    }
    # Always write a unit-specific summary for easy pickup by coordinator / async reviewer
    out = BUILD_DIR / f"feedback-{unit_id}.json"
    out.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    summary["feedback_summary_path"] = str(out.relative_to(NATIVE_ROOT))
    return summary


if __name__ == "__main__":
    import sys
    unit = sys.argv[1] if len(sys.argv) > 1 else "s3-row-selection"
    scen = sys.argv[2] if len(sys.argv) > 2 else "row_selection"
    res = run_feedback_for_unit(unit, scen)
    print(json.dumps(res, indent=2))
