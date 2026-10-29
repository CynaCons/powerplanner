import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { layoutDocument } from '../layout/engine';
import { buildAxis, dateToX, xToDate } from '../layout/timeAxis';
import { todayISO } from '../utils/dates';
import { gridDelimiterOf, weekendDisplayOf } from '../types/document';
import { TimeAxis, DragDatePill, delimiterXs, nonWorkingBands } from '../renderer/TimeAxis';
import { Rows } from '../renderer/Rows';
import { Summaries } from '../renderer/Summaries';
import { TaskBars } from '../renderer/TaskBars';
import { Baseline } from '../renderer/Baseline';
import { Milestones } from '../renderer/Milestones';
import { Brackets } from '../renderer/Brackets';
import { Dependencies, DependencyHits } from '../renderer/Dependencies';
import { Markers } from '../renderer/Markers';
import { RowGutter } from '../renderer/RowGutter';
import { useChartInteractions } from './useChartInteractions';
import type { ChartGeometry } from '../layout/geometry';
import { chordKeys } from './keymap';
import { ConnectorOverlay } from './ConnectorOverlay';
import { useToolStore, cursorForTool } from '../stores/toolStore';
import {
  gutterWidthFor,
  GUTTER_W_MIN as ROW_GUTTER_W_MIN,
  GUTTER_W_MAX as ROW_GUTTER_W_MAX,
} from '../layout/gutterWidth';

/** The row list is draggable-wide; a long package name needs more than 200px. */
const GUTTER_KEY = 'powerplanner:gutter-width:v1';
/**
 * Room kept below the last row for labels placed under their bar
 * (SRS-LAY-052). Taking it out of the last row's own band is what made the
 * bottom label vanish; the band is additional height, inside the clip.
 */
const FOOT_BAND_H = 30;

