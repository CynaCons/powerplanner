import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';

interface Props {
  layout: LayoutResult;
}

const SIZE = 11;

export function Milestones({ layout }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);

  return (
    <g>
      {layout.milestones.map((lm) => {
        const m = lm.milestone;
        const x = dateToX(m.date, viewStart, pxPerDay) + pxPerDay / 2;
        const y = lm.y;
        const selected = isSelected(m.id);
        const color = m.color || '#f59e0b';
        return (
          <g key={m.id}>
            <polygon
              data-pp-kind="milestone"
              data-pp-id={m.id}
              points={`${x},${y - SIZE} ${x + SIZE},${y} ${x},${y + SIZE} ${x - SIZE},${y}`}
              fill={color}
              stroke={selected ? 'var(--color-accent)' : 'rgba(0,0,0,0.3)'}
              strokeWidth={selected ? 2 : 1}
              style={{ cursor: 'grab' }}
            />
            <text
              x={x + SIZE + 4}
              y={y + 4}
              fontSize={11}
              fill="var(--color-text)"
              pointerEvents="none"
            >
              {m.label}
            </text>
          </g>
        );
      })}
    </g>
  );
}
