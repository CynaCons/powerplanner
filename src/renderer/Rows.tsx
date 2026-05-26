import type { Row } from '../types/document';

interface Props {
  rows: Row[];
  rowHeights: number[];
  rowOffsets: number[];
  width: number;
}

export function Rows({ rows, rowHeights, rowOffsets, width }: Props) {
  return (
    <g>
      {rows.map((row, i) => (
        <g key={row.id}>
          <line
            x1={0}
            y1={rowOffsets[i] + rowHeights[i]}
            x2={width}
            y2={rowOffsets[i] + rowHeights[i]}
            stroke="var(--color-border-soft)"
            strokeWidth={0.5}
          />
        </g>
      ))}
    </g>
  );
}
