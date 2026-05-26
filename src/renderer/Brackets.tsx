import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';

interface Props {
  layout: LayoutResult;
}

export function Brackets({ layout }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);

  return (
    <g>
      {layout.brackets.map((lb) => {
        const b = lb.bracket;
        const x = dateToX(b.start, viewStart, pxPerDay);
        const endX = dateToX(b.end, viewStart, pxPerDay) + pxPerDay;
        const w = Math.max(2, endX - x);
        const topRow = lb.topRow;
        const bottomRow = lb.bottomRow;
        const top = layout.rowOffsets[topRow] - 12;
        const bottom = layout.rowOffsets[bottomRow] + (layout.rowHeights[bottomRow] ?? 36) - 12;
        const selected = isSelected(b.id);
        const color = b.color || 'var(--color-accent)';

        return (
          <g key={b.id}>
            <line x1={x} y1={top} x2={x + w} y2={top} stroke={color} strokeWidth={1.5} />
            <line x1={x} y1={top} x2={x} y2={top + 6} stroke={color} strokeWidth={1.5} />
            <line x1={x + w} y1={top} x2={x + w} y2={top + 6} stroke={color} strokeWidth={1.5} />
            <line x1={x} y1={bottom} x2={x + w} y2={bottom} stroke={color} strokeWidth={1.5} opacity={0.4} />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              x={x}
              y={top - 4}
              width={w}
              height={8}
              fill="transparent"
              style={{ cursor: 'grab' }}
            />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              data-pp-sub="start"
              x={x - 4}
              y={top - 4}
              width={8}
              height={12}
              fill="transparent"
              style={{ cursor: 'ew-resize' }}
            />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              data-pp-sub="end"
              x={x + w - 4}
              y={top - 4}
              width={8}
              height={12}
              fill="transparent"
              style={{ cursor: 'ew-resize' }}
            />
            <text
              x={x + w / 2}
              y={top - 6}
              textAnchor="middle"
              fontSize={11}
              fontWeight={600}
              fill={color}
              pointerEvents="none"
            >
              {b.label}
            </text>
            {selected && (
              <rect
                x={x - 2}
                y={top - 6}
                width={w + 4}
                height={bottom - top + 10}
                fill="none"
                stroke="var(--color-accent)"
                strokeDasharray="3 3"
                pointerEvents="none"
              />
            )}
          </g>
        );
      })}
    </g>
  );
}
