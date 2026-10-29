import { useMemo, useState } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useIsSelected } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import { useViewStore } from '../stores/viewStore';
import { ROW_HEIGHT, type LayoutResult } from '../layout/engine';
import { carriedY } from '../layout/geometry';
import { dateToX } from '../layout/timeAxis';
import type { DragPreviewMap } from '../app/useChartInteractions';
import { InlineLabel } from '../app/InlineLabel';
import { computeCriticalPath } from '../layout/criticalPath';
import { computeSpaceRight, fitLabel, type RequestedPlacement } from './labelFit';

interface Props {
  layout: LayoutResult;
  dragPreview: DragPreviewMap | null;
}

/**
 * Width of a resize grip, straddling the bar's end cap (SRS-EDIT-070).
 *
 * It straddles rather than sits inside, which is both where the owner's hand
 * goes and cheaper: a grip centred on the end eats only half its width out of
 * the body that carries the rename double-click.
 */
const GRIP_W = 7;
/** How far a dependency anchor floats clear of the end it belongs to. */
const ANCHOR_LEAD = 16;
/** Radius of that anchor. `ANCHOR_LEAD - r` is its clearance from the grip. */
const ANCHOR_R = 5;
/** Drawn width of the progress handle; its hit target is wider. */
const PROGRESS_HANDLE_W = 8;
/** Drawn height of a task bar. Smaller than the row band it is hit-tested in. */
const BAR_DRAWN_H = 11;
/** Half-width of a milestone diamond — must match SIZE in Milestones.tsx. */
const MILESTONE_HALF = 11;
/** How far an 11px glyph's descender reaches under its baseline. */
const DESCENDER_PX = 3;

/**
 * The baseline of a label placed below its bar (SRS-EDIT-055, SRS-EDIT-056).
 *
 * It used to be `bandTop + bandHeight + 12`, which on a 22px bar band in a
 * 36px row is five pixels into the NEXT row — so the label read as belonging
 * to the row under the one it describes, and on the last row there was no next
 * row to leak into: it fell past the sheet's bottom edge and the plot clip
 * removed it ("I don't see my label anymore. That's dumb.").
 *
 * The row is the budget. Whatever slack sits between the bar band and the row
 * floor is all a below-label gets, less the descender, so the glyph box ends
 * inside the row that owns the bar.
 */
export function belowLabelY(bandTop: number, bandHeight: number): number {
  const slackBelowBand = (ROW_HEIGHT - bandHeight) / 2;
  return bandTop + bandHeight + Math.max(1, slackBelowBand - DESCENDER_PX);
}

