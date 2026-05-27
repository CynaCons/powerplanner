import { useRef, useState, useCallback } from 'react';
import type { RefObject, MouseEvent as ReactMouseEvent, WheelEvent as ReactWheelEvent } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import { useToolStore } from '../stores/toolStore';
import { useContextMenuStore } from '../stores/contextMenuStore';
import { xToDate } from '../layout/timeAxis';
import { addDays, diffDays } from '../utils/dates';
import { snapDelta } from '../utils/snap';
import { newId } from '../utils/ids';
import type { LayoutResult } from '../layout/engine';

interface InteractionOptions {
  svgRef: RefObject<SVGSVGElement | null>;
  chartWidth: number;
  rowGutterWidth: number;
  axisHeight: number;
  layout: LayoutResult;
}

function rowAtY(yInChart: number, layout: LayoutResult): string | null {
  for (let i = 0; i < layout.rowOffsets.length; i++) {
    const top = layout.rowOffsets[i];
    const bottom = top + layout.rowHeights[i];
    if (yInChart >= top && yInChart < bottom) return layout.visibleRows[i].id;
  }
  return null;
}

export type DragKind =
  | 'pan'
  | 'task-move'
  | 'task-resize-start'
  | 'task-resize-end'
  | 'milestone-move'
  | 'bracket-move'
  | 'bracket-resize-start'
  | 'bracket-resize-end'
  | 'dep-create'
  | 'lasso';

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
  depSourceTaskId?: string;
  depSourceEdge?: 'start' | 'end';
  depCurrentX?: number;
  depCurrentY?: number;
  depStartX?: number;
  depStartY?: number;
  lassoStartX?: number;
  lassoStartY?: number;
  lassoCurrentX?: number;
  lassoCurrentY?: number;
}

export interface DragPreviewMap {
  tasks: Map<string, { start: string; end: string }>;
  milestones: Map<string, string>;
  brackets: Map<string, { start: string; end: string }>;
}

