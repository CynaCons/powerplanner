import { useRef, useState } from 'react';
import type { GanttDocument, ISODate, TimeScale } from '../types/document';
import { dateToX } from '../layout/timeAxis';
import { formatForScale } from './TimeAxis';
import { addDays } from '../utils/dates';
import { snapDelta } from '../utils/snap';
import { useDocumentStore } from '../stores/documentStore';
import { useEditStore } from '../stores/editStore';
import { useSelectionStore, useIsSelected } from '../stores/selectionStore';
import { InlineLabel } from '../app/InlineLabel';

interface Props {
  doc: GanttDocument;
  viewStart: string;
  pxPerDay: number;
  /** The rows' own height, which is where the rules stop. */
  chartHeight: number;
  /** How much of the plot the window can show, chrome excluded. */
  bodyHeight: number;
  /** The plot's scroll offset, which the chips have to cancel out. */
  scrollY: number;
}

/** The foot band, and the chip that sits in it (SRS-ELEM-053). */
const BAND_H = 30;
const CHIP_H = 19;
const CHIP_PAD_PX = 16;
/** Roughly the advance of the 10px chip face — enough to size a box round a name. */
const CHIP_CHAR_PX = 6.2;
/** How far the pointer must travel before a press counts as a drag (SRS-EDIT-053). */
const DRAG_THRESHOLD_PX = 3;

/**
 * Where the band starts, in the scrolled group's own coordinates
 * (SRS-ELEM-062).
 *
 * Under the last row while the whole plan fits the window; pinned to the foot
 * of the visible plot once it does not. The first version only ever did the
 * former, which read correctly in a 108px test fixture and was indefensible on
 * a real plan: the sample is 1584px of rows in a 778px plot, so the band was
 * drawn at y=1632 inside a clip that ends at 826 and was cut away entirely. A
 * planner who added a deadline marker saw no chip and no MARKERS eyebrow at
 * all, and the only way to reach the name was to wheel 888px to the foot of a
 * 39-row plan — where the band then landed in the very strip the minimap was
 * deleted for occupying (SRS-ELEM-060). "I cannot edit the text" was still
 * true.
 *
 * The `+ scrollY` cancels the parent group's `translate(… -scrollY)`, which is
 * the same trick the today pill uses to ride the top of the plot
 * (SRS-TAX-053) — there by sitting outside the scrolled group, here by
 * undoing it, because the chips share a drag gesture with the rules and
 * splitting the component would split that state in two.
 */
export function footBandTop(chartHeight: number, bodyHeight: number, scrollY = 0): number {
  // The inputs are screened before they are clamped, because `Math.max(0, NaN)`
  // is NaN and every element of the band derives from this one number: a single
  // unmeasured input becomes five malformed SVG attributes — the band rect's y
  // and height, the divider's y1/y2, the eyebrow's y, the chip's y and the
  // rule's y1/y2 — repeated per marker, per frame, until the layout settles.
  const rows = Number.isFinite(chartHeight) ? Math.max(0, chartHeight) : 0;
  const visible = Number.isFinite(bodyHeight) ? Math.max(0, bodyHeight - BAND_H) : 0;
  const offset = Number.isFinite(scrollY) ? scrollY : 0;
  return Math.min(rows, visible) + offset;
}

/** A chip wide enough for its own name, and wide enough to hit. */
export function chipWidth(label: string): number {
  return Math.max(44, label.length * CHIP_CHAR_PX + CHIP_PAD_PX);
}

/**
 * The date a release commits (SRS-ELEM-052).
 *
 * The same two steps a bar drag takes — whole days from the pointer, then the
 * scale's grid — so a marker lands on the dates a bar lands on, and the
 * readout drawn during the drag is this value and not the raw pointer delta.
 * Under the threshold nothing moves, so a click that selects cannot also
 * nudge the date (SRS-EDIT-053 made the same mistake cost a label's position).
 */
export function markerDragDate(
  original: ISODate,
  dx: number,
  pxPerDay: number,
  scale: TimeScale,
  snapOn: boolean,
): ISODate {
  if (Math.abs(dx) < DRAG_THRESHOLD_PX) return original;
  const days = Math.round(dx / pxPerDay);
  return addDays(original, snapOn ? snapDelta(days, scale) : days);
}