export function TaskBars({ layout, dragPreview }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useIsSelected();
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

  // The bar under the pointer. The grips and the progress handle appear on
  // hover as well as on selection, because selecting a bar to reach its grips
  // is the round trip SR-IXC-13 asks to be removed — the affordance has to be
  // there before the gesture starts.
  const [hoverId, setHoverId] = useState<string | null>(null);

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
        const h = lt.height;
        // A drag can carry the bar to another row, and until it is released the
        // layout still has it on the row it came from (SRS-EDIT-072).
        const y = preview?.rowId ? carriedY(layout, t.rowId, preview.rowId, lt.y, 0, h) : lt.y;
        // Leave at least 8px of body between the grips, so the double-click
        // that renames the task always has somewhere to land.
        const gripW = Math.min(GRIP_W, Math.floor(width - 8));
        const selected = isSelected(t.id);
        const critical = criticalIds.has(t.id);
        const color = t.color || 'var(--bar-default)';
        // The dragged value while a drag is in flight, so the fill tracks the
        // handle instead of snapping to it on release (SR-IXC-03).
        const dragPct = dragPreview?.progress.get(t.id);
        const pct = Math.max(0, Math.min(100, dragPct ?? t.percentComplete ?? 0));
        const fillWidth = (width * pct) / 100;
        const barTop = y + (h - BAR_DRAWN_H) / 2;
        const active = selected || hoverId === t.id;
        // Where the progress handle is drawn. The fill edge, except at 0% and
        // 100%, where the edge is exactly a resize grip: a handle buried under
        // one is an affordance that cannot be reached, and the grip wins the
        // press (SRS-EDIT-071). Held a grip's width inside instead, which at
        // those two values is the only place it can be both seen and grabbed.
        const handleX =
          gripW > 0
            ? Math.min(Math.max(x + fillWidth, x + gripW), x + width - gripW)
            : x + fillWidth;
        // The handle's hit target joins the same body budget the grips observe
        // (SRS-EDIT-078). It was a hard 10px, which on a one-week bar at the
        // opening zoom — 15px — covered every pixel of body the grips had left,
        // so the double-click that renames a task had nowhere to land.
        const progW = Math.max(4, Math.min(10, width - 2 * gripW));

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
              /* Which row it belongs to, so an insert can be shown to have
                 landed where the planner was looking (SRS-EDIT-028). */
              data-pp-row={t.rowId}
              /* Selection, readable from the DOM. The outline that shows it is
                 an anonymous sibling rect, so nothing could say WHICH bars a
                 gesture took — only how many (SRS-EDIT-033). */
              data-pp-selected={selected ? '1' : undefined}
              x={x}
              y={y}
              width={width}
              height={h}
              fill="transparent"
              style={{ cursor: 'move' }}
              onDoubleClick={() => beginEdit(t.id)}
              onMouseEnter={() => setHoverId(t.id)}
              onMouseLeave={() => setHoverId((cur) => (cur === t.id ? null : cur))}
            />
            {/* The drawn bar (direction B, owner's choice 2026-10-23): a thin
                solid span. Remaining work is the same hue held back, completed
                work is solid, and the label sits outside — which is what lets
                the bar stay this quiet without losing its reading. */}
            <rect
              x={x}
              y={barTop}
              width={width}
              height={BAR_DRAWN_H}
              fill={color}
              style={{ fillOpacity: 'var(--bar-track-a)' }}
              pointerEvents="none"
            />
            {pct > 0 && (
              <rect
                className="bar-progress-fill"
                data-pp-fill={t.id}
                x={x}
                y={barTop}
                width={fillWidth}
                height={BAR_DRAWN_H}
                fill={color}
                pointerEvents="none"
              />
            )}
            {/* Selection outline. Before the grips and the anchors, so neither
                is cut in half by the frame that surrounds them. */}
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
            {/* Progress by dragging the fill edge (SRS-EDIT-074, SR-IXC-06).
                Its hit target is the full band height and wider than the drawn
                handle, because 8px of chrome is not 8px of target. */}
            {gripW > 0 && (active || dragPct != null) && (
              <>
                <rect
                  className="bar-progress-handle"
                  x={handleX - PROGRESS_HANDLE_W / 2}
                  y={barTop - 4}
                  width={PROGRESS_HANDLE_W}
                  height={BAR_DRAWN_H + 8}
                  rx={3}
                  stroke={color}
                  pointerEvents="none"
                />
                <rect
                  data-pp-kind="task"
                  data-pp-id={t.id}
                  data-pp-sub="progress"
                  x={handleX - progW / 2}
                  y={y}
                  width={progW}
                  height={h}
                  fill="transparent"
                  style={{ cursor: 'ew-resize' }}
                  onDoubleClick={() => beginEdit(t.id)}
                />
              </>
            )}
            {/* The live percentage, while the handle is held (SR-IXC-06). It
                is above the bar rather than beside it in x: beside, it would
                sit on the neighbour in the same lane at any real zoom. */}
            {dragPct != null && (
              <g pointerEvents="none">
                <rect
                  className="bar-progress-pill"
                  x={handleX - 19}
                  y={barTop - 28}
                  width={38}
                  height={19}
                  rx={9.5}
                />
                <text
                  className="bar-progress-pill-text"
                  data-pp-progress={t.id}
                  x={handleX}
                  y={barTop - 14.5}
                  textAnchor="middle"
                  fontSize={11}
                  fontWeight={600}
                >
                  {pct}%
                </text>
              </g>
            )}
            {/* The grips, on the ends, where every Gantt tool puts them and
                where the owner's hand already went (SRS-EDIT-070, §02 of the
                design demo). Never wider than the bar can spare: two fixed
                grips covering a 10px bar end to end left no body to
                double-click, and a freshly inserted one-week task is about
                10px at the zoom the sample opens on. */}
            {gripW > 0 && (
              <>
                <rect
                  data-pp-kind="task"
                  data-pp-id={t.id}
                  data-pp-sub="start"
                  x={x - gripW / 2}
                  y={y}
                  width={gripW}
                  height={h}
                  fill="transparent"
                  style={{ cursor: 'ew-resize' }}
                  onDoubleClick={() => beginEdit(t.id)}
                />
                <rect
                  data-pp-kind="task"
                  data-pp-id={t.id}
                  data-pp-sub="end"
                  x={x + width - gripW / 2}
                  y={y}
                  width={gripW}
                  height={h}
                  fill="transparent"
                  style={{ cursor: 'ew-resize' }}
                  onDoubleClick={() => beginEdit(t.id)}
                />
                {active && (
                  <>
                    <rect
                      className="bar-grip"
                      x={x - gripW / 2}
                      y={barTop - 1}
                      width={gripW}
                      height={BAR_DRAWN_H + 2}
                      rx={2}
                      pointerEvents="none"
                    />
                    <rect
                      className="bar-grip"
                      x={x + width - gripW / 2}
                      y={barTop - 1}
                      width={gripW}
                      height={BAR_DRAWN_H + 2}
                      rx={2}
                      pointerEvents="none"
                    />
                  </>
                )}
              </>
            )}
            {/* The dependency anchors, floated clear of the ends on a short
                lead (SRS-EDIT-070). They used to be centred ON the end caps,
                which is the one place a resize grip can go, so selecting a bar
                in order to resize it took the grips away. */}
            {selected && (
              <>
                <line
                  className="bar-anchor-lead"
                  x1={x - 4}
                  y1={y + h / 2}
                  x2={x - ANCHOR_LEAD}
                  y2={y + h / 2}
                  pointerEvents="none"
                />
                <line
                  className="bar-anchor-lead"
                  x1={x + width + 4}
                  y1={y + h / 2}
                  x2={x + width + ANCHOR_LEAD}
                  y2={y + h / 2}
                  pointerEvents="none"
                />
                <circle
                  className="bar-anchor"
                  data-pp-kind="connector"
                  data-pp-id={t.id}
                  data-pp-sub="start"
                  cx={x - ANCHOR_LEAD}
                  cy={y + h / 2}
                  r={ANCHOR_R}
                  style={{ cursor: 'crosshair' }}
                />
                <circle
                  className="bar-anchor"
                  data-pp-kind="connector"
                  data-pp-id={t.id}
                  data-pp-sub="end"
                  cx={x + width + ANCHOR_LEAD}
                  cy={y + h / 2}
                  r={ANCHOR_R}
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
      <text x={x + 4} y={belowLabelY(y, barHeight)} {...baseProps}>
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
