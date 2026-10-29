import type { GanttDocument } from '../types/document';
import type { Selectable } from '../stores/selectionStore';

/**
 * Where an insert should land (SRS-EDIT-028).
 *
 * Both the insert buttons and the insert keys passed `doc.rows[0]`. In every
 * shipped template that is a section header, and it is usually scrolled off
 * the top, so inserting a task appeared to do nothing — or, worse, hung work
 * off a heading. The owner hit this immediately on a document he had just
 * created: "I think they've all been created at the row of the section, which
 * is a bit weird."
 *
 * A section is a heading, not a place work goes, so selecting one and
 * inserting makes a row beneath it and puts the work there. That is the one
 * answer that is never wrong: it cannot fail, and it cannot silently attach a
 * task to a title.
 */
export type InsertTarget =
  | { kind: 'row'; rowId: string }
  | { kind: 'newChild'; parentId: string; index: number }
  | null;

/** Rows that other rows name as their parent. */
export function groupRowIds(doc: GanttDocument): Set<string> {
  return new Set(doc.rows.filter((r) => r.groupId).map((r) => r.groupId as string));
}

export function resolveInsertTarget(doc: GanttDocument, items: Selectable[]): InsertTarget {
  if (doc.rows.length === 0) return null;
  const groups = groupRowIds(doc);

  const rowOf = (it: Selectable): string | null => {
    if (it.kind === 'row') return it.id;
    if (it.kind === 'task') return doc.tasks.find((t) => t.id === it.id)?.rowId ?? null;
    if (it.kind === 'milestone') return doc.milestones.find((m) => m.id === it.id)?.rowId ?? null;
    return null;
  };

  for (const it of items) {
    const rowId = rowOf(it);
    if (!rowId) continue;
    if (!groups.has(rowId)) return { kind: 'row', rowId };
    // A section was picked: the work goes on a row of its own, directly under it.
    return { kind: 'newChild', parentId: rowId, index: doc.rows.findIndex((r) => r.id === rowId) + 1 };
  }

  // Nothing selected. Prefer the first row that is not a heading.
  const plain = doc.rows.find((r) => !groups.has(r.id));
  if (plain) return { kind: 'row', rowId: plain.id };
  return { kind: 'newChild', parentId: doc.rows[0].id, index: 1 };
}
