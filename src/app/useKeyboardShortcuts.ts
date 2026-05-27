import { useEffect } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import { useToolStore } from '../stores/toolStore';
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

      const docStore = useDocumentStore.getState();
      const { undo, redo, addTask, addMilestone, addBracket, deleteTask, deleteMilestone, deleteBracket, deleteDependency, deleteMarker, updateTask, updateMilestone, doc } = docStore;
      const sel = useSelectionStore.getState();
      const view = useViewportStore.getState();
      const edit = useEditStore.getState();
      const tool = useToolStore.getState();

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
          else if (item.kind === 'dependency') deleteDependency(item.id);
          else if (item.kind === 'marker') deleteMarker(item.id);
        }
        sel.clear();
        return;
      }
      if (e.key.toLowerCase() === 'b' && !meta) {
        e.preventDefault();
        if (doc.rows.length === 0) return;
        const taskIds = sel.items.filter((s) => s.kind === 'task').map((s) => s.id);
        const tasks = doc.tasks.filter((t) => taskIds.includes(t.id));
        if (tasks.length === 0) {
          const t = todayISO();
          addBracket({ id: newId('br'), label: 'Phase', start: t, end: addDays(t, 14), rowIds: [doc.rows[0].id] });
        } else {
          const start = tasks.reduce((a, t) => (t.start < a ? t.start : a), tasks[0].start);
          const end = tasks.reduce((a, t) => (t.end > a ? t.end : a), tasks[0].end);
          const rowIds = Array.from(new Set(tasks.map((t) => t.rowId)));
          addBracket({ id: newId('br'), label: 'Phase', start, end, rowIds });
        }
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
        edit.endEdit();
        if (tool.activeTool !== 'select') tool.setTool('select', true);
        return;
      }
      // Tool switching (Figma-style single-letter)
      if (!meta && !e.shiftKey) {
        if (e.key.toLowerCase() === 'v') {
          e.preventDefault();
          tool.setTool('select', true);
          return;
        }
        if (e.key.toLowerCase() === 't') {
          e.preventDefault();
          tool.setTool('add-task', true);
          return;
        }
        if (e.key.toLowerCase() === 'y') {
          e.preventDefault();
          tool.setTool('add-milestone', true);
          return;
        }
        if (e.key.toLowerCase() === 'h') {
          e.preventDefault();
          tool.setTool('pan', true);
          return;
        }
        if (e.key.toLowerCase() === 'r') {
          e.preventDefault();
          tool.setTool('marquee', true);
          return;
        }
      }
      // Linear-style scale shortcuts (Shift+letter to avoid clashing with tools)
      if (e.shiftKey && !meta) {
        const scaleKey = e.key.toLowerCase();
        if (scaleKey === 'd' || scaleKey === 'w' || scaleKey === 'm' || scaleKey === 'q' || scaleKey === 'y') {
          const scaleMap = { d: 'day', w: 'week', m: 'month', q: 'quarter', y: 'year' } as const;
          e.preventDefault();
          docStore.setScale(scaleMap[scaleKey as keyof typeof scaleMap]);
          return;
        }
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
      if (e.key === 'Home') {
        e.preventDefault();
        const dates = [
          ...doc.tasks.flatMap((t) => [t.start, t.end]),
          ...doc.milestones.map((m) => m.date),
        ];
        if (dates.length === 0) return;
        const min = dates.reduce((a, b) => (a < b ? a : b));
        const max = dates.reduce((a, b) => (a > b ? a : b));
        const chartArea = document.querySelector('.chart-area');
        const width = (chartArea?.clientWidth ?? 800) - 200;
        view.fit(min, max, Math.max(200, width));
        return;
      }
      if (e.key === 'F2' && sel.items.length === 1 && sel.items[0].kind === 'task') {
        e.preventDefault();
        edit.beginEdit(sel.items[0].id);
        return;
      }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
        if (sel.items.length === 0) return;
        e.preventDefault();
        const direction = e.key === 'ArrowLeft' ? -1 : 1;
        const step = e.shiftKey ? 7 : 1;
        const delta = direction * step;
        for (const item of sel.items) {
          if (item.kind === 'task') {
            const t = doc.tasks.find((x) => x.id === item.id);
            if (!t) continue;
            updateTask(t.id, { start: addDays(t.start, delta), end: addDays(t.end, delta) });
          } else if (item.kind === 'milestone') {
            const m = doc.milestones.find((x) => x.id === item.id);
            if (!m) continue;
            updateMilestone(m.id, { date: addDays(m.date, delta) });
          }
        }
        return;
      }
      if (e.key.toLowerCase() === 's' && !meta) {
        e.preventDefault();
        edit.setSnap(!edit.snapEnabled);
        return;
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);
}
