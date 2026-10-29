import { useMemo } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { Row } from '../types/document';

interface Props {
  rows: Row[];
  /** Kept for the caller's shape; separators key off offsets, not heights. */
  rowHeights?: number[];
  rowOffsets: number[];
  width: number;
}

/**
 * Row separators, drawn between GROUPS only.
 *
 * A rule under every row turns a long plan into ruled paper and competes with
 * the bars for attention. The grouping is the structure worth drawing, so the
 * rule lands above each group header and nowhere else — the gutter's indent
 * and weight carry the rest (owner report 2026-10-23).
 */
export function Rows({ rows, rowHeights, rowOffsets, width }: Props) {
  const allRows = useDocumentStore((s) => s.doc.rows);
  const hasChildren = new Set(allRows.filter((r) => r.groupId).map((r) => r.groupId as string));
  const doc = useDocumentStore((s) => s.doc);
  const items = useSelectionStore((s) => s.items);

  /**
   * Which rows the band belongs to.
   *
   * The owner asked for the whole row to light up when he picks something on
   * it, so a bar or a milestone bands its own row rather than only a row that
   * was clicked in the list.
   */
  const bandRows = useMemo(() => {
    const ids = new Set<string>();
    for (const it of items) {
      if (it.kind === 'row') ids.add(it.id);
      else if (it.kind === 'task') {
        const t = doc.tasks.find((x) => x.id === it.id);
        if (t) ids.add(t.rowId);
      } else if (it.kind === 'milestone') {
        const m = doc.milestones.find((x) => x.id === it.id);
        if (m) ids.add(m.rowId);
      }
    }
    return ids;
  }, [items, doc]);

  return (
    <g>
      {/*
        The selected row's band, drawn across the whole sheet (SRS-EDIT-024).

        It lived in the gutter alone, so at any useful zoom the tint stopped at
        the row labels and the eye had to carry a bar back across an untinted
        plot to know whose row it was. This layer is inside `chart-body-clip`,
        which spans the gutter and the plot together, and it draws under the
        bars because the plot group comes after it.
      */}
      {rows.map((row, i) =>
        bandRows.has(row.id) ? (
          <g key={`band-${row.id}`} data-pp-rowband="1" pointerEvents="none">
            <rect
              x={0}
              y={rowOffsets[i]}
              width={width}
              height={rowHeights?.[i] ?? 0}
              fill="var(--color-accent)"
              opacity={0.07}
            />
            <rect
              x={0}
              y={rowOffsets[i]}
              width={3}
              height={rowHeights?.[i] ?? 0}
              fill="var(--color-accent)"
            />
          </g>
        ) : null,
      )}

      {rows.map((row, i) => {
        // A group header opens a band: rule above it, except at the very top
        // where the axis baseline already closes the header off.
        const isGroup = hasChildren.has(row.id);
        if (!isGroup || i === 0) return null;
        return (
          <line
            key={row.id}
            x1={0}
            y1={rowOffsets[i]}
            x2={width}
            y2={rowOffsets[i]}
            stroke="var(--color-border-soft)"
            strokeWidth={1}
          />
        );
      })}
    </g>
  );
}
