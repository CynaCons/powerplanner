/**
 * Embeddable, read-only Gantt renderer.
 *
 * Self-contained: depends ONLY on the layout engine + types. No zustand
 * stores, no DOM event handlers, no interactions. Designed to be mounted
 * inside any host React app (e.g. PowerNote, a docs site, an export
 * preview) by passing a GanttDocument + container dimensions.
 *
 * Reuses layoutDocument() for the maths so embedded charts stay pixel-
 * identical to the main editor.
 */

import type { CSSProperties } from 'react';
import type { GanttDocument } from '../types/document';
import { layoutDocument } from '../layout/engine';
import { buildAxis, dateToX } from '../layout/timeAxis';
import { computeCriticalPath } from '../layout/criticalPath';
import { diffDays, addDays, todayISO, isWeekend } from '../utils/dates';

export interface GanttRendererOptions {
  /** Show the red glow on critical-path tasks (default false). */
  showCriticalPath?: boolean;
  /** Show the today line. Default true. */
  showToday?: boolean;
  /** Show weekend stripes (day/week scales only). Default true. */
  showWeekends?: boolean;
  /** Pixel width of the row gutter on the left. Default scales with width. */
  rowGutterWidth?: number;
  /** Override the document theme — defaults to whatever `data-theme` the host sets. */
  themeOverride?: 'dark' | 'light' | 'print';
  /** Optional inline style applied to the root SVG. */
  style?: CSSProperties;
  /** Optional CSS class applied to the root SVG. */
  className?: string;
}

export interface GanttRendererProps {
  document: GanttDocument;
  width: number;
  height: number;
  options?: GanttRendererOptions;
}

const AXIS_H = 56;
const HEADER_PADDING = 8;

