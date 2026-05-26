import type { Row } from '../types/document';
import { useDocumentStore } from '../stores/documentStore';

interface Props {
  rows: Row[];
  rowHeights: number[];
  rowOffsets: number[];
  width: number;
}

export function RowGutter({ rows, rowHeights, rowOffsets, width }: Props) {
  const updateRow = useDocumentStore((s) => s.updateRow);
  const allRows = useDocumentStore((s) => s.doc.rows);
  const childCount = new Map<string, number>();
  for (const r of allRows) {
    if (r.groupId) childCount.set(r.groupId, (childCount.get(r.groupId) ?? 0) + 1);
  }

  return (
    <g>
      {rows.map((row, i) => {
        const isGroup = (childCount.get(row.id) ?? 0) > 0;
        const indent = row.groupId ? 16 : 0;
        return (
          <g key={row.id} transform={`translate(0 ${rowOffsets[i]})`}>
            <rect x={0} y={0} width={width} height={rowHeights[i]} fill="transparent" />
            {isGroup && (
              <g
                onClick={() => updateRow(row.id, { collapsed: !row.collapsed })}
                style={{ cursor: 'pointer' }}
              >
                <rect x={4} y={rowHeights[i] / 2 - 8} width={16} height={16} fill="transparent" />
                <polygon
                  points={
                    row.collapsed
                      ? `${8},${rowHeights[i] / 2 - 4} ${14},${rowHeights[i] / 2} ${8},${rowHeights[i] / 2 + 4}`
                      : `${6},${rowHeights[i] / 2 - 3} ${14},${rowHeights[i] / 2 - 3} ${10},${rowHeights[i] / 2 + 4}`
                  }
                  fill="var(--color-text-soft)"
                />
              </g>
            )}
            <text
              x={12 + indent + (isGroup ? 12 : 0)}
              y={rowHeights[i] / 2 + 4}
              fill="var(--color-text)"
              fontSize={12}
              fontWeight={isGroup ? 600 : 500}
            >
              {row.label}
            </text>
          </g>
        );
      })}
    </g>
  );
}
