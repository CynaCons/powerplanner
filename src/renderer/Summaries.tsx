import type { LayoutResult } from '../layout/engine';

interface Props {
  layout: LayoutResult;
}

export function Summaries({ layout }: Props) {
  return (
    <g>
      {layout.summaries.map((s) => (
        <g key={s.rowId}>
          <rect x={s.x} y={s.y} width={s.width} height={6} fill="var(--color-text-soft)" opacity={0.6} rx={1} />
          <polygon
            points={`${s.x},${s.y + 6} ${s.x + 6},${s.y + 6} ${s.x},${s.y + 12}`}
            fill="var(--color-text-soft)"
            opacity={0.6}
          />
          <polygon
            points={`${s.x + s.width},${s.y + 6} ${s.x + s.width - 6},${s.y + 6} ${s.x + s.width},${s.y + 12}`}
            fill="var(--color-text-soft)"
            opacity={0.6}
          />
        </g>
      ))}
    </g>
  );
}