export function GanttRenderer({ document, width, height, options = {} }: GanttRendererProps) {
  const {
    showCriticalPath = false,
    showToday = true,
    showWeekends = true,
    rowGutterWidth,
    themeOverride,
    style,
    className,
  } = options;

  // Responsive row gutter: ~22% of width, clamped.
  const gutterW = rowGutterWidth ?? Math.max(80, Math.min(180, Math.round(width * 0.22)));
  const chartW = Math.max(60, width - gutterW);

  // Auto-fit: derive viewStart + pxPerDay from doc date range.
  const allDates = [
    ...document.tasks.flatMap((t) => [t.start, t.end]),
    ...document.milestones.map((m) => m.date),
  ];
  let viewStart: string;
  let pxPerDay: number;
  if (allDates.length === 0) {
    viewStart = todayISO();
    pxPerDay = chartW / 60;
  } else {
    const min = allDates.reduce((a, b) => (a < b ? a : b));
    const max = allDates.reduce((a, b) => (a > b ? a : b));
    const days = Math.max(1, diffDays(min, max) + 1);
    const padding = Math.max(1, Math.ceil(days * 0.06));
    pxPerDay = chartW / (days + padding * 2);
    pxPerDay = Math.max(0.05, Math.min(120, pxPerDay));
    viewStart = addDays(min, -padding);
  }

  const layout = layoutDocument({ doc: document, viewStart, pxPerDay });
  const axis = buildAxis(
    viewStart,
    pxPerDay,
    chartW,
    document.calendar.scale,
    document.calendar.fiscalYearStart,
  );

  const criticalIds = showCriticalPath ? computeCriticalPath(document).criticalTaskIds : null;

  // Weekend stripes
  const weekendStripes: { x: number; w: number }[] = [];
  if (showWeekends && (document.calendar.scale === 'day' || document.calendar.scale === 'week')) {
    const days = Math.ceil(chartW / pxPerDay) + 1;
    for (let i = 0; i < days; i++) {
      const iso = addDays(viewStart, i);
      if (isWeekend(iso, document.calendar.workingDays)) {
        weekendStripes.push({ x: i * pxPerDay, w: pxPerDay });
      }
    }
  }

  const today = todayISO();
  const todayX = dateToX(today, viewStart, pxPerDay);
  const todayVisible = showToday && todayX >= 0 && todayX <= chartW;

  // Theme isolation: wrap in a div with data-theme so host's theme doesn't bleed.
  const rootStyle: CSSProperties = {
    width: '100%',
    height: '100%',
    display: 'block',
    background: 'var(--pp-bg, #0b0c0f)',
    color: 'var(--pp-text, #f0f1f3)',
    fontFamily: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif',
    ...style,
  };

  return (
    <svg
      width={width}
      height={height}
      viewBox={`0 0 ${width} ${height}`}
      preserveAspectRatio="xMidYMid meet"
      className={className}
      style={rootStyle}
      data-theme={themeOverride}
      role="img"
      aria-label={`Gantt chart: ${document.title}`}
    >
      <defs>
        <linearGradient id="pp-embed-grad" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#7c83ff" />
          <stop offset="55%" stopColor="#a78bfa" />
          <stop offset="100%" stopColor="#fb7185" />
        </linearGradient>
        <clipPath id={`pp-clip-${document.title.replace(/\W/g, '')}`}>
          <rect x={gutterW} y={AXIS_H} width={chartW} height={Math.max(0, height - AXIS_H)} />
        </clipPath>
      </defs>

      {/* Window background */}
      <rect x={0} y={0} width={width} height={height} fill="var(--pp-bg, #0b0c0f)" />

      {/* Gutter background */}
      <rect x={0} y={0} width={gutterW} height={height} fill="var(--pp-bg-panel, #101216)" />
      <line x1={gutterW} y1={0} x2={gutterW} y2={height} stroke="var(--pp-border-hair, rgba(255,255,255,0.06))" />

      {/* Time axis — major + minor levels */}
      <g transform={`translate(${gutterW} 0)`}>
        <rect x={0} y={0} width={chartW} height={axis.major.height} fill="var(--pp-bg-panel, #101216)" />
        {axis.major.ticks.map((t, i) => (
          <g key={`maj-${i}`} transform={`translate(${t.x} 0)`}>
            <line x1={0} y1={0} x2={0} y2={axis.major.height} stroke="var(--pp-border-hair, rgba(255,255,255,0.06))" />
            <text x={6} y={axis.major.height - 8} fill="var(--pp-text-soft, #a1a5ad)" fontSize={10} fontWeight={600}>
              {t.label}
            </text>
          </g>
        ))}
        <g transform={`translate(0 ${axis.major.height})`}>
          <rect x={0} y={0} width={chartW} height={axis.minor.height} fill="var(--pp-bg, #0b0c0f)" />
          {axis.minor.ticks.map((t, i) => (
            <g key={`min-${i}`} transform={`translate(${t.x} 0)`}>
              <line x1={0} y1={0} x2={0} y2={axis.minor.height} stroke="var(--pp-border-hair, rgba(255,255,255,0.06))" />
              <text x={4} y={axis.minor.height - 6} fill="var(--pp-text-dim, #6b7079)" fontSize={9}>
                {t.label}
              </text>
            </g>
          ))}
        </g>
      </g>

      {/* Row gutter labels */}
      <g transform={`translate(0 ${AXIS_H})`}>
        {layout.visibleRows.map((row, i) => (
          <g key={row.id} transform={`translate(0 ${layout.rowOffsets[i]})`}>
            <text
              x={12 + (row.groupId ? 16 : 0)}
              y={layout.rowHeights[i] / 2 + 4}
              fill="var(--pp-text, #f0f1f3)"
              fontSize={11}
              fontWeight={500}
            >
              {row.label}
            </text>
            <line
              x1={0}
              y1={layout.rowHeights[i]}
              x2={width}
              y2={layout.rowHeights[i]}
              stroke="var(--pp-border-hair, rgba(255,255,255,0.06))"
              strokeWidth={0.5}
            />
          </g>
        ))}
      </g>

      {/* Chart content (clipped) */}
      <g transform={`translate(${gutterW} ${AXIS_H})`} clipPath={`url(#pp-clip-${document.title.replace(/\W/g, '')})`}>
        {/* Weekend stripes */}
        {weekendStripes.map((s, i) => (
          <rect key={`w-${i}`} x={s.x} y={0} width={s.w} height={layout.chartHeight} fill="var(--pp-weekend, rgba(255,255,255,0.025))" />
        ))}

        {/* Today line */}
        {todayVisible && (
          <g pointerEvents="none">
            <line x1={todayX} y1={0} x2={todayX} y2={layout.chartHeight} stroke="var(--pp-today, #f87171)" strokeWidth={1.2} />
          </g>
        )}

        {/* Markers */}
        {document.markers.map((m) => {
          const x = dateToX(m.date, viewStart, pxPerDay);
          if (x < -20 || x > chartW + 20) return null;
          return (
            <g key={m.id} pointerEvents="none">
              <line
                x1={x}
                y1={0}
                x2={x}
                y2={layout.chartHeight}
                stroke={m.type === 'deadline' ? 'var(--pp-deadline, #fbbf24)' : 'var(--pp-text-soft, #a1a5ad)'}
                strokeWidth={1}
                strokeDasharray={m.type === 'deadline' ? '5 3' : '2 2'}
                opacity={0.8}
              />
              <text
                x={x + 4}
                y={10}
                fontSize={9}
                fill={m.type === 'deadline' ? 'var(--pp-deadline, #fbbf24)' : 'var(--pp-text-soft, #a1a5ad)'}
              >
                {m.label}
              </text>
            </g>
          );
        })}

        {/* Brackets */}
        {layout.brackets.map((lb) => {
          const top = layout.rowOffsets[lb.topRow] - 10;
          const w = Math.max(2, lb.width);
          const color = lb.bracket.color || 'var(--pp-accent, #818cf8)';
          return (
            <g key={lb.bracket.id} pointerEvents="none">
              <line x1={lb.x} y1={top} x2={lb.x + w} y2={top} stroke={color} strokeWidth={1.2} />
              <line x1={lb.x} y1={top} x2={lb.x} y2={top + 5} stroke={color} strokeWidth={1.2} />
              <line x1={lb.x + w} y1={top} x2={lb.x + w} y2={top + 5} stroke={color} strokeWidth={1.2} />
              <text x={lb.x + w / 2} y={top - 4} textAnchor="middle" fontSize={9.5} fontWeight={600} fill={color}>
                {lb.bracket.label}
              </text>
            </g>
          );
        })}

        {/* Summary rows */}
        {layout.summaries.map((s) => (
          <g key={s.rowId} pointerEvents="none">
            <rect x={s.x} y={s.y} width={s.width} height={5} rx={1.5} fill="var(--pp-text-soft, #a1a5ad)" opacity={0.55} />
          </g>
        ))}

        {/* Task bars */}
        {layout.tasks.map((lt) => {
          const color = lt.task.color || 'var(--pp-accent, #818cf8)';
          const pct = Math.max(0, Math.min(100, lt.task.percentComplete ?? 0));
          const fillW = (lt.width * pct) / 100;
          const isCritical = criticalIds?.has(lt.task.id);
          return (
            <g key={lt.task.id} pointerEvents="none">
              {isCritical && (
                <rect
                  x={lt.x - 2}
                  y={lt.y - 2}
                  width={lt.width + 4}
                  height={lt.height + 4}
                  rx={5}
                  fill="none"
                  stroke="var(--pp-today, #f87171)"
                  strokeWidth={1.2}
                  opacity={0.7}
                />
              )}
              <rect x={lt.x} y={lt.y} width={lt.width} height={lt.height} rx={3} fill={color} opacity={0.92} />
              {pct > 0 && (
                <rect x={lt.x} y={lt.y + lt.height - 3} width={fillW} height={3} fill="rgba(0,0,0,0.45)" />
              )}
              {lt.width > 60 && (
                <text x={lt.x + 6} y={lt.y + lt.height / 2 + 3.5} fontSize={10} fill="white" fontWeight={500}>
                  {truncate(lt.task.label, Math.floor(lt.width / 6.5) - 1)}
                </text>
              )}
            </g>
          );
        })}

        {/* Milestones */}
        {layout.milestones.map((lm) => {
          const cx = dateToX(lm.milestone.date, viewStart, pxPerDay) + pxPerDay / 2;
          const cy = lm.y;
          const sz = 8;
          const color = lm.milestone.color || '#fbbf24';
          return (
            <g key={lm.milestone.id} pointerEvents="none">
              <polygon
                points={`${cx},${cy - sz} ${cx + sz},${cy} ${cx},${cy + sz} ${cx - sz},${cy}`}
                fill={color}
                stroke="rgba(0,0,0,0.3)"
                strokeWidth={1}
              />
            </g>
          );
        })}

        {/* Dependencies */}
        {layout.dependencies.map((ld) => (
          <path
            key={ld.dependency.id}
            d={ld.path}
            fill="none"
            stroke="var(--pp-text-soft, #a1a5ad)"
            strokeWidth={1}
            opacity={0.7}
            pointerEvents="none"
          />
        ))}
      </g>

      {/* Bottom border between axis and chart */}
      <line x1={gutterW} y1={AXIS_H} x2={width} y2={AXIS_H} stroke="var(--pp-border-hair, rgba(255,255,255,0.06))" />

      <style>{`
        @media (max-width: 999999px) {
          svg[data-theme='light'] {
            --pp-bg: #fafbfc;
            --pp-bg-panel: #ffffff;
            --pp-text: #0b0c0f;
            --pp-text-soft: #5b6068;
            --pp-text-dim: #8b8f97;
            --pp-border-hair: rgba(0, 0, 0, 0.06);
            --pp-weekend: rgba(0, 0, 0, 0.025);
          }
        }
      `}</style>

      {/* Padding placeholder so height>0 even when no rows */}
      {layout.visibleRows.length === 0 && (
        <text x={width / 2} y={height / 2} textAnchor="middle" fontSize={11} fill="var(--pp-text-dim, #6b7079)">
          {HEADER_PADDING}empty chart
        </text>
      )}
    </svg>
  );
}

function truncate(s: string, n: number): string {
  if (n <= 0) return '';
  if (s.length <= n) return s;
  return s.slice(0, Math.max(1, n - 1)) + '…';
}
