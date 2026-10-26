import type { AxisLayout } from '../layout/timeAxis';

interface Props {
  axis: AxisLayout;
}

/**
 * The time axis is the chart's strongest graphic element, deliberately.
 *
 * Every Gantt tool renders its axis as grey 10px text, which makes the
 * calendar feel like a caption on the bars. PowerGantt's whole claim is that
 * the chart is calendar-true, so the calendar is drawn as the structure the
 * bars hang from: the month is set in ink at reading size, it sits on a ruled
 * baseline that runs the full width, and the weeks drop from that rule as
 * measured ticks. Everything else in the chart stays quiet so this can carry.
 * (PLAN.md v2.16 — owner report 2026-10-23.)
 */
export function TimeAxis({ axis }: Props) {
  const majorH = axis.major.height;
  const minorH = axis.minor.height;
  const microH = axis.micro?.height ?? 0;

  return (
    <g>
      {/* Month band */}
      <rect x={0} y={0} width={10000} height={majorH} fill="var(--color-bg-panel)" />
      {axis.major.ticks.map((t, i) => (
        <g key={`maj-${i}`} transform={`translate(${t.x} 0)`}>
          {/* The month division is a short riser, not a full-height rule: the
              baseline below already carries the structure, and two competing
              rules read as a table. */}
          <line x1={0} y1={majorH - 14} x2={0} y2={majorH} stroke="var(--color-text)" strokeWidth={1} />
          <text x={8} y={majorH - 10} fill="var(--color-text)" fontSize={15} fontWeight={600}>
            {t.label}
          </text>
        </g>
      ))}
      {/* The ruled baseline. The one heavy line in the chart. */}
      <line x1={0} y1={majorH} x2={10000} y2={majorH} stroke="var(--color-text)" strokeWidth={1.5} />

      {/* Week band — ticks drop from the rule, labels sit clear of them */}
      <g transform={`translate(0 ${majorH})`}>
        <rect x={0} y={0} width={10000} height={minorH} fill="var(--color-bg)" />
        {axis.minor.ticks.map((t, i) => (
          <g key={`min-${i}`} transform={`translate(${t.x} 0)`}>
            <line x1={0} y1={0} x2={0} y2={6} stroke="var(--color-text-dim)" strokeWidth={1} />
            <text x={4} y={minorH - 5} fill="var(--color-text-soft)" fontSize={11}>
              {t.label}
            </text>
          </g>
        ))}
      </g>

      {/* Day numbers, when the scale is fine enough to show them */}
      {axis.micro && (
        <g transform={`translate(0 ${majorH + minorH})`}>
          <rect x={0} y={0} width={10000} height={microH} fill="var(--color-bg)" />
          {axis.micro.ticks.map((t, i) => (
            <text key={`mic-${i}`} x={t.x + 2} y={microH - 4} fill="var(--color-text-dim)" fontSize={10}>
              {t.label}
            </text>
          ))}
        </g>
      )}
    </g>
  );
}
