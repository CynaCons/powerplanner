import { useMemo } from 'react';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import { allocateLabels, type Placement } from './labelFit';

interface Props {
  layout: LayoutResult;
}

const SIZE = 11;

export function Milestones({ layout }: Props) {
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
          requested: lm.milestone.labelPlacement,
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
              style={{ cursor: 'grab' }}
            />
            <MilestoneLabel id={m.id} x={x} y={y} placement={alloc?.placement} text={m.label} />
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
}

function MilestoneLabel({ id, x, y, text, placement }: MlProps) {
  if (!placement || placement === 'hidden') return null;

  const baseProps = {
    fontSize: 11,
    fill: 'var(--color-text)',
    pointerEvents: 'none' as const,
  };

  // A label that has stepped off the line needs to say which diamond it belongs
  // to. Left and right sit against their own marker and need no help; above and
  // below are centred over it, and in a crowded lane that centring alone is not
  // enough to read (owner directive 2026-10-23).
  const leader = (y1: number, y2: number) => (
    <line
      data-pp-leader={id}
      x1={x}
      y1={y1}
      x2={x}
      y2={y2}
      stroke="var(--color-text-mute)"
      strokeWidth={1}
      pointerEvents="none"
    />
  );

  if (placement === 'above')
    return (
      <>
        {leader(y - SIZE - 1, y - SIZE - 7)}
        <text x={x} y={y - SIZE - 8} textAnchor="middle" {...baseProps}>{text}</text>
      </>
    );
  if (placement === 'below')
    return (
      <>
        {leader(y + SIZE + 1, y + SIZE + 7)}
        <text x={x} y={y + SIZE + 17} textAnchor="middle" {...baseProps}>{text}</text>
      </>
    );
  if (placement === 'left')
    return <text x={x - SIZE - 4} y={y + 4} textAnchor="end" {...baseProps}>{text}</text>;
  return <text x={x + SIZE + 4} y={y + 4} {...baseProps}>{text}</text>;
}

