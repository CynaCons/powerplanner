import { useMemo } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useViewStore } from '../stores/viewStore';
import { diffDays, addDays } from '../utils/dates';

interface Props {
  chartWidth: number;
  rowGutterWidth: number;
}

const HEIGHT = 28;
const MARGIN = 12;

/**
 * Compact overview rail above the bottom toolbar showing the entire
 * project's date range with a translucent viewport indicator.
 * Click-to-pan the viewport to that range.
 */
export function Minimap({ chartWidth, rowGutterWidth }: Props) {
  const doc = useDocumentStore((s) => s.doc);
  const show = useViewStore((s) => s.showMinimap);
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const setStart = useViewportStore((s) => s.setStart);

  const range = useMemo(() => {
    const dates = [
      ...doc.tasks.flatMap((t) => [t.start, t.end]),
      ...doc.milestones.map((m) => m.date),
    ];
    if (dates.length === 0) return null;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    return { min, max, days: diffDays(min, max) + 1 };
  }, [doc.tasks, doc.milestones]);

  if (!show || !range) return null;
  const trackWidth = chartWidth - MARGIN * 2;
  const dayPx = trackWidth / Math.max(1, range.days);

  const viewportDays = Math.ceil(chartWidth / pxPerDay);
  const viewportOffsetDays = diffDays(range.min, viewStart);
  const indicatorX = MARGIN + viewportOffsetDays * dayPx;
  const indicatorW = Math.max(8, viewportDays * dayPx);

  // Bars rendered as compact strokes
  const bars = doc.tasks.map((t) => {
    const x = MARGIN + diffDays(range.min, t.start) * dayPx;
    const w = Math.max(1, (diffDays(t.start, t.end) + 1) * dayPx);
    return { id: t.id, x, w, color: t.color || 'var(--color-accent)' };
  });

  const onMouseDown = (e: React.MouseEvent<SVGSVGElement>) => {
    const svg = e.currentTarget;
    const rect = svg.getBoundingClientRect();
    const clickX = e.clientX - rect.left - MARGIN;
    const dayOffset = Math.round(clickX / dayPx) - Math.floor(viewportDays / 2);
    setStart(addDays(range.min, Math.max(0, dayOffset)));
  };

  return (
    <div
      style={{
        position: 'absolute',
        bottom: 8,
        left: rowGutterWidth + 12,
        right: 12,
        height: HEIGHT,
        pointerEvents: 'auto',
        zIndex: 5,
      }}
    >
      <svg
        width="100%"
        height={HEIGHT}
        viewBox={`0 0 ${chartWidth} ${HEIGHT}`}
        preserveAspectRatio="none"
        onMouseDown={onMouseDown}
        style={{
          display: 'block',
          background: 'var(--color-bg-panel)',
          border: '1px solid var(--color-border-hairline)',
          borderRadius: 6,
          cursor: 'pointer',
        }}
      >
        {/* track */}
        <rect x={MARGIN} y={6} width={trackWidth} height={HEIGHT - 12} rx={2} fill="var(--color-bg-subtle)" />
        {/* bars */}
        {bars.map((b) => (
          <rect key={b.id} x={b.x} y={9} width={b.w} height={HEIGHT - 18} rx={1} fill={b.color} opacity={0.75} />
        ))}
        {/* viewport indicator */}
        <rect
          x={Math.max(MARGIN, indicatorX)}
          y={3}
          width={Math.min(trackWidth, indicatorW)}
          height={HEIGHT - 6}
          rx={3}
          fill="var(--color-accent)"
          fillOpacity={0.1}
          stroke="var(--color-accent)"
          strokeWidth={1.2}
          pointerEvents="none"
        />
      </svg>
    </div>
  );
}