interface MarkerDrag {
  id: string;
  original: ISODate;
  dx: number;
}

export function Markers({ doc, viewStart, pxPerDay, chartHeight, bodyHeight, scrollY }: Props) {
  const updateMarker = useDocumentStore((s) => s.updateMarker);
  const setSelection = useSelectionStore((s) => s.setSelection);
  const addToSelection = useSelectionStore((s) => s.add);
  const isSelected = useIsSelected();
  const editingId = useEditStore((s) => s.editingTaskId);
  const beginEdit = useEditStore((s) => s.beginEdit);
  const endEdit = useEditStore((s) => s.endEdit);
  const snapEnabled = useEditStore((s) => s.snapEnabled);
  const [drag, setDrag] = useState<MarkerDrag | null>(null);

  const scale = doc.calendar.scale;
  const bandTop = footBandTop(chartHeight, bodyHeight, scrollY);
  // The rules stop at the band wherever the band is, so the dashed line always
  // meets its own chip and never runs on past it.
  const ruleBottom = Math.max(0, bandTop);

  /**
   * What the drag arithmetic needs, assigned on every render so the
   * listener installed at mousedown still reads today’s value (the same
   * reason `ChartArea` carries its geometry in a ref). Reading these from
   * the closure instead is how a preview and the value it commits come to
   * disagree: the pill said one date and the release wrote another.
   */
  const live = useRef({ pxPerDay, scale, snapEnabled });
  live.current = { pxPerDay, scale, snapEnabled };
  const hasMarkers = doc.markers.length > 0;

  /**
   * Selection and the drag, both handled here rather than by the chart's
   * delegated mousedown: that handler reads a marker as selection-only ("no
   * drag"), and it lives in another file. Stopping the event keeps one gesture
   * from being read twice.
   *
   * The listeners are attached per gesture and read the viewport and the snap
   * through `getState()`, so a zoom or a magnet toggled mid-drag is honoured
   * rather than frozen at the value mousedown closed over.
   */
  const onMarkerDown = (e: React.MouseEvent, id: string, date: ISODate) => {
    e.stopPropagation();
    e.preventDefault();
    const additive = e.shiftKey || e.metaKey || e.ctrlKey;
    if (additive) addToSelection({ kind: 'marker', id });
    else setSelection([{ kind: 'marker', id }]);
    if (e.button !== 0) return;

    const startClientX = e.clientX;
    let dx = 0;
    setDrag({ id, original: date, dx: 0 });

    const detach = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      window.removeEventListener('keydown', key);
    };
    const move = (ev: globalThis.MouseEvent) => {
      dx = ev.clientX - startClientX;
      setDrag({ id, original: date, dx });
    };
    const up = () => {
      detach();
      setDrag(null);
      const l = live.current;
      const next = markerDragDate(date, dx, l.pxPerDay, l.scale, l.snapEnabled);
      // One write, on release, so a thirty-day move costs one Ctrl+Z and not
      // thirty (SRS-EDIT-022): `updateMarker` has no frame key to coalesce on.
      if (next !== date) updateMarker(id, { date: next });
    };
    // Escape abandons the gesture and writes nothing, as it does everywhere
    // else on this surface (SR-IXC-07, SR-IXC-09).
    const key = (ev: globalThis.KeyboardEvent) => {
      if (ev.key !== 'Escape') return;
      detach();
      setDrag(null);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
    window.addEventListener('keydown', key);
  };

  return (
    <g>
      {/*
        The band, and only when there is something in it (SRS-ELEM-054). It is
        drawn before the chips so they sit on it, and it runs wider than any
        plot because this component is not told the plot's width —
        `chart-plot-clip` is what ends it at the right edge.
      */}
      {hasMarkers && (
        <g>
          {/* Opaque, and it takes its own presses. Pinned, the band genuinely
              covers whichever rows are at the foot of the window, so a planner
              must not read a bar through it and a click on it must not select
              a row that is not on screen. Where the plan fits the window the
              band is still below the last row and covers nothing. */}
          <rect x={0} y={bandTop} width={20000} height={BAND_H} fill="var(--color-bg-subtle)" />
          <line x1={0} y1={bandTop} x2={20000} y2={bandTop} stroke="var(--color-border-soft)" strokeWidth={1} />
          {/* The label the minimap never had: anything parked at the foot of
              the chart has to say what it is (SRS-ELEM-060). */}
          <text
            x={6}
            y={bandTop + 20}
            fontSize={8.5}
            fontWeight={700}
            letterSpacing={0.9}
            fill="var(--color-text-dim)"
          >
            MARKERS
          </text>
        </g>
      )}

      {doc.markers.map((m) => {
        const dragging = drag && drag.id === m.id ? drag : null;
        const shown = dragging
          ? markerDragDate(m.date, dragging.dx, pxPerDay, scale, snapEnabled)
          : m.date;
        const x = dateToX(shown, viewStart, pxPerDay);
        const color = m.color || (m.type === 'deadline' ? 'var(--color-deadline)' : 'var(--color-text-soft)');
        const selected = isSelected(m.id);
        const w = chipWidth(m.label);
        const chipX = x - w / 2;
        const chipY = bandTop + (BAND_H - CHIP_H) / 2;
        const editing = editingId === m.id;

        return (
          <g key={m.id}>
            <line
              x1={x}
              y1={0}
              x2={x}
              y2={ruleBottom}
              stroke={color}
              strokeWidth={selected || dragging ? 2 : 1.2}
              strokeDasharray={m.type === 'deadline' ? '6 4' : '2 2'}
              opacity={0.85}
            />
            {/* The rule's own target. A 1.2px line is not something a pointer
                can find, which is why nothing about a marker was reachable
                (SRS-ELEM-051). */}
            <rect
              data-pp-kind="marker"
              data-pp-id={m.id}
              x={x - 5}
              y={0}
              width={10}
              height={ruleBottom}
              fill="transparent"
              style={{ cursor: 'ew-resize' }}
              onMouseDown={(e) => onMarkerDown(e, m.id, m.date)}
              onDoubleClick={(e) => { e.stopPropagation(); beginEdit(m.id); }}
            />

            {/* The chip in the foot band: the marker's name where the owner
                asked for it, and the second place the marker can be grabbed. */}
            <rect
              data-pp-kind="marker"
              data-pp-id={m.id}
              x={chipX}
              y={chipY}
              width={w}
              height={CHIP_H}
              rx={4}
              fill="var(--color-bg-panel)"
              stroke={color}
              strokeWidth={1}
              style={{ cursor: 'move' }}
              onMouseDown={(e) => onMarkerDown(e, m.id, m.date)}
              onDoubleClick={(e) => { e.stopPropagation(); beginEdit(m.id); }}
            />
            {editing ? (
              /* The name is not also drawn under the field: the editor's
                 background is 0.92 alpha and the old value ghosts through it
                 (SRS-EDIT-051). */
              <InlineLabel
                text={m.label}
                x={chipX + 1}
                y={chipY + 1}
                width={Math.max(w, 96)}
                height={CHIP_H - 2}
                onCommit={(text) => {
                  const next = text.trim();
                  if (next && next !== m.label) updateMarker(m.id, { label: next });
                  endEdit();
                }}
                onCancel={endEdit}
              />
            ) : (
              <text
                x={x}
                y={chipY + 13}
                textAnchor="middle"
                fontSize={10}
                fontWeight={600}
                fill={color}
                pointerEvents="none"
              >
                {m.label}
              </text>
            )}
            {selected && !editing && (
              <rect
                x={chipX - 3}
                y={chipY - 3}
                width={w + 6}
                height={CHIP_H + 6}
                rx={6}
                fill="none"
                stroke="var(--color-accent)"
                strokeDasharray="3 3"
                pointerEvents="none"
              />
            )}

            {/* The date the release will commit, while the pointer is still
                down (SR-IXC-04, SRS-ELEM-052). It rides above the chip rather
                than following the pointer: this component is inside the plot
                clip, and a pill at the pointer would be cut off at exactly the
                edges where a planner needs to read it. */}
            {dragging && (
              <g pointerEvents="none">
                <rect x={x - 46} y={chipY - 23} width={92} height={18} rx={9} fill="var(--color-accent)" />
                <text
                  x={x}
                  y={chipY - 10}
                  textAnchor="middle"
                  fontSize={10}
                  fontWeight={600}
                  fill="var(--color-bg-panel)"
                >
                  {formatForScale(shown, scale)}
                </text>
              </g>
            )}
          </g>
        );
      })}
    </g>
  );
}