export function useChartInteractions({ svgRef, rowGutterWidth, axisHeight, layout }: InteractionOptions) {
  const dragRef = useRef<DragState | null>(null);
  const [dragPreview, setDragPreview] = useState<DragPreviewMap | null>(null);
  const [, forceRender] = useState(0);
  const triggerRender = useCallback(() => forceRender((n) => n + 1), []);

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
      // Default: wheel = zoom anchored at cursor (Figma/Linear convention)
      // Shift+wheel = horizontal pan
      // Ctrl/Meta still zooms (browser default would zoom anyway)
      if (e.shiftKey) {
        e.preventDefault();
        useViewportStore.getState().pan(-e.deltaY);
      } else {
        e.preventDefault();
        const { x } = getSvgPoint(e.clientX, e.clientY);
        const anchorX = Math.max(0, x - rowGutterWidth);
        const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
        useViewportStore.getState().zoom(factor, anchorX);
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
        const tool = useToolStore.getState();
        const xInChart = x - rowGutterWidth;
        const { y: yAbs } = getSvgPoint(e.clientX, e.clientY);
        const yInChart = yAbs - axisHeight;

        // Tool-aware background click
        if (tool.activeTool === 'add-task' || tool.activeTool === 'add-milestone') {
          const date = xToDate(xInChart, view.startDate, view.pxPerDay);
          const rowId = rowAtY(yInChart, layout) ?? layout.visibleRows[0]?.id;
          if (rowId) {
            if (tool.activeTool === 'add-task') {
              const newTaskId = newId('task');
              doc.tasks; // typescript no-op
              const stores = useDocumentStore.getState();
              stores.addTask({
                id: newTaskId,
                rowId,
                label: 'New task',
                start: date,
                end: addDays(date, 7),
                percentComplete: 0,
              });
              useSelectionStore.getState().setSelection([{ kind: 'task', id: newTaskId }]);
            } else {
              const newMsId = newId('ms');
              useDocumentStore.getState().addMilestone({ id: newMsId, rowId, label: 'New milestone', date });
              useSelectionStore.getState().setSelection([{ kind: 'milestone', id: newMsId }]);
            }
            tool.consumeOneShot();
          }
          return;
        }

        if (tool.activeTool === 'marquee') {
          dragRef.current = {
            kind: 'lasso',
            startClientX: e.clientX,
            startClientY: e.clientY,
            lassoStartX: xInChart,
            lassoStartY: yInChart,
            lassoCurrentX: xInChart,
            lassoCurrentY: yInChart,
            deltaDays: 0,
          };
          attachWindowListeners(handleWindowMove, handleWindowUp);
          return;
        }

        if (tool.activeTool === 'select') {
          useSelectionStore.getState().clear();
        }

        // Pan (default fallback OR explicit pan tool)
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

      // Dependency creation handle: a small grab dot on bar edges in "connector" mode
      if (kind === 'connector') {
        const task = doc.tasks.find((t) => t.id === id);
        if (!task) return;
        const { x: cx, y: cy } = getSvgPoint(e.clientX, e.clientY);
        dragRef.current = {
          kind: 'dep-create',
          startClientX: e.clientX,
          startClientY: e.clientY,
          depSourceTaskId: id,
          depSourceEdge: sub === 'start' ? 'start' : 'end',
          depStartX: cx - rowGutterWidth,
          depStartY: cy - axisHeight,
          depCurrentX: cx - rowGutterWidth,
          depCurrentY: cy - axisHeight,
          deltaDays: 0,
        };
        attachWindowListeners(handleWindowMove, handleWindowUp);
        return;
      }

      // Selection on click
      const selStore = useSelectionStore.getState();
      const additive = e.shiftKey || e.metaKey || e.ctrlKey;
      if (kind === 'task' || kind === 'milestone' || kind === 'bracket' || kind === 'dependency' || kind === 'marker') {
        if (!additive) {
          selStore.setSelection([{ kind: kind as 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker', id }]);
        } else if (!selStore.isSelected(id)) {
          selStore.add({ kind: kind as 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker', id });
        }
      }
      // Dependency and marker are selection-only (no drag)
      if (kind === 'dependency' || kind === 'marker') {
        return;
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

    if (state.kind === 'dep-create') {
      const svg = svgRef.current;
      if (!svg) return;
      const rect = svg.getBoundingClientRect();
      state.depCurrentX = e.clientX - rect.left - rowGutterWidth;
      state.depCurrentY = e.clientY - rect.top - axisHeight;
      triggerRender();
      return;
    }

    if (state.kind === 'lasso') {
      const svg = svgRef.current;
      if (!svg) return;
      const rect = svg.getBoundingClientRect();
      state.lassoCurrentX = e.clientX - rect.left - rowGutterWidth;
      state.lassoCurrentY = e.clientY - rect.top - axisHeight;
      triggerRender();
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
    const docStore = useDocumentStore.getState();
    const edit = useEditStore.getState();
    const scale = docStore.doc.calendar.scale;
    const snappedDelta = edit.snapEnabled ? snapDelta(state.deltaDays, scale) : state.deltaDays;

    if (state.kind === 'lasso') {
      const x0 = Math.min(state.lassoStartX!, state.lassoCurrentX!);
      const x1 = Math.max(state.lassoStartX!, state.lassoCurrentX!);
      const y0 = Math.min(state.lassoStartY!, state.lassoCurrentY!);
      const y1 = Math.max(state.lassoStartY!, state.lassoCurrentY!);
      const sel = useSelectionStore.getState();
      sel.clear();
      for (const t of layout.tasks) {
        if (t.x + t.width >= x0 && t.x <= x1 && t.y + t.height >= y0 && t.y <= y1) {
          sel.add({ kind: 'task', id: t.task.id });
        }
      }
      for (const m of layout.milestones) {
        if (m.x >= x0 && m.x <= x1 && m.y >= y0 && m.y <= y1) {
          sel.add({ kind: 'milestone', id: m.milestone.id });
        }
      }
      useToolStore.getState().consumeOneShot();
      triggerRender();
      return;
    }

    if (state.kind === 'dep-create' && state.depSourceTaskId) {
      // Check if drop target is a task
      const dropEl = document.elementFromPoint(state.depCurrentX! + rowGutterWidth + (svgRef.current?.getBoundingClientRect().left ?? 0), state.depCurrentY! + axisHeight + (svgRef.current?.getBoundingClientRect().top ?? 0));
      const targetEl = dropEl?.closest('[data-pp-kind="task"]') as HTMLElement | null;
      if (targetEl && targetEl.dataset.ppId && targetEl.dataset.ppId !== state.depSourceTaskId) {
        const fromId = state.depSourceTaskId;
        const toId = targetEl.dataset.ppId;
        const exists = docStore.doc.dependencies.some((d) => d.from === fromId && d.to === toId);
        if (!exists) {
          docStore.addDependency({
            id: `dep-${Math.random().toString(36).slice(2, 10)}`,
            from: fromId,
            to: toId,
            type: state.depSourceEdge === 'start' ? 'start-to-start' : 'finish-to-start',
          });
        }
      }
      setDragPreview(null);
      triggerRender();
      return;
    }

    if (state.kind === 'task-move' && state.targetId && state.originalStart && state.originalEnd) {
      docStore.updateTask(state.targetId, {
        start: addDays(state.originalStart, snappedDelta),
        end: addDays(state.originalEnd, snappedDelta),
      });
    } else if (state.kind === 'task-resize-start' && state.targetId && state.originalStart && state.originalEnd) {
      const newStart = addDays(state.originalStart, snappedDelta);
      const limited = diffDays(newStart, state.originalEnd) >= 0 ? newStart : state.originalEnd;
      docStore.updateTask(state.targetId, { start: limited });
    } else if (state.kind === 'task-resize-end' && state.targetId && state.originalStart && state.originalEnd) {
      const newEnd = addDays(state.originalEnd, snappedDelta);
      const limited = diffDays(state.originalStart, newEnd) >= 0 ? newEnd : state.originalStart;
      docStore.updateTask(state.targetId, { end: limited });
    } else if (state.kind === 'milestone-move' && state.targetId && state.originalDate) {
      docStore.updateMilestone(state.targetId, { date: addDays(state.originalDate, snappedDelta) });
    } else if (state.kind === 'bracket-move' && state.targetId && state.originalStart && state.originalEnd) {
      docStore.updateBracket(state.targetId, {
        start: addDays(state.originalStart, snappedDelta),
        end: addDays(state.originalEnd, snappedDelta),
      });
    } else if (state.kind === 'bracket-resize-start' && state.targetId && state.originalStart) {
      docStore.updateBracket(state.targetId, { start: addDays(state.originalStart, snappedDelta) });
    } else if (state.kind === 'bracket-resize-end' && state.targetId && state.originalEnd) {
      docStore.updateBracket(state.targetId, { end: addDays(state.originalEnd, snappedDelta) });
    }
    setDragPreview(null);
  }, [handleWindowMove]);

  const depDrag = dragRef.current?.kind === 'dep-create' ? dragRef.current : null;
  const lassoDrag = dragRef.current?.kind === 'lasso' ? dragRef.current : null;

  const handleContextMenu = useCallback(
    (e: ReactMouseEvent<SVGSVGElement>) => {
      e.preventDefault();
      const target = e.target as Element;
      const datasetEl = target.closest('[data-pp-kind]') as HTMLElement | null;
      const { x: svgX, y: svgY } = getSvgPoint(e.clientX, e.clientY);
      const ctx = useContextMenuStore.getState();
      const sel = useSelectionStore.getState();

      if (datasetEl) {
        const kind = datasetEl.dataset.ppKind as string;
        const id = datasetEl.dataset.ppId as string;
        if (kind === 'task' || kind === 'milestone' || kind === 'bracket' || kind === 'dependency' || kind === 'marker') {
          if (!sel.isSelected(id)) sel.setSelection([{ kind: kind as 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker', id }]);
          ctx.show(e.clientX, e.clientY, { kind: kind as 'task' | 'milestone' | 'bracket' | 'dependency' | 'marker', id });
          return;
        }
      }
      // Background
      const xInChart = svgX - rowGutterWidth;
      const yInChart = svgY - axisHeight;
      const view = useViewportStore.getState();
      const date = xToDate(xInChart, view.startDate, view.pxPerDay);
      const rowId = rowAtY(yInChart, layout) ?? layout.visibleRows[0]?.id;
      ctx.show(e.clientX, e.clientY, { kind: 'background', date, rowId });
    },
    [getSvgPoint, rowGutterWidth, axisHeight, layout],
  );

  return { handleSvgMouseDown, handleWheel, handleContextMenu, dragPreview, xToDate, depDrag, lassoDrag };
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
