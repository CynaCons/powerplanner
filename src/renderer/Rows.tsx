import { useDocumentStore } from '../stores/documentStore';
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
export function Rows({ rows, rowOffsets, width }: Props) {
  const allRows = useDocumentStore((s) => s.doc.rows);
  const hasChildren = new Set(allRows.filter((r) => r.groupId).map((r) => r.groupId as string));

  return (
    <g>
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
