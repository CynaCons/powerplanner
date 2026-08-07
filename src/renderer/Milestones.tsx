import { useMemo } from 'react';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import { computeSpaceRight, fitLabel, type Placement } from './labelFit';

interface Props {
  layout: LayoutResult;
}

const SIZE = 11;

export function Milestones({ layout }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);

  // A milestone label competes with whatever comes next in its lane — which may
  // be a task bar, not another diamond. Both are laid into the same sub-row, so
  // both go into the occupancy list (PLAN.md v2.12.0 / L0d).
  const spaceRight = useMemo(() => {
    const items = [
      ...layout.tasks.map((lt) => {
        const x = dateToX(lt.task.start, viewStart, pxPerDay);
        const endX = dateToX(lt.task.end, viewStart, pxPerDay);
        return {
          id: `task:${lt.task.id}`,
          x,
          right: x + Math.max(2, endX - x + pxPerDay),
          lane: `${lt.task.rowId}#${lt.subRow ?? 0}`,
        };
      }),
      ...layout.milestones.map((lm) => {
        const cx = dateToX(lm.milestone.date, viewStart, pxPerDay) + pxPerDay / 2;
        return {
          id: `ms:${lm.milestone.id}`,
          x: cx - SIZE,
          right: cx + SIZE,
          lane: `${lm.milestone.rowId}#${lm.subRow ?? 0}`,
        };
      }),
    ];
    const spaced = computeSpaceRight(items);
    const byId = new Map<string, number>();
    for (const [item, space] of spaced) byId.set(item.id, space);
    return byId;
  }, [layout.tasks, layout.milestones, viewStart, pxPerDay]);

  return (
    <g>
      {layout.milestones.map((lm) => {
        const m = lm.milestone;
        const x = dateToX(m.date, viewStart, pxPerDay) + pxPerDay / 2;
        const y = lm.y;
        const selected = isSelected(m.id);
        const color = m.color || '#f59e0b';
        // Undefined means "auto", NOT "right". Defaulting to `right` here made
        // the label look like an explicit user choice to the fitter, which
        // passes explicit placements through untouched — so milestone labels
        // were never fitted and kept colliding.
        const placement = m.labelPlacement;
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
            <MilestoneLabel
              x={x}
              y={y}
              placement={placement}
              text={m.label}
              spaceRight={spaceRight.get(`ms:${m.id}`) ?? Infinity}
            />
          </g>
        );
      })}
    </g>
  );
}

interface MlProps {
  x: number;
  y: number;
  text: string;
  /** Undefined = auto-fit. A concrete value is an explicit document choice. */
  placement: Placement | undefined;
  spaceRight: number;
}

function MilestoneLabel({ x, y, text, placement, spaceRight }: MlProps) {
  // A diamond has no interior, so auto-fit enters the fitter as `on-bar` with a
  // zero-width bar: it can never sit "on" the marker, so the fitter falls
  // straight through to right / truncate / hide based on real space.
  const fit = fitLabel({
    text,
    requested: placement ?? 'on-bar',
    barWidth: 0,
    spaceRight,
  });
  if (fit.placement === 'hidden') return null;

  const baseProps = {
    fontSize: 11,
    fill: 'var(--color-text)',
    pointerEvents: 'none' as const,
  };
  const title = fit.truncated ? <title>{text}</title> : null;

  if (fit.placement === 'above')
    return <text x={x} y={y - SIZE - 4} textAnchor="middle" {...baseProps}>{title}{fit.text}</text>;
  if (fit.placement === 'below')
    return <text x={x} y={y + SIZE + 12} textAnchor="middle" {...baseProps}>{title}{fit.text}</text>;
  if (fit.placement === 'left')
    return <text x={x - SIZE - 4} y={y + 4} textAnchor="end" {...baseProps}>{title}{fit.text}</text>;
  return <text x={x + SIZE + 4} y={y + 4} {...baseProps}>{title}{fit.text}</text>;
}
