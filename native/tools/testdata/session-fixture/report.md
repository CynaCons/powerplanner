# Session report

## Meta

- **dllVersion**: 2.10.0-fixture
- **pptxName**: session-fixture-live-failures.pptx
- **chartId**: chart-fixture-1
- **startTime**: 2026-07-18T14:22:00Z
- **screen**: `{"w": 1920, "h": 1080, "dpi": 96}`
- **recordingReason**: Synthetic fixture for session_report.py --selftest (R2). Encodes the three 2026-07-18 live-verify failure signatures from docs/session-recorder-spec.md §1.
- **eventCount**: 43

## Timeline

- **0ms** `note` seq=1 session-fixture start — real emitter dialect + silent create / healthy create / window-edge (g-instance lifecycle)
- **12ms** `paint` seq=2 surface=overlay count=1 tMs=4.2
- **15ms** `snapshot` seq=3 entities=3 chrome.ownSel=/ chrome.native=
- **20ms** `doc` seq=4 taskCount=2 rowCount=2 sig=2026-01-01|2026-06-30
- **840ms** `input` seq=5 surface=overlay msg=WM_LBUTTONDOWN hit=TASK/discovery zone=task pt=[410, 388]
- **842ms** `input` seq=6 surface=overlay msg=WM_LBUTTONUP hit=TASK/discovery zone=task pt=[410, 388]
  - **855ms** `nativeSel` kind=TASK_LABEL id=discovery hasChildShapeRange=True childKind=TASK_LABEL childId=discovery resolution=allowed_chart_root_but_child_selected
- **870ms** `paint` seq=8 surface=overlay count=2 tMs=3.1
- **880ms** `snapshot` seq=9 entities=3 chrome.ownSel=/ chrome.native=TASK_LABEL
- **900ms** `frame` seq=10 file=`frames/0001-nativeSel-label-child.png` trigger=nativeSel
- **2100ms** `input` seq=11 surface=overlay msg=WM_LBUTTONDOWN hit=ROW/r2 zone=row_band pt=[450, 412]
- **2110ms** `gesture` **lifecycle** start g=1 kind=Create id= payload={"rowId":"r2","anchor":[230,212]}
  - 2140ms update {"rowId":"r2","anchorDay":40,"currentDay":48}
  - ⚠️ **open gesture** — no commit/cancel after start (g=1 kind=Create id=)
