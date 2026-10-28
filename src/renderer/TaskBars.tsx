import { useMemo } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import { useViewStore } from '../stores/viewStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import type { DragPreviewMap } from '../app/useChartInteractions';
import { InlineLabel } from '../app/InlineLabel';
import { computeCriticalPath } from '../layout/criticalPath';
import { computeSpaceRight, fitLabel, type RequestedPlacement } from './labelFit';

interface Props {
  layout: LayoutResult;
  dragPreview: DragPreviewMap | null;
}

const HANDLE_W = 5;
/** Drawn height of a task bar. Smaller than the row band it is hit-tested in. */
const BAR_DRAWN_H = 11;
/** Half-width of a milestone diamond — must match SIZE in Milestones.tsx. */
const MILESTONE_HALF = 11;

export function TaskBars({ layout, dragPreview }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);
  const updateTask = useDocumentStore((s) => s.updateTask);
  const editingTaskId = useEditStore((s) => s.editingTaskId);
  const endEdit = useEditStore((s) => s.endEdit);
  const beginEdit = useEditStore((s) => s.beginEdit);
  const showCritical = useViewStore((s) => s.showCriticalPath);
  const doc = useDocumentStore((s) => s.doc);
  const criticalIds = useMemo(
    () => (showCritical ? computeCriticalPath(doc).criticalTaskIds : new Set<string>()),
    [showCritical, doc],
  );

  // How much free space each bar has before the next bar in its own lane.
  // A label may only spill right into space that is actually empty — otherwise
  // it lands on the neighbour's label, which is what made dense charts
  // unreadable (PLAN.md v2.12.0 / L0a).
  const spaceRight = useMemo(() => {
    // Milestones share the lane with tasks, so a diamond (and its own label) is
    // just as much of an obstacle as the next bar. Leaving them out is what
    // left task↔milestone collisions after the first pass at this fix.
    const geo = [
      ...layout.tasks.map((lt) => {
        const preview = dragPreview?.tasks.get(lt.task.id);
        const x = dateToX(preview?.start ?? lt.task.start, viewStart, pxPerDay);
        const endX = dateToX(preview?.end ?? lt.task.end, viewStart, pxPerDay);
        return {
          id: lt.task.id,
          x,
          right: x + Math.max(2, endX - x + pxPerDay),
          lane: `${lt.task.rowId}#${lt.subRow ?? 0}`,
        };
      }),
      ...layout.milestones.map((lm) => {
        const cx = dateToX(lm.milestone.date, viewStart, pxPerDay) + pxPerDay / 2;
        return {
          id: `__ms:${lm.milestone.id}`,
          x: cx - MILESTONE_HALF,
          right: cx + MILESTONE_HALF,
          lane: `${lm.milestone.rowId}#${lm.subRow ?? 0}`,
        };
      }),
    ];
    const spaced = computeSpaceRight(geo);
    const byId = new Map<string, number>();
    for (const [item, space] of spaced) byId.set(item.id, space);
    return byId;
  }, [layout.tasks, layout.milestones, dragPreview, viewStart, pxPerDay]);

  return (
    <g>
      {layout.tasks.map((lt) => {
        const t = lt.task;
        const preview = dragPreview?.tasks.get(t.id);
        const start = preview?.start ?? t.start;
        const end = preview?.end ?? t.end;
        const x = dateToX(start, viewStart, pxPerDay);
        const endX = dateToX(end, viewStart, pxPerDay);
        const width = Math.max(2, endX - x + pxPerDay);
        const y = lt.y;
        const h = lt.height;
        // Leave at least 8px of body between the grips, so the double-click
        // that renames the task always has somewhere to land.
        const handleW = Math.min(HANDLE_W, Math.floor((width - 8) / 2));
        const selected = isSelected(t.id);
        const critical = criticalIds.has(t.id);
        const color = t.color || 'var(--bar-default)';
        const pct = Math.max(0, Math.min(100, t.percentComplete ?? 0));
        const fillWidth = (width * pct) / 100;

        return (
          <g key={t.id}>
            {/* Critical path glow */}
            {critical && (
              <rect
                x={x - 3}
                y={y - 3}
                width={width + 6}
                height={h + 6}
                rx={6}
                fill="none"
                stroke="var(--color-today)"
                strokeWidth={1.5}
                opacity={0.75}
                pointerEvents="none"
              />
            )}
            {/* Hit target. Full row-band height so a bar stays easy to grab and
                drag, even though the drawn bar below is deliberately thin —
                visual weight and target size are different problems.
                `move`, not `grab`: a hand promises to pan the view, and this
                drag changes the schedule (SRS-EDIT-030, owner 2026-10-27). */}
            <rect
              className="task-bar-body"
              data-pp-kind="task"
              data-pp-id={t.id}
              x={x}
              y={y}
              width={width}
              height={h}
              fill="transparent"
              style={{ cursor: 'move' }}
              onDoubleClick={() => beginEdit(t.id)}
            />
            {/* The drawn bar (direction B, owner's choice 2026-10-23): a thin
                solid span. Remaining work is the same hue held back, completed
                work is solid, and the label sits outside — which is what lets
                the bar stay this quiet without losing its reading. */}
            <rect
              x={x}
              y={y + (h - BAR_DRAWN_H) / 2}
              width={width}
              height={BAR_DRAWN_H}
              fill={color}
              style={{ fillOpacity: 'var(--bar-track-a)' }}
              pointerEvents="none"
            />
            {pct > 0 && (
              <rect
                x={x}
                y={y + (h - BAR_DRAWN_H) / 2}
                width={fillWidth}
                height={BAR_DRAWN_H}
                fill={color}
                pointerEvents="none"
              />
            )}
            {/* Resize handles, never wider than the bar can spare.
                Two fixed 5px grips covered a 10px bar end to end, so the body
                underneath — which carries the double-click that renames it —
                could not be hit at all. A freshly inserted one-week task is
                about 10px at the zoom the sample opens on. */}
            {handleW > 0 && (
              <>
                <rect
                  data-pp-kind="task"
                  data-pp-id={t.id}
                  data-pp-sub="start"
                  x={x}
                  y={y}
                  width={handleW}
                  height={h}
                  fill="white"
                  opacity={selected ? 0.4 : 0.001}
                  style={{ cursor: 'ew-resize' }}
                />
                <rect
                  data-pp-kind="task"
                  data-pp-id={t.id}
                  data-pp-sub="end"
                  x={x + width - handleW}
                  y={y}
                  width={handleW}
                  height={h}
                  fill="white"
                  opacity={selected ? 0.4 : 0.001}
                  style={{ cursor: 'ew-resize' }}
                />
              </>
            )}
            {/* Selection outline */}
            {selected && (
              <rect
                x={x - 1}
                y={y - 1}
                width={width + 2}
                height={h + 2}
                rx={5}
                fill="none"
                stroke="var(--color-accent)"
                strokeWidth={1.5}
                pointerEvents="none"
              />
            )}
            {/* Connector handles (visible when selected) */}
            {selected && (
              <>
                <circle
                  data-pp-kind="connector"
                  data-pp-id={t.id}
                  data-pp-sub="start"
                  cx={x}
                  cy={y + h / 2}
                  r={5}
                  fill="var(--color-accent)"
                  stroke="white"
                  strokeWidth={1.5}
                  style={{ cursor: 'crosshair' }}
                />
                <circle
                  data-pp-kind="connector"
                  data-pp-id={t.id}
                  data-pp-sub="end"
                  cx={x + width}
                  cy={y + h / 2}
                  r={5}
                  fill="var(--color-accent)"
                  stroke="white"
                  strokeWidth={1.5}
                  style={{ cursor: 'crosshair' }}
                />
              </>
            )}
            {/* Inline editor / Label */}
            {editingTaskId === t.id ? (
              <InlineLabel
                text={t.label}
                x={x + 2}
                y={y + 2}
                width={Math.max(80, width - 4)}
                height={h - 4}
                onCommit={(text) => {
                  updateTask(t.id, { label: text });
                  endEdit();
                }}
                onCancel={endEdit}
              />
            ) : (
              // The label is `pointerEvents: none`, so the double-click
              // handler and the `text` cursor that used to sit on this wrapper
              // could never fire — a <g> has no hit area of its own. Renaming
              // is on the bar itself, on F2, and on the right-click menu.
              <LabelText
                text={t.label}
                x={x}
                y={y}
                barWidth={width}
                barHeight={h}
                placement={t.labelPlacement ?? 'auto-right'}
                spaceRight={spaceRight.get(t.id) ?? Infinity}
              />
            )}
          </g>
        );
      })}
    </g>
  );
}

