import { useRef, useState, useCallback } from 'react';
import type { RefObject, MouseEvent as ReactMouseEvent, WheelEvent as ReactWheelEvent } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { xToDate } from '../layout/timeAxis';
import { addDays, diffDays } from '../utils/dates';

interface InteractionOptions {
  svgRef: RefObject<SVGSVGElement | null>;
  chartWidth: number;
  rowGutterWidth: number;
  axisHeight: number;
}

export type DragKind = 'pan' | 'task-move' | 'task-resize-start' | 'task-resize-end' | 'milestone-move' | 'bracket-move' | 'bracket-resize-start' | 'bracket-resize-end';

export interface DragState {
  kind: DragKind;
  targetId?: string;
  startClientX: number;
  startClientY: number;
  originalStart?: string;
  originalEnd?: string;
  originalDate?: string;
  startViewDate?: string;
  deltaDays: number;
}

export interface DragPreviewMap {
  tasks: Map<string, { start: string; end: string }>;
  milestones: Map<string, string>;
  brackets: Map<string, { start: string; end: string }>;
}

export function useChartInteractions({ svgRef, rowGutterWidth }: InteractionOptions) {
  const dragRef = useRef<DragState | null>(null);
  const [dragPreview, setDragPreview] = useState<DragPreviewMap | null>(null);

  const getSvgPoint = useCallback(
    (clientX: number, clientY: number): { x: number; y: number } => {
      const svg = svgRef.current;
      if (!svg) return { x: 0, y: 0 };
      const rect = svg.getBoundingClientRect();
      return { x: clientX - rect.left, y: clientY - rect.top };
    },
    [svgRef],
  );

  const handleWheel = useCallback(
    (e: ReactWheelEvent<SVGSVGElement>) => {
      if (e.ctrlKey || e.metaKey) {
        e.preventDefault();
        const { x } = getSvgPoint(e.clientX, e.clientY);
        const anchorX = Math.max(0, x - rowGutterWidth);
        const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
        useViewportStore.getState().zoom(factor, anchorX);
      } else if (e.shiftKey) {
        e.preventDefault();
        useViewportStore.getState().pan(-e.deltaY);
      } else {
        e.preventDefault();
        useViewportStore.getState().pan(-e.deltaX || -e.deltaY);
      }
    },
    [getSvgPoint, rowGutterWidth],
  );

  const handleSvgMouseDown = useCallback(
    (e: ReactMouseEvent<SVGSVGElement>) => {
      if (e.button !== 0) return;
      const target = e.target as Element;
      const datasetEl = target.closest('[data-pp-kind]') as HTMLElement | null;
      const view = useViewportStore.getState();
      const doc = useDocumentStore.getState().doc;

      const { x } = getSvgPoint(e.clientX, e.clientY);
      // If clicking outside chart area (in row gutter) — ignore
      if (!datasetEl && x < rowGutterWidth) return;

      if (!datasetEl) {
        // Pan
        dragRef.current = {
          kind: 'pan',
          startClientX: e.clientX,
          startClientY: e.clientY,
          startViewDate: view.startDate,
          deltaDays: 0,
        };
        attachWindowListeners(handleWindowMove, handleWindowUp);
        return;
      }

      const kind = datasetEl.dataset.ppKind as string;
      const id = datasetEl.dataset.ppId as string;
      const sub = datasetEl.dataset.ppSub as string | undefined;

      // Selection on click
      const selStore = useSelectionStore.getState();
      const additive = e.shiftKey || e.metaKey || e.ctrlKey;
      if (kind === 'task' || kind === 'milestone' || kind === 'bracket') {
        if (!additive) {
          selStore.setSelection([{ kind: kind as 'task' | 'milestone' | 'bracket', id }]);
        } else if (!selStore.isSelected(id)) {
          selStore.add({ kind: kind as 'task' | 'milestone' | 'bracket', id });
        }
      }

      // Start drag
      if (kind === 'task') {
        const task = doc.tasks.find((t) => t.id === id);
        if (!task) return;
        if (sub === 'start') {
          dragRef.current = {
            kind: 'task-resize-start',
            targetId: id,
            startClientX: e.clientX,
            startClientY: e.clientY,
            originalStart: task.start,
            originalEnd: task.end,
            deltaDays: 0,
          };
        } else if (sub === 'end') {
          dragRef.current = {
            kind: 'task-resize-end',
            targetId: id,
            startClientX: e.clientX,
            startClientY: e.clientY,
            originalStart: task.start,
            originalEnd: task.end,
            deltaDays: 0,
          };
        } else {
          dragRef.current = {
            kind: 'task-move',
            targetId: id,
            startClientX: e.clientX,
            startClientY: e.clientY,
            originalStart: task.start,
            originalEnd: task.end,
            deltaDays: 0,
          };
        }
        attachWindowListeners(handleWindowMove, handleWindowUp);
        return;
      }

      if (kind === 'milestone') {
        const m = doc.milestones.find((x) => x.id === id);
        if (!m) return;
        dragRef.current = {
          kind: 'milestone-move',
          targetId: id,
          startClientX: e.clientX,
          startClientY: e.clientY,
          originalDate: m.date,
          deltaDays: 0,
        };
        attachWindowListeners(handleWindowMove, handleWindowUp);
        return;
      }

      if (kind === 'bracket') {
        const b = doc.brackets.find((x) => x.id === id);
        if (!b) return;
        const k =
          sub === 'start' ? 'bracket-resize-start' : sub === 'end' ? 'bracket-resize-end' : 'bracket-move';
        dragRef.current = {
          kind: k as DragKind,
          targetId: id,
          startClientX: e.clientX,
          startClientY: e.clientY,
          originalStart: b.start,
          originalEnd: b.end,
          deltaDays: 0,
        };
        attachWindowListeners(handleWindowMove, handleWindowUp);
        return;
      }
    },
    [getSvgPoint, rowGutterWidth],
  );

  const handleWindowMove = useCallback((e: globalThis.MouseEvent) => {
    const state = dragRef.current;
    if (!state) return;
    const view = useViewportStore.getState();
    const dx = e.clientX - state.startClientX;
    const dDays = Math.round(dx / view.pxPerDay);

    if (state.kind === 'pan') {
      if (!state.startViewDate) return;
      view.setStart(addDays(state.startViewDate, -dDays));
      return;
    }

    state.deltaDays = dDays;
    const next: DragPreviewMap = { tasks: new Map(), milestones: new Map(), brackets: new Map() };

    if (state.kind === 'task-move' && state.originalStart && state.originalEnd && state.targetId) {
      next.tasks.set(state.targetId, {
        start: addDays(state.originalStart, dDays),
        end: addDays(state.originalEnd, dDays),
      });
    } else if (state.kind === 'task-resize-start' && state.originalStart && state.originalEnd && state.targetId) {
      const newStart = addDays(state.originalStart, dDays);
      const limited = diffDays(newStart, state.originalEnd) >= 0 ? newStart : state.originalEnd;
      next.tasks.set(state.targetId, { start: limited, end: state.originalEnd });
    } else if (state.kind === 'task-resize-end' && state.originalStart && state.originalEnd && state.targetId) {
      const newEnd = addDays(state.originalEnd, dDays);
      const limited = diffDays(state.originalStart, newEnd) >= 0 ? newEnd : state.originalStart;
      next.tasks.set(state.targetId, { start: state.originalStart, end: limited });
    } else if (state.kind === 'milestone-move' && state.originalDate && state.targetId) {
      next.milestones.set(state.targetId, addDays(state.originalDate, dDays));
    } else if (state.kind === 'bracket-move' && state.originalStart && state.originalEnd && state.targetId) {
      next.brackets.set(state.targetId, {
        start: addDays(state.originalStart, dDays),
        end: addDays(state.originalEnd, dDays),
      });
    } else if (state.kind === 'bracket-resize-start' && state.originalStart && state.originalEnd && state.targetId) {
      next.brackets.set(state.targetId, { start: addDays(state.originalStart, dDays), end: state.originalEnd });
    } else if (state.kind === 'bracket-resize-end' && state.originalStart && state.originalEnd && state.targetId) {
      next.brackets.set(state.targetId, { start: state.originalStart, end: addDays(state.originalEnd, dDays) });
    }
    setDragPreview(next);
  }, []);

  const handleWindowUp = useCallback(() => {
    const state = dragRef.current;
    detachWindowListeners(handleWindowMove, handleWindowUp);
    dragRef.current = null;
    if (!state) {
      setDragPreview(null);
      return;
    }
    const doc = useDocumentStore.getState();
    if (state.kind === 'task-move' && state.targetId && state.originalStart && state.originalEnd) {
      doc.updateTask(state.targetId, {
        start: addDays(state.originalStart, state.deltaDays),
        end: addDays(state.originalEnd, state.deltaDays),
      });
    } else if (state.kind === 'task-resize-start' && state.targetId && state.originalStart && state.originalEnd) {
      const newStart = addDays(state.originalStart, state.deltaDays);
      const limited = diffDays(newStart, state.originalEnd) >= 0 ? newStart : state.originalEnd;
      doc.updateTask(state.targetId, { start: limited });
    } else if (state.kind === 'task-resize-end' && state.targetId && state.originalStart && state.originalEnd) {
      const newEnd = addDays(state.originalEnd, state.deltaDays);
      const limited = diffDays(state.originalStart, newEnd) >= 0 ? newEnd : state.originalStart;
      doc.updateTask(state.targetId, { end: limited });
    } else if (state.kind === 'milestone-move' && state.targetId && state.originalDate) {
      doc.updateMilestone(state.targetId, { date: addDays(state.originalDate, state.deltaDays) });
    } else if (state.kind === 'bracket-move' && state.targetId && state.originalStart && state.originalEnd) {
      doc.updateBracket(state.targetId, {
        start: addDays(state.originalStart, state.deltaDays),
        end: addDays(state.originalEnd, state.deltaDays),
      });
    } else if (state.kind === 'bracket-resize-start' && state.targetId && state.originalStart) {
      doc.updateBracket(state.targetId, { start: addDays(state.originalStart, state.deltaDays) });
    } else if (state.kind === 'bracket-resize-end' && state.targetId && state.originalEnd) {
      doc.updateBracket(state.targetId, { end: addDays(state.originalEnd, state.deltaDays) });
    }
    setDragPreview(null);
  }, [handleWindowMove]);

  return { handleSvgMouseDown, handleWheel, dragPreview, xToDate };
}

let attached = false;
function attachWindowListeners(
  move: (e: globalThis.MouseEvent) => void,
  up: (e: globalThis.MouseEvent) => void,
): void {
  if (attached) return;
  window.addEventListener('mousemove', move);
  window.addEventListener('mouseup', up);
  attached = true;
}
function detachWindowListeners(
  move: (e: globalThis.MouseEvent) => void,
  up: (e: globalThis.MouseEvent) => void,
): void {
  window.removeEventListener('mousemove', move);
  window.removeEventListener('mouseup', up);
  attached = false;
}
