import type { GanttDocument } from '../types/document';

interface Props {
  doc: GanttDocument;
  rowHeights: number[];
  rowOffsets: number[];
  width: number;
}

export function RowGutter({ doc, rowHeights, rowOffsets, width }: Props) {
  return (
    <g>
      {doc.rows.map((row, i) => (
        <g key={row.id} transform={`translate(0 ${rowOffsets[i]})`}>
          <rect x={0} y={0} width={width} height={rowHeights[i]} fill="transparent" />
          <text x={12} y={rowHeights[i] / 2 + 4} fill="var(--color-text)" fontSize={12} fontWeight={500}>
            {row.label}
          </text>
        </g>
      ))}
    </g>
  );
}