interface LabelProps {
  text: string;
  x: number;
  y: number;
  barWidth: number;
  barHeight: number;
  placement: RequestedPlacement;
  /** Free space before the next bar in this lane; Infinity if nothing follows. */
  spaceRight: number;
}

function LabelText({ text, x, y, barWidth, barHeight, placement, spaceRight }: LabelProps) {
  const fit = fitLabel({ text, requested: placement, barWidth, spaceRight });
  if (fit.placement === 'hidden') return null;

  const baseProps = {
    fontSize: 11,
    fill: 'var(--color-text)',
    pointerEvents: 'none' as const,
  };
  // A shortened label must stay discoverable — hover reveals the full text.
  const title = fit.truncated ? <title>{text}</title> : null;

  if (fit.placement === 'left') {
    return (
      <text x={x - 6} y={y + barHeight / 2 + 4} textAnchor="end" {...baseProps}>
        {title}
        {fit.text}
      </text>
    );
  }
  if (fit.placement === 'right') {
    return (
      <text x={x + barWidth + 6} y={y + barHeight / 2 + 4} {...baseProps}>
        {title}
        {fit.text}
      </text>
    );
  }
  if (fit.placement === 'above') {
    return (
      <text x={x + 4} y={y - 4} {...baseProps}>
        {title}
        {fit.text}
      </text>
    );
  }
  if (fit.placement === 'below') {
    return (
      <text x={x + 4} y={y + barHeight + 12} {...baseProps}>
        {title}
        {fit.text}
      </text>
    );
  }
  // On-bar labels are ink, not white. White forced every bar to carry a
  // saturated fill to keep contrast; over a tint, ink reads cleanly in both
  // themes and whatever the stream colour is.
  return (
    <text x={x + 8} y={y + barHeight / 2 + 4} {...baseProps}>
      {title}
      <tspan>{fit.text}</tspan>
    </text>
  );
}
