# SRS - Task Progress Editing (ASPICE-style)

## Feature Purpose
Tasks have a percentComplete (0-100). Visual progress bar is shown on the task bar. User must be able to easily select a task and edit its progress without the visual being hidden by text or selection chrome.

## User Interactions
- Click task bar (body or progress area) → task selected (TASK), visual feedback.
- App bar or context or card for selected task allows changing percent (nudge or direct).
- Progress bar remains visible and distinguishable from task label text.

## Software Requirements
- SR-PROG-01: Task selection (via hit on TASK or TASK_PROGRESS) must set ownSelKind=TASK and provide context for editing percent.
- SR-PROG-02: Progress indicator must not be completely overlapped by task label text; adjust layout or placement if needed (e.g. progress inside bar, label offset).
- SR-PROG-03: Changing percent via supported path (appbar if wired, card editor, or future direct) must update the model, re-emit shapes, and preserve selection + visuals.
- SR-PROG-04: E2E trace must verify after task select that progress is "actionable" (e.g. via state or available actions) and no disappearance of related elements.

## Verification
Use native e2e trace 'task-select-progress': select task, check sel=TASK, rowLabelCount stable, screenshots show progress bar distinct, optionally perform percent change and verify.

## Current Gaps (to be closed)
- App bar for TASK in current build may not surface direct percent controls (relies on edit card or context).
- Overlap reported by user; layout in scene/builder for bar + label + progress needs review.
