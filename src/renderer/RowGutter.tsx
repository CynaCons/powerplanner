import { useMemo } from 'react';
import type { Row } from '../types/document';
import { useDocumentStore } from '../stores/documentStore';

interface Props {
  rows: Row[];
  rowHeights: number[];
  rowOffsets: number[];
  width: number;
}

/**
 * The row gutter reads as a hierarchy, not a list.
 *
 * It used to render every row at the same size with the same hairline under
 * it, so a 35-row programme arrived as 35 equal lines and the eye had nothing
 * to hold onto (owner report 2026-10-23: "the left side is a bit meh, not so
 * clear"). Now a group carries a colour spine in its own stream colour and its
 * packages sit indented and lighter beneath it.
 *
 * The spine colour is taken from the group's own tasks rather than stored on
 * the row, so it can never disagree with the bars it labels.
 */
export function RowGutter({ rows, rowHeights, rowOffsets, width }: Props) {
  const updateRow = useDocumentStore((s) => s.updateRow);
  const allRows = useDocumentStore((s) => s.doc.rows);
  const tasks = useDocumentStore((s) => s.doc.tasks);

  const { childCount, groupColor } = useMemo(() => {
    const counts = new Map<string, number>();
    for (const r of allRows) {
      if (r.groupId) counts.set(r.groupId, (counts.get(r.groupId) ?? 0) + 1);
    }
    // rowId -> its group's id, so a task can colour the group it belongs to.
    const parentOf = new Map<string, string>();
    for (const r of allRows) if (r.groupId) parentOf.set(r.id, r.groupId);

    const colors = new Map<string, string>();
    for (const t of tasks) {
      const parent = parentOf.get(t.rowId);
      if (parent && t.color && !colors.has(parent)) colors.set(parent, t.color);
    }
    return { childCount: counts, groupColor: colors };
  }, [allRows, tasks]);

  return (
    <g>
      {rows.map((row, i) => {
        const isGroup = (childCount.get(row.id) ?? 0) > 0;
        const h = rowHeights[i];
        const spine = groupColor.get(row.id) ?? 'var(--color-text-soft)';

        return (
          // The data attributes live on the GROUP, not on the backing rect:
          // the right-click handler resolves its target with `closest()`, which
          // walks ancestors, and the label text sits as a sibling of the rect
          // rather than inside it.
          <g
            key={row.id}
            data-pp-kind="row"
            data-pp-id={row.id}
            transform={`translate(0 ${rowOffsets[i]})`}
          >
            <rect x={0} y={0} width={width} height={h} fill="transparent" />

            {isGroup ? (
              <>
                <rect x={10} y={h / 2 - 7} width={3} height={14} fill={spine} />
                <g
                  onClick={() => updateRow(row.id, { collapsed: !row.collapsed })}
                  style={{ cursor: 'pointer' }}
                >
                  <rect x={18} y={h / 2 - 8} width={16} height={16} fill="transparent" />
                  <polygon
                    points={
                      row.collapsed
                        ? `${23},${h / 2 - 4} ${29},${h / 2} ${23},${h / 2 + 4}`
                        : `${21},${h / 2 - 3} ${31},${h / 2 - 3} ${26},${h / 2 + 4}`
                    }
                    fill="var(--color-text-dim)"
                  />
                </g>
                <text x={38} y={h / 2 + 4} fill="var(--color-text)" fontSize={12} fontWeight={600}>
                  {row.label}
                </text>
              </>
            ) : (
              <text
                x={row.groupId ? 30 : 12}
                y={h / 2 + 4}
                fill="var(--color-text-soft)"
                fontSize={11}
              >
                {row.label}
              </text>
            )}
          </g>
        );
      })}
    </g>
  );
}
