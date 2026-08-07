# Visual-vocabulary gate checklist (Phase 13 v2.8.2)

Against `spec/visual-vocabulary.md` + native gallery / CHROME CALM captures.

| Element | Spec expectation | Automated | Review |
|---------|------------------|-----------|--------|
| Task bar | Rounded rect, swatch fill, progress solid | partial (SCENE VIZ / ops) | gallery-task.png |
| Milestone | Diamond / rotated square | partial | gallery-milestone.png |
| Marker | Vertical line + pill | partial | gallery-marker.png |
| Dep elbow | Below text, single arrowhead | partial (ops SCENE VIZ) | gallery-dep-link.png |
| Row rail labels | Readable, no full wash over text | automated (row label counts) | gallery-row.png |
| Selection chrome | Hairline overall; calm row accent | CHROME CALM markers | appbar matrix |

**Web ↔ native minimum parity set**

| Fixture | Web | Native |
|---------|-----|--------|
| basic-chart | `spec/visual/basic-chart.svg` | gallery document frame |

Full pixel match **deferred**; operators compare layout structure (row count, bar spans) not anti-aliased pixels.

**Slideshow / multi-monitor:** Demo only (`docs/native-visual-dpi-policy.md`).