export function ChartArea() {
  const containerRef = useRef<HTMLDivElement>(null);
  const svgRef = useRef<SVGSVGElement>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const clear = useSelectionStore((s) => s.clear);

  const doc = useDocumentStore((s) => s.doc);
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const fit = useViewportStore((s) => s.fit);

  useLayoutEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => {
      setSize({ width: el.clientWidth, height: el.clientHeight });
    });
    ro.observe(el);
    setSize({ width: el.clientWidth, height: el.clientHeight });
    return () => ro.disconnect();
  }, []);

  // `null` means "nobody has chosen a width": the panel then follows its own
  // names (SRS-LAY-050). A stored preference, or one drag of the divider, ends
  // that for good — an auto width that kept overriding the planner's would be
  // the panel arguing back.
  const [gutterW, setGutterW] = useState<number | null>(() => {
    try {
      const stored = Number(localStorage.getItem(GUTTER_KEY));
      if (Number.isFinite(stored) && stored >= ROW_GUTTER_W_MIN) {
        return Math.min(ROW_GUTTER_W_MAX, stored);
      }
    } catch { /* a blocked localStorage costs the preference, not the app */ }
    return null;
  });
  const [draggingGutter, setDraggingGutter] = useState(false);

  const autoGutterW = useMemo(() => gutterWidthFor(doc.rows), [doc.rows]);

  // A narrow window gets the minimum regardless of preference: the chart has
  // to keep enough room to be a chart.
  const ROW_GUTTER_W = size.width < 600
    ? ROW_GUTTER_W_MIN
    : Math.min(gutterW ?? autoGutterW, Math.max(ROW_GUTTER_W_MIN, size.width - 320));

  useEffect(() => {
    if (!draggingGutter) return;
    const move = (e: globalThis.MouseEvent) => {
      const el = containerRef.current;
      if (!el) return;
      const next = e.clientX - el.getBoundingClientRect().left;
      setGutterW(Math.max(ROW_GUTTER_W_MIN, Math.min(ROW_GUTTER_W_MAX, next)));
    };
    const up = () => {
      setDraggingGutter(false);
      // A press that never moved leaves `gutterW` null; storing "null" would
      // read back as NaN and the auto width would be lost for good.
      if (gutterW !== null) {
        try { localStorage.setItem(GUTTER_KEY, String(gutterW)); } catch { /* non-fatal */ }
      }
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
    return () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
  }, [draggingGutter, gutterW]);
  const chartWidth = Math.max(100, size.width - ROW_GUTTER_W);

  // `--row-gutter-w` was a static 200px in global.css that nothing updated. It
  // happened to equal the old constant, so it read as true; now that the width
  // is measured it would lie, and the axis-zoom spec converts a pointer offset
  // into a date with it. Publish the real number instead of deleting a token
  // something already reads (SRS-LAY-050).
  useEffect(() => {
    document.documentElement.style.setProperty('--row-gutter-w', `${ROW_GUTTER_W}px`);
  }, [ROW_GUTTER_W]);

  // Auto-fit on first load if document has tasks
  const didInitialFit = useRef(false);
  useEffect(() => {
    if (didInitialFit.current) return;
    if (size.width < 300) return;
    if (doc.tasks.length === 0 && doc.milestones.length === 0) return;
    const dates = [
      ...doc.tasks.flatMap((t) => [t.start, t.end]),
      ...doc.milestones.map((m) => m.date),
    ];
    if (dates.length === 0) return;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    fit(min, max, chartWidth);
    didInitialFit.current = true;
  }, [size.width, size.height, doc.tasks, doc.milestones, fit, chartWidth]);

  const layout = useMemo(
    () => layoutDocument({ doc, viewStart, pxPerDay }),
    [doc, viewStart, pxPerDay],
  );

  const axis = useMemo(
    () => buildAxis(viewStart, pxPerDay, chartWidth, doc.calendar.scale, doc.calendar.fiscalYearStart),
    [viewStart, pxPerDay, chartWidth, doc.calendar.scale, doc.calendar.fiscalYearStart],
  );

  /** The header band is exactly the bands the axis built — not a constant that
   *  happens to be near them. It was 56 for every scale while the real sum runs
   *  44 (week) to 64 (day), so the day-number band overflowed into the first row
   *  and every other scale left a dead strip. One number now feeds the clip, the
   *  transforms, the rule and the pointer arithmetic, so they cannot disagree. */
  const AXIS_TOTAL_H = axis.major.height + axis.minor.height + (axis.micro?.height ?? 0);

  /** Where the chart sheet ends: after the last row and its foot band, or at
   *  the viewport edge when the plan is taller than the window. */
  const sheetBottom = Math.min(size.height, AXIS_TOTAL_H + layout.chartHeight + FOOT_BAND_H);
  /** The plot's own height, foot band included — what the clips and the scroll
   *  clamp both have to agree on, or the band is drawn and then cut away. */
  const plotHeight = layout.chartHeight + FOOT_BAND_H;

  // The wheel scrolls the body. A plan that already fits has nothing to
  // scroll, so maxScroll is 0 and the view stays put. The command bar floats
  // over the foot of the chart, so the scroll range carries its height too —
  // otherwise the last row can never be read.
  const CHROME_H = 52;
  const bodyHeight = Math.max(0, size.height - AXIS_TOTAL_H - CHROME_H);
  const maxScroll = Math.max(0, plotHeight - bodyHeight);
  const scrollY = useViewportStore((s) => s.scrollY);

  /**
   * Bring a just-inserted row into view (SRS-EDIT-028).
   *
   * The chart scrolls through `scrollY` rather than a scrollable element, so
   * there is no `scrollIntoView` to lean on; the row's own offset in the
   * layout is the only place the answer lives.
   */
  const revealRowId = useViewportStore((s) => s.revealRowId);
  const clearReveal = useViewportStore((s) => s.clearReveal);
  const scrollBy = useViewportStore((s) => s.scrollBy);
  useEffect(() => {
    if (!revealRowId) return;
    const i = layout.visibleRows.findIndex((r) => r.id === revealRowId);
    if (i < 0) {
      clearReveal();
      return;
    }
    const top = layout.rowOffsets[i];
    const h = layout.rowHeights[i] ?? 0;
    let dy = 0;
    if (top < scrollY) dy = top - scrollY;
    else if (top + h > scrollY + bodyHeight) dy = top + h - (scrollY + bodyHeight);
    if (dy !== 0) scrollBy(dy, maxScroll);
    clearReveal();
  }, [revealRowId, layout, scrollY, bodyHeight, maxScroll, scrollBy, clearReveal]);

  const today = todayISO();
  const todayX = dateToX(today, viewStart, pxPerDay);
  const showToday = todayX >= 0 && todayX <= chartWidth;

  /**
   * The non-working-day mask (SRS-TAX-056).
   *
   * It used to be gated to the day and week scales, so at month or coarser it
   * was simply absent — and where it did paint it was 2.5% black at 0.4
   * opacity, about one percent against the sheet. Both together meant the
   * `workingDays` control in the gear panel had no visible effect at all.
   * Every scale now, at a contrast you can see, and `off` is a choice the
   * roadmap can state rather than something the zoom decides.
   */
  const weekendDisplay = weekendDisplayOf(doc.calendar);
  const weekendBands = useMemo(
    () =>
      weekendDisplay === 'off'
        ? []
        : nonWorkingBands(viewStart, pxPerDay, chartWidth, doc.calendar.workingDays),
    [weekendDisplay, viewStart, pxPerDay, chartWidth, doc.calendar.workingDays],
  );

  /**
   * The vertical delimiters (SRS-TAX-054).
   *
   * The plot drew no vertical rule at all except the today line, which is why
   * a dragged milestone felt like it was floating: there was nothing under it
   * to land on. The granularity is the roadmap's own setting and is read
   * straight through — not inferred from `calendar.scale` — because the owner's
   * decision was that month boundaries must be askable for while reading days.
   */
  const gridDelimiter = gridDelimiterOf(doc.calendar);
  const delimiters = useMemo(
    () => delimiterXs(gridDelimiter, viewStart, pxPerDay, chartWidth, doc.calendar.fiscalYearStart),
    [gridDelimiter, viewStart, pxPerDay, chartWidth, doc.calendar.fiscalYearStart],
  );

  /**
   * The geometry every gesture inverts, assigned on every render so a window
   * listener installed once still reads today's value (SRS-EDIT-033). Four
   * copies of this used to be captured by closure, each stale in its own way.
   */
  const geomRef = useRef<ChartGeometry>({ gutter: ROW_GUTTER_W, axisH: AXIS_TOTAL_H, scrollY, chartWidth, maxScroll, layout });
  geomRef.current = { gutter: ROW_GUTTER_W, axisH: AXIS_TOTAL_H, scrollY, chartWidth, maxScroll, layout };

  const { handleSvgMouseDown, handleContextMenu, dragPreview, dragPill, dragCursor, depDrag, lassoDrag } = useChartInteractions({
    svgRef,
    geomRef,
  });

  /**
   * The plot width the store anchors a pointer-less zoom against
   * (SRS-TAX-051). The chart is the only thing that knows it, and a zoom
   * raised from a button or a key has no other way to find the middle.
   */
  const setChartWidth = useViewportStore((s) => s.setChartWidth);
  useEffect(() => {
    setChartWidth(chartWidth);
  }, [chartWidth, setChartWidth]);
  const activeTool = useToolStore((s) => s.activeTool);

  const isEmpty = doc.tasks.length === 0 && doc.milestones.length === 0 && doc.brackets.length === 0;

  return (
    <div ref={containerRef} className="chart-area" onMouseDown={(e) => { if (e.target === e.currentTarget) clear(); }}>
      {isEmpty && (
        <div className="empty-state">
          <div className="empty-title">Your chart is empty</div>
          <div className="empty-hint">
            Right-click the canvas to add a task where you click, or press {chordKeys('mod+k').map((k) => (<kbd key={k}>{k}</kbd>))} for
            the command palette. To start from a worked example instead, use <strong>File → New
            roadmap</strong>.
          </div>
        </div>
      )}
      <svg
        ref={svgRef}
        width={size.width}
        height={size.height}
        onMouseDown={handleSvgMouseDown}
        onContextMenu={handleContextMenu}
        /* No `onWheel`: the interaction hook registers a non-passive native
           listener instead, because React's is passive and its
           `preventDefault()` does nothing (SRS-TAX-050). */
        style={{ display: 'block', cursor: dragCursor ?? cursorForTool(activeTool), userSelect: 'none' }}
      >
        {/* Background */}
        <rect x={0} y={0} width={size.width} height={size.height} fill="var(--color-bg)" />

        {/*
          The chart is a bounded sheet, not a surface that runs to the bottom of
          the window. The gutter panel and the division rule used to paint the
          full viewport height, so a five-row plan dissolved into several
          hundred pixels of empty panel and the chart never looked finished
          (owner report 2026-10-23). They now end with the content, and what is
          below the last row is plainly outside the sheet.
        */}
        <rect x={0} y={0} width={ROW_GUTTER_W} height={sheetBottom} fill="var(--color-bg-panel)" />
        <line
          x1={ROW_GUTTER_W}
          y1={0}
          x2={ROW_GUTTER_W}
          y2={sheetBottom}
          stroke={draggingGutter ? 'var(--color-accent)' : 'var(--color-border-soft)'}
          strokeWidth={draggingGutter ? 2 : 1}
        />
        <line
          x1={0}
          y1={sheetBottom}
          x2={size.width}
          y2={sheetBottom}
          stroke="var(--color-border-soft)"
        />

        {/*
          Clipping, and the one rule that governs it: a `clip-path` resolves in
          the REFERENCING element's own post-transform user space. These three
          rects are written in absolute coordinates, so each one must hang on a
          group that carries no transform; the scrolled groups sit inside.

          The chart used to hang a `y=0` clip on the group carrying
          `translate(gutter, axisH - scrollY)`, so the window was dragged up by
          exactly the scroll offset: the top edge rose and bars painted over the
          calendar, the bottom edge rose and the foot of the plan went blank,
          and the TODAY pill at negative local y was outside the window at every
          offset. Never put a clip on a transformed element again.
        */}
        <defs>
          <clipPath id="chart-body-clip">
            <rect x={0} y={AXIS_TOTAL_H} width={size.width} height={Math.max(0, sheetBottom - AXIS_TOTAL_H)} />
          </clipPath>
          <clipPath id="chart-plot-clip">
            <rect x={ROW_GUTTER_W} y={AXIS_TOTAL_H} width={chartWidth} height={Math.max(0, sheetBottom - AXIS_TOTAL_H)} />
          </clipPath>
          <clipPath id="chart-axis-clip">
            <rect x={ROW_GUTTER_W} y={0} width={chartWidth} height={AXIS_TOTAL_H} />
          </clipPath>
        </defs>

        {/* Row separators and gutter labels: the sheet, below the header */}
        <g clipPath="url(#chart-body-clip)">
          <g transform={`translate(0 ${AXIS_TOTAL_H - scrollY})`}>
            <Rows
              rows={layout.visibleRows}
              rowHeights={layout.rowHeights}
              rowOffsets={layout.rowOffsets}
              width={size.width}
            />
          </g>
          <g transform={`translate(0 ${AXIS_TOTAL_H - scrollY})`}>
            <RowGutter rows={layout.visibleRows} rowHeights={layout.rowHeights} rowOffsets={layout.rowOffsets} width={ROW_GUTTER_W} />
          </g>
        </g>

        {/* Everything right of the gutter, clipped to the plot area */}
        <g clipPath="url(#chart-plot-clip)">
          {/* The calendar painted down through the plot: the mask first, then
              the delimiters over it, then everything the plan is made of. */}
          <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H - scrollY})`} pointerEvents="none">
            {weekendBands.map((b, i) => (
              <rect key={`wk-${i}`} className="grid-weekend" x={b.x} y={0} width={b.w} height={layout.chartHeight} />
            ))}
            {delimiters.map((x, i) => (
              <line
                key={`gd-${i}`}
                className={gridDelimiter === 'day' ? 'grid-delimiter' : 'grid-delimiter grid-delimiter--period'}
                x1={x}
                y1={0}
                x2={x}
                y2={layout.chartHeight}
              />
            ))}
          </g>

          <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H - scrollY})`}>
            {/* Today's rule in the body. Its pill rides the top of the plot and
                is pinned, so neither one touches the calendar (SRS-TAX-053). */}
          {showToday && (
            <line
              x1={todayX}
              y1={0}
              x2={todayX}
              y2={layout.chartHeight}
              stroke="var(--color-today)"
              strokeWidth={1.5}
              pointerEvents="none"
            />
          )}

          {/* The connectors' press targets go UNDER everything that outranks
              them in `hitRank`, so paint order, the CSS cursor, :hover and
              `resolveHit` all agree (SRS-EDIT-077). Their ink is painted after
              the bars, below. */}
          <DependencyHits layout={layout} />
          <Brackets layout={layout} />
          <Summaries layout={layout} />
          <Baseline layout={layout} />
          <TaskBars layout={layout} dragPreview={dragPreview} />
          <Milestones layout={layout} dragPreview={dragPreview} />
          <Dependencies layout={layout} />
          {/* The chips ride the foot of the VISIBLE plot, so they are reachable
              on a plan taller than the window (SRS-ELEM-062); the rules still
              scroll with their rows. */}
          <Markers
            doc={doc}
            viewStart={viewStart}
            pxPerDay={pxPerDay}
            chartHeight={layout.chartHeight}
            bodyHeight={bodyHeight}
            scrollY={scrollY}
          />
          {depDrag && depDrag.depStartX != null && depDrag.depCurrentX != null && (
            <ConnectorOverlay
              fromX={depDrag.depStartX}
              fromY={depDrag.depStartY!}
              toX={depDrag.depCurrentX}
              toY={depDrag.depCurrentY!}
            />
          )}
          {lassoDrag && lassoDrag.lassoStartX != null && lassoDrag.lassoCurrentX != null && (
            <rect
              x={Math.min(lassoDrag.lassoStartX, lassoDrag.lassoCurrentX)}
              y={Math.min(lassoDrag.lassoStartY!, lassoDrag.lassoCurrentY!)}
              width={Math.abs(lassoDrag.lassoCurrentX - lassoDrag.lassoStartX)}
              height={Math.abs(lassoDrag.lassoCurrentY! - lassoDrag.lassoStartY!)}
              fill="var(--color-accent)"
              fillOpacity={0.1}
              stroke="var(--color-accent)"
              strokeDasharray="3 3"
              pointerEvents="none"
            />
          )}
        </g>

          {/*
            The today pill, below the bands and pinned against the scroll
            (SRS-TAX-053). It was painted INTO the finest calendar band at
            every scale, so it buried whichever label it landed on — the
            owner's capture has it eating "Nov" and leaving "ov". The calendar
            bands belong to the calendar; the pill rides the top of the plot
            and only a hairline crosses them. It sits inside the plot clip so
            it cannot stray over the gutter, and outside the scrolled group so
            it stays put.
          */}
          {showToday && (
            <g transform={`translate(${ROW_GUTTER_W + todayX} ${AXIS_TOTAL_H + 5})`} pointerEvents="none">
              <rect className="axis-today-pill" x={-23} y={0} width={46} height={16} rx={8} />
              <text
                className="axis-today-pill-text"
                x={0}
                y={11.5}
                textAnchor="middle"
                fontSize={9}
                fontWeight={700}
                letterSpacing={0.6}
              >
                TODAY
              </text>
            </g>
          )}
        </g>

        {/*
          The calendar header is painted LAST and owns the top layer. The clip
          keeps the body out of the band; the paint order means even a future
          element that escapes the clip cannot bury the structure the chart's
          whole claim rests on.
        */}
        <g clipPath="url(#chart-axis-clip)">
          <g transform={`translate(${ROW_GUTTER_W} 0)`}>
            <TimeAxis axis={axis} />
          </g>
          {/* Today crosses the bands as a 1px rule and nothing else, so the
              calendar labels survive it at every scale (SRS-TAX-053). */}
          {showToday && (
            <line
              className="axis-today-rule"
              x1={ROW_GUTTER_W + todayX}
              y1={0}
              x2={ROW_GUTTER_W + todayX}
              y2={AXIS_TOTAL_H}
              pointerEvents="none"
            />
          )}
        </g>

        {/* Bottom border between axis and chart */}
        <line x1={ROW_GUTTER_W} y1={AXIS_TOTAL_H} x2={size.width} y2={AXIS_TOTAL_H} stroke="var(--color-border-soft)" />
      {/* Resize grip for the row list, drawn last so nothing paints over it.
          Wider than the rule it sits on, because a 1px target is not a target. */}
      <rect
          x={ROW_GUTTER_W - 4}
          y={0}
          width={8}
          height={size.height}
          fill="transparent"
          style={{ cursor: 'col-resize' }}
          onMouseDown={(e) => { e.stopPropagation(); e.preventDefault(); setDraggingGutter(true); }}
        />

        {/* The live date readout, last and unclipped (SRS-TAX-058). A drag runs
            past the edges of the plot and a pill inside the plot clip would be
            cut off at exactly the moment the planner needs to read it. */}
        {dragPill && (
          <DragDatePill pill={dragPill} minX={ROW_GUTTER_W} maxX={ROW_GUTTER_W + chartWidth} />
        )}
      </svg>
    </div>
  );
}

export { dateToX, xToDate };
