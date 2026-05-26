import { useEffect } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewportStore } from '../stores/viewportStore';
import { addDays, todayISO } from '../utils/dates';
import { newId } from '../utils/ids';

function isEditableTarget(t: EventTarget | null): boolean {
  if (!(t instanceof HTMLElement)) return false;
  const tag = t.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || t.isContentEditable;
}

export function useKeyboardShortcuts() {
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (isEditableTarget(e.target)) return;

      const { undo, redo, addTask, addMilestone, deleteTask, deleteMilestone, deleteBracket, doc } = useDocumentStore.getState();
      const sel = useSelectionStore.getState();
      const view = useViewportStore.getState();

      const meta = e.metaKey || e.ctrlKey;
      if (meta && e.key.toLowerCase() === 'z' && !e.shiftKey) {
        e.preventDefault();
        undo();
        return;
      }
      if (meta && (e.key.toLowerCase() === 'y' || (e.key.toLowerCase() === 'z' && e.shiftKey))) {
        e.preventDefault();
        redo();
        return;
      }
      if (e.key === 'Delete' || e.key === 'Backspace') {
        if (sel.items.length === 0) return;
        e.preventDefault();
        for (const item of sel.items) {
          if (item.kind === 'task') deleteTask(item.id);
          else if (item.kind === 'milestone') deleteMilestone(item.id);
          else if (item.kind === 'bracket') deleteBracket(item.id);
        }
        sel.clear();
        return;
      }
      if (e.key.toLowerCase() === 'n') {
        e.preventDefault();
        if (doc.rows.length === 0) return;
        const today = todayISO();
        addTask({
          id: newId('task'),
          rowId: doc.rows[0].id,
          label: 'New task',
          start: today,
          end: addDays(today, 7),
          percentComplete: 0,
        });
        return;
      }
      if (e.key.toLowerCase() === 'm') {
        e.preventDefault();
        if (doc.rows.length === 0) return;
        addMilestone({
          id: newId('ms'),
          rowId: doc.rows[0].id,
          label: 'New milestone',
          date: todayISO(),
        });
        return;
      }
      if (e.key === 'Escape') {
        sel.clear();
        return;
      }
      if (e.key === '+' || e.key === '=') {
        e.preventDefault();
        view.zoom(1.25, 400);
        return;
      }
      if (e.key === '-') {
        e.preventDefault();
        view.zoom(0.8, 400);
        return;
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);
}
