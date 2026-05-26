import type { AxisLayout } from '../layout/timeAxis';

interface Props {
  axis: AxisLayout;
}

export function TimeAxis({ axis }: Props) {
  const majorH = axis.major.height;
  const minorH = axis.minor.height;
  const microH = axis.micro?.height ?? 0;

  return (
    <g>
      {/* Major level */}
      <rect x={0} y={0} width={10000} height={majorH} fill="var(--color-bg-panel)" />
      {axis.major.ticks.map((t, i) => (
        <g key={`maj-${i}`} transform={`translate(${t.x} 0)`}>
          <line x1={0} y1={0} x2={0} y2={majorH} stroke="var(--color-border-soft)" />
          <text x={6} y={majorH - 8} fill="var(--color-text-soft)" fontSize={11} fontWeight={600}>
            {t.label}
          </text>
        </g>
      ))}
      {/* Minor level */}
      <g transform={`translate(0 ${majorH})`}>
        <rect x={0} y={0} width={10000} height={minorH} fill="var(--color-bg)" />
        {axis.minor.ticks.map((t, i) => (
          <g key={`min-${i}`} transform={`translate(${t.x} 0)`}>
            <line x1={0} y1={0} x2={0} y2={minorH} stroke="var(--color-border-soft)" />
            <text x={4} y={minorH - 6} fill="var(--color-text-dim)" fontSize={10}>
              {t.label}
            </text>
          </g>
        ))}
      </g>
      {/* Micro level (day numbers) */}
      {axis.micro && (
        <g transform={`translate(0 ${majorH + minorH})`}>
          <rect x={0} y={0} width={10000} height={microH} fill="var(--color-bg)" />
          {axis.micro.ticks.map((t, i) => (
            <text key={`mic-${i}`} x={t.x + 2} y={microH - 4} fill="var(--color-text-dim)" fontSize={9}>
              {t.label}
            </text>
          ))}
        </g>
      )}
    </g>
  );
}
