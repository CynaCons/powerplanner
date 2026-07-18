# Visual goldens (Phase 13 v2.8.2)

MD5 + size compared by `harness_driver.py golden` / scenario `--update-goldens`.

## Policy

See `docs/native-visual-dpi-policy.md`. Update only intentionally.

## Expected names (wire via scenarios)

| Golden name | Source artifact (typical) |
|-------------|---------------------------|
| `appbar-document.png` | `native/build/gallery-document-appbar.png` or `ab-none.png` |
| `appbar-task.png` | `native/build/gallery-task-appbar.png` or `ab-task.png` |
| `chrome-calm-idle.png` | calm chrome capture when available |

Seed: run harness gallery/matrix once, then:

```powershell
python native/tools/harness_driver.py golden native/build/ab-task.png appbar-task --update
python native/tools/harness_driver.py golden native/build/ab-none.png appbar-document --update
```

**Golden DPI:** whatever the capture machine used; record in commit message.
