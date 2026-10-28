import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { layoutDocument } from '../layout/engine';
import { buildAxis, dateToX, xToDate } from '../layout/timeAxis';
import { todayISO, isWeekend, addDays } from '../utils/dates';
import { TimeAxis } from '../renderer/TimeAxis';
import { Rows } from '../renderer/Rows';
import { Summaries } from '../renderer/Summaries';
import { TaskBars } from '../renderer/TaskBars';
import { Baseline } from '../renderer/Baseline';
import { Minimap } from '../renderer/Minimap';
import { Milestones } from '../renderer/Milestones';
import { Brackets } from '../renderer/Brackets';
import { Dependencies } from '../renderer/Dependencies';
import { Markers } from '../renderer/Markers';
import { RowGutter } from '../renderer/RowGutter';
import { useChartInteractions } from './useChartInteractions';
import type { ChartGeometry } from '../layout/geometry';
import { chordKeys } from './keymap';
import { ConnectorOverlay } from './ConnectorOverlay';
import { useToolStore, cursorForTool } from '../stores/toolStore';

const ROW_GUTTER_W_BASE = 200;
const ROW_GUTTER_W_MIN = 120;
const ROW_GUTTER_W_MAX = 520;
/** The row list is draggable-wide; a long package name needs more than 200px. */
const GUTTER_KEY = 'powerplanner:gutter-width:v1';

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

  const [gutterW, setGutterW] = useState<number>(() => {
    try {
      const stored = Number(localStorage.getItem(GUTTER_KEY));
      if (Number.isFinite(stored) && stored >= ROW_GUTTER_W_MIN) {
        return Math.min(ROW_GUTTER_W_MAX, stored);
      }
    } catch { /* a blocked localStorage costs the preference, not the app */ }
    return ROW_GUTTER_W_BASE;
  });
  const [draggingGutter, setDraggingGutter] = useState(false);

  // A narrow window gets the minimum regardless of preference: the chart has
  // to keep enough room to be a chart.
  const ROW_GUTTER_W = size.width < 600 ? ROW_GUTTER_W_MIN : Math.min(gutterW, Math.max(ROW_GUTTER_W_MIN, size.width - 320));

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
      try { localStorage.setItem(GUTTER_KEY, String(gutterW)); } catch { /* non-fatal */ }
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
    return () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
  }, [draggingGutter, gutterW]);
  const chartWidth = Math.max(100, size.width - ROW_GUTTER_W);

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

  /** Where the chart sheet ends: after the last row, or at the viewport edge
   *  when the plan is taller than the window. */
  const sheetBottom = Math.min(size.height, AXIS_TOTAL_H + layout.chartHeight);

  // The wheel scrolls the body. A plan that already fits has nothing to
  // scroll, so maxScroll is 0 and the view stays put. The command bar floats
  // over the foot of the chart, so the scroll range carries its height too —
  // otherwise the last row can never be read.
  const CHROME_H = 52;
  const bodyHeight = Math.max(0, size.height - AXIS_TOTAL_H - CHROME_H);
  const maxScroll = Math.max(0, layout.chartHeight - bodyHeight);
  const scrollY = useViewportStore((s) => s.scrollY);

  const today = todayISO();
  const todayX = dateToX(today, viewStart, pxPerDay);
  const showToday = todayX >= 0 && todayX <= chartWidth;

  // Weekend stripes (for day scale)
  const weekendStripes = useMemo(() => {
    if (doc.calendar.scale !== 'day' && doc.calendar.scale !== 'week') return [];
    const stripes: { x: number; w: number }[] = [];
    const days = Math.ceil(chartWidth / pxPerDay) + 1;
    for (let i = 0; i < days; i++) {
      const iso = addDays(viewStart, i);
      if (isWeekend(iso, doc.calendar.workingDays)) {
        const x = i * pxPerDay;
        stripes.push({ x, w: pxPerDay });
      }
    }
    return stripes;
  }, [viewStart, pxPerDay, chartWidth, doc.calendar.scale, doc.calendar.workingDays]);

  /**
   * The geometry every gesture inverts, assigned on every render so a window
   * listener installed once still reads today's value (SRS-EDIT-033). Four
   * copies of this used to be captured by closure, each stale in its own way.
   */
  const geomRef = useRef<ChartGeometry>({ gutter: ROW_GUTTER_W, axisH: AXIS_TOTAL_H, scrollY, chartWidth, maxScroll, layout });
  geomRef.current = { gutter: ROW_GUTTER_W, axisH: AXIS_TOTAL_H, scrollY, chartWidth, maxScroll, layout };

  const { handleSvgMouseDown, handleWheel, handleContextMenu, dragPreview, dragCursor, depDrag, lassoDrag } = useChartInteractions({
    svgRef,
    geomRef,
  });
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
        onWheel={handleWheel}
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
          {/* Weekend stripes */}
          <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H - scrollY})`}>
            {weekendStripes.map((s, i) => (
              <rect key={i} x={s.x} y={0} width={s.w} height={layout.chartHeight} fill="var(--color-weekend)" opacity={0.4} />
            ))}
          </g>

          <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H - scrollY})`}>
            {/* Today line. The pill belongs to the header, not to the body. */}
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

          <Brackets layout={layout} />
          <Summaries layout={layout} />
          <Baseline layout={layout} />
          <TaskBars layout={layout} dragPreview={dragPreview} />
          <Milestones layout={layout} dragPreview={dragPreview} />
          <Dependencies layout={layout} />
          <Markers doc={doc} viewStart={viewStart} pxPerDay={pxPerDay} chartHeight={layout.chartHeight} />
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
          {/* The TODAY pill sits in the header, so it neither scrolls away nor
              falls outside the body clip at negative local y. */}
          {showToday && (
            <g transform={`translate(${ROW_GUTTER_W + todayX - 22} ${AXIS_TOTAL_H - 16})`} pointerEvents="none">
              <rect x={0} y={0} width={44} height={14} rx={3} fill="var(--color-today)" />
              <text x={22} y={11} textAnchor="middle" fontSize={9} fontWeight={600} fill="white" letterSpacing={0.3}>
                TODAY
              </text>
            </g>
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
      </svg>
      <Minimap chartWidth={chartWidth} rowGutterWidth={ROW_GUTTER_W} />
    </div>
  );
}

export { dateToX, xToDate };
