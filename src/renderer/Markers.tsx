import type { GanttDocument } from '../types/document';
import { dateToX } from '../layout/timeAxis';

interface Props {
  doc: GanttDocument;
  viewStart: string;
  pxPerDay: number;
  chartHeight: number;
}

export function Markers({ doc, viewStart, pxPerDay, chartHeight }: Props) {
  return (
    <g>
      {doc.markers.map((m) => {
        const x = dateToX(m.date, viewStart, pxPerDay);
        const color = m.color || (m.type === 'deadline' ? 'var(--color-deadline)' : 'var(--color-text-soft)');
        return (
          <g key={m.id}>
            <line
              x1={x}
              y1={0}
              x2={x}
              y2={chartHeight}
              stroke={color}
              strokeWidth={1.2}
              strokeDasharray={m.type === 'deadline' ? '6 4' : '2 2'}
              opacity={0.85}
            />
            <text x={x + 4} y={12} fontSize={10} fill={color}>
              {m.label}
            </text>
          </g>
        );
      })}
    </g>
  );
}
