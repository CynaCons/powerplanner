import { useMemo } from 'react';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import { allocateLabels, type Placement } from './labelFit';
import type { DragPreviewMap } from '../app/useChartInteractions';

interface Props {
  layout: LayoutResult;
  dragPreview?: DragPreviewMap | null;
}

const SIZE = 11;

export function Milestones({ layout, dragPreview }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);

  // Milestone labels are allocated across the lane rather than fitted one at a
  // time, so a label blocked on the right steps above or below instead of
  // being truncated or dropped (owner report 2026-10-23).
  const placements = useMemo(
    () =>
      allocateLabels(
        layout.milestones.map((lm) => ({
          id: lm.milestone.id,
          x: dateToX(lm.milestone.date, viewStart, pxPerDay) + pxPerDay / 2,
          text: lm.milestone.label,
          lane: `${lm.milestone.rowId}#${lm.subRow ?? 0}`,
          requested: lm.milestone.labelOffset ? 'hidden' : lm.milestone.labelPlacement,
        })),
        SIZE,
      ),
    [layout.milestones, viewStart, pxPerDay],
  );

  return (
    <g>
      {layout.milestones.map((lm) => {
        const m = lm.milestone;
        const x = dateToX(m.date, viewStart, pxPerDay) + pxPerDay / 2;
        const y = lm.y;
        const selected = isSelected(m.id);
        const color = m.color || 'var(--color-milestone)';
        // Undefined means "auto", NOT "right". Defaulting to `right` here made
        // the label look like an explicit user choice to the fitter, which
        // passes explicit placements through untouched — so milestone labels
        // were never fitted and kept colliding.
        const alloc = placements.get(m.id);
        return (
          <g key={m.id}>
            <polygon
              data-pp-kind="milestone"
              data-pp-id={m.id}
              points={`${x},${y - SIZE} ${x + SIZE},${y} ${x},${y + SIZE} ${x - SIZE},${y}`}
              fill={color}
              stroke={selected ? 'var(--color-accent)' : 'rgba(0,0,0,0.3)'}
              strokeWidth={selected ? 2 : 1}
              style={{ cursor: 'move' }}
            />
            <MilestoneLabel
              id={m.id}
              x={x}
              y={y}
              placement={alloc?.placement}
              text={m.label}
              offset={dragPreview?.labels.get(m.id) ?? m.labelOffset ?? null}
            />
          </g>
        );
      })}
    </g>
  );
}

interface MlProps {
  id: string;
  x: number;
  y: number;
  text: string;
  /** Resolved by the lane allocator. Undefined means it could not be placed. */
  placement: Placement | undefined;
  /** A place the user dragged it to, which overrides the allocator. */
  offset: { dx: number; dy: number } | null;
}

/** Where a label's anchor sits for each automatic placement, in px from the
 *  marker centre. Dragging seeds its offset from here, so a label does not
 *  jump the moment it is grabbed. */
function anchorFor(placement: Placement): { dx: number; dy: number } {
  switch (placement) {
    case 'above':
      return { dx: 0, dy: -SIZE - 8 };
    case 'below':
      return { dx: 0, dy: SIZE + 17 };
    case 'left':
      return { dx: -SIZE - 4, dy: 4 };
    default:
      return { dx: SIZE + 4, dy: 4 };
  }
}

function MilestoneLabel({ id, x, y, text, placement, offset }: MlProps) {
  const dragged = offset != null;
  if (!dragged && (!placement || placement === 'hidden')) return null;

  const anchor = dragged ? offset : anchorFor(placement ?? 'right');
  const tx = x + anchor.dx;
  const ty = y + anchor.dy;

  // A dragged label can end up anywhere, so it always keeps a leader; the
  // automatic vertical placements keep the short one they had.
  const needsLeader = dragged || placement === 'above' || placement === 'below';
  const len = Math.hypot(anchor.dx, anchor.dy) || 1;
  const ux = anchor.dx / len;
  const uy = anchor.dy / len;
  const edge = SIZE + 1;
  const stop = Math.max(edge + 2, len - 4);

  const anchorAttr = dragged ? 'start' : placement === 'above' || placement === 'below' ? 'middle' : placement === 'left' ? 'end' : 'start';

  return (
    <>
      {needsLeader && (
        <line
          data-pp-leader={id}
          x1={x + ux * edge}
          y1={y + uy * edge}
          x2={x + ux * stop}
          y2={y + uy * stop}
          stroke="var(--color-text-mute)"
          strokeWidth={1}
          pointerEvents="none"
        />
      )}
      <text
        data-pp-kind="label"
        data-pp-id={id}
        data-pp-owner="milestone"
        data-pp-dx={anchor.dx}
        data-pp-dy={anchor.dy}
        x={tx}
        y={ty}
        textAnchor={anchorAttr}
        fontSize={11}
        fill="var(--color-text)"
        style={{ cursor: 'move' }}
      >
        {text}
      </text>
    </>
  );
}
