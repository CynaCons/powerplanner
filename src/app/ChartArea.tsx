import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { layoutDocument } from '../layout/engine';
import { buildAxis, dateToX, xToDate } from '../layout/timeAxis';
import { todayISO, isWeekend, addDays } from '../utils/dates';
import { TimeAxis } from '../renderer/TimeAxis';
import { Rows } from '../renderer/Rows';
import { TaskBars } from '../renderer/TaskBars';
import { Milestones } from '../renderer/Milestones';
import { Brackets } from '../renderer/Brackets';
import { Dependencies } from '../renderer/Dependencies';
import { Markers } from '../renderer/Markers';
import { RowGutter } from '../renderer/RowGutter';
import { useChartInteractions } from './useChartInteractions';

const AXIS_TOTAL_H = 56;
const ROW_GUTTER_W = 200;

export function ChartArea() {
  const containerRef = useRef<HTMLDivElement>(null);
  const svgRef = useRef<SVGSVGElement>(null);
  const [size, setSize] = useState({ width: 1000, height: 600 });
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
    const chartW = Math.max(200, size.width - ROW_GUTTER_W);
    fit(min, max, chartW);
    didInitialFit.current = true;
  }, [size.width, size.height, doc.tasks, doc.milestones, fit]);

  const chartWidth = Math.max(100, size.width - ROW_GUTTER_W);

  const layout = useMemo(
    () => layoutDocument({ doc, viewStart, pxPerDay }),
    [doc, viewStart, pxPerDay],
  );

  const axis = useMemo(
    () => buildAxis(viewStart, pxPerDay, chartWidth, doc.calendar.scale, doc.calendar.fiscalYearStart),
    [viewStart, pxPerDay, chartWidth, doc.calendar.scale, doc.calendar.fiscalYearStart],
  );

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

  const { handleSvgMouseDown, handleWheel, dragPreview } = useChartInteractions({
    svgRef,
    chartWidth,
    rowGutterWidth: ROW_GUTTER_W,
    axisHeight: AXIS_TOTAL_H,
  });

  return (
    <div ref={containerRef} className="chart-area" onMouseDown={(e) => { if (e.target === e.currentTarget) clear(); }}>
      <svg
        ref={svgRef}
        width={size.width}
        height={size.height}
        onMouseDown={handleSvgMouseDown}
        onWheel={handleWheel}
        style={{ display: 'block', cursor: dragPreview ? 'grabbing' : 'default', userSelect: 'none' }}
      >
        {/* Background */}
        <rect x={0} y={0} width={size.width} height={size.height} fill="var(--color-bg)" />

        {/* Row gutter background */}
        <rect x={0} y={0} width={ROW_GUTTER_W} height={size.height} fill="var(--color-bg-panel)" />
        <line x1={ROW_GUTTER_W} y1={0} x2={ROW_GUTTER_W} y2={size.height} stroke="var(--color-border-soft)" />

        {/* Weekend stripes (in chart area) */}
        <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H})`}>
          {weekendStripes.map((s, i) => (
            <rect key={i} x={s.x} y={0} width={s.w} height={layout.chartHeight} fill="var(--color-weekend)" opacity={0.4} />
          ))}
        </g>

        {/* Row separators */}
        <g transform={`translate(0 ${AXIS_TOTAL_H})`}>
          <Rows
            doc={doc}
            rowHeights={layout.rowHeights}
            rowOffsets={layout.rowOffsets}
            width={size.width}
          />
        </g>

        {/* Row gutter labels */}
        <g transform={`translate(0 ${AXIS_TOTAL_H})`}>
          <RowGutter doc={doc} rowHeights={layout.rowHeights} rowOffsets={layout.rowOffsets} width={ROW_GUTTER_W} />
        </g>

        {/* Time axis */}
        <g transform={`translate(${ROW_GUTTER_W} 0)`} clipPath="url(#chart-clip)">
          <TimeAxis axis={axis} />
        </g>

        {/* Chart content (clipped) */}
        <defs>
          <clipPath id="chart-clip">
            <rect x={0} y={0} width={chartWidth} height={size.height} />
          </clipPath>
        </defs>

        <g transform={`translate(${ROW_GUTTER_W} ${AXIS_TOTAL_H})`} clipPath="url(#chart-clip)">
          {/* Today line */}
          {showToday && (
            <line
              x1={todayX}
              y1={0}
              x2={todayX}
              y2={layout.chartHeight}
              stroke="var(--color-today)"
              strokeWidth={1.5}
              strokeDasharray="4 3"
            />
          )}

          <Brackets layout={layout} />
          <TaskBars layout={layout} dragPreview={dragPreview} />
          <Milestones layout={layout} />
          <Dependencies layout={layout} />
          <Markers doc={doc} viewStart={viewStart} pxPerDay={pxPerDay} chartHeight={layout.chartHeight} />
        </g>

        {/* Bottom border between axis and chart */}
        <line x1={ROW_GUTTER_W} y1={AXIS_TOTAL_H} x2={size.width} y2={AXIS_TOTAL_H} stroke="var(--color-border-soft)" />
      </svg>
    </div>
  );
}

export { dateToX, xToDate };