- **2155ms** `paint` seq=14 surface=overlay count=3 tMs=5.0
- **2160ms** `snapshot` seq=15 entities=4 chrome.ownSel=/ chrome.native=TASK_LABEL
- **2170ms** `frame` seq=16 file=`frames/0002-create-hairline-preview.png` trigger=gesture-update
- **2800ms** `input` seq=17 surface=overlay msg=WM_LBUTTONUP hit=ROW/r2 zone=row_band pt=[500, 412]
- **2900ms** `paint` seq=18 surface=overlay count=4 tMs=4.5
- **3500ms** `paint` seq=19 surface=overlay count=5 tMs=3.8
- **3600ms** `snapshot` seq=20 entities=4 chrome.ownSel=/ chrome.native=TASK_LABEL
- **4200ms** ⚠️ `error` seq=21 where=StartCreateGesture/commit hr=-2147467259 msg=create commit path never entered (swallowed); no TASK child materialized
- **5000ms** `input` seq=22 surface=appbar msg=WM_LBUTTONDOWN hit=APPBAR_CMD/insert-task zone=button pt=[100, 20]
- **5050ms** `op` seq=23 cmd=insert-task dispatchMs=42.5 hr=0 phases=['hit', 'dispatch', 'rebuild']
- **5100ms** `ownSel` kind=TASK id=wireframes reason=ApplyClickSelection
- **5120ms** `nativeSel` kind=CHART_ROOT id=chart-fixture-1 hasChildShapeRange=False childKind= childId= resolution=suppressed_to_chart_root
- **5150ms** `doc` seq=26 taskCount=2 rowCount=2 sig=2026-01-01|2026-06-30
- **6000ms** `gesture` **lifecycle** start g=2 kind=TaskBody id=wireframes payload={"rowId":"r2","anchor":[380,418]}
  - 6050ms update {"rowId":"r2","deltaDays":7,"candidateStart":"2026-02-08","candidateEnd":"202...
  - 6150ms **commit** g=2 id=wireframes duration=150ms result=ok hr=0
- **6200ms** `frame` seq=31 file=`frames/0003-move-commit.png` trigger=gesture-commit
- **7000ms** `input` seq=32 surface=overlay msg=WM_LBUTTONDOWN hit=ROW/r1 zone=row_band pt=[470, 430]
- **7010ms** `gesture` **lifecycle** start g=3 kind=Create id= payload={"rowId":"r1","anchor":[250,230]}
  - 7040ms update {"rowId":"r1","anchorDay":20,"currentDay":35}
  - 7200ms **commit** g=3 id=newTaskHealthy duration=190ms result=ok hr=0
- **7210ms** `doc` seq=37 taskCount=3 rowCount=2 sig=2026-01-01|2026-06-30
- **8000ms** `input` seq=38 surface=overlay msg=WM_LBUTTONDOWN hit=WINDOW/ zone=window_edge_l pt=[300, 360]
- **8010ms** `gesture` **lifecycle** start g=4 kind=WindowEdgeL id= payload={"rowId":"","anchor":[80,160]}
  - 8050ms update {"candidateStart":"2026-01-15","candidateEnd":"2026-06-30"}
  - 8200ms **commit** g=4 id= duration=190ms result=ok hr=0
- **8300ms** `note` seq=43 session-fixture end

## Snapshot / entity diffs

_4 snapshot(s). Diffs between consecutive dumps:_

### Snapshot seq=3 t=15ms (baseline)
- entities: 3
- chrome: `{"appBarVisible":true,"appBarContext":"document","ownSelKind":"","ownSelId":"","nativeSelKind":"","rowCount":2}`

### Diff → seq=9 t=880ms
**Chrome changes:**
- chrome.nativeSelKind: '' → 'TASK_LABEL'
**Entity changes (id+kind):**
- ~ TASK_LABEL/discovery: flags.selectedNative: False → True

### Diff → seq=15 t=2160ms
**Chrome changes:**
- chrome.gestureKind: <missing> → "Create"
**Entity changes (id+kind):**
- ⚠️ + TASK/preview-create slideRect=[210, 230, 50, 1] h=1.0  ← **1px-high entity (hairline preview signature)**

### Diff → seq=20 t=3600ms
**Chrome changes:**
- chrome.gestureKind: "Create" → <missing>
_Entities unchanged._


## Frames

- t=900ms seq=10 `frames/0001-nativeSel-label-child.png` surface=overlay trigger=nativeSel
- t=2170ms seq=16 `frames/0002-create-hairline-preview.png` surface=overlay trigger=gesture-update
- t=6200ms seq=31 `frames/0003-move-commit.png` surface=overlay trigger=gesture-commit

## Anomalies

- **gesture_start_without_commit_or_cancel**: t=2110ms seq=12 g=1 kind=Create id= (failure signature: silent create/drag — no commit)
- **nativeSel_child_without_suppression_or_ownSel_mirror**: t=855ms seq=7 kind=TASK_LABEL id=discovery hasChildShapeRange=True childKind=TASK_LABEL childId=discovery resolution=allowed_chart_root_but_child_selected (failure signature: task-bar click selected label child)
- **error_event**: t=4200ms seq=21 where=StartCreateGesture/commit hr=-2147467259 msg=create commit path never entered (swallowed); no TASK child materialized
- **paint_gap_during_gesture**: gap=1285ms between paint seq=8@870ms and seq=14@2155ms (threshold 500ms)
- **paint_gap_during_gesture**: gap=745ms between paint seq=14@2155ms and seq=18@2900ms (threshold 500ms)
- **paint_gap_during_gesture**: gap=600ms between paint seq=18@2900ms and seq=19@3500ms (threshold 500ms)
- **paint_gap_during_gesture**: gap=2600ms between paint seq=19@3500ms and seq=29@6100ms (threshold 500ms)
- **paint_gap_during_gesture**: gap=1000ms between paint seq=29@6100ms and seq=35@7100ms (threshold 500ms)
- **paint_gap_during_gesture**: gap=1000ms between paint seq=35@7100ms and seq=41@8100ms (threshold 500ms)
- **hairline_preview_entity**: t=2160ms seq=15 entity TASK/preview-create height=1.0px slideRect=[210, 230, 50, 1] (failure signature: 1px create preview)
- **hairline_preview_entity**: t=3600ms seq=20 entity TASK/preview-create height=1.0px slideRect=[210, 230, 50, 1] (failure signature: 1px create preview)
