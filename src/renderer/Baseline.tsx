import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useViewStore } from '../stores/viewStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import { BAR_HEIGHT } from '../layout/engine';

interface Props {
  layout: LayoutResult;
}

/**
 * Renders ghost outlines of each task's baseline (snapshotted) position
 * underneath the current bars, so drift is visually obvious.
 */
export function Baseline({ layout }: Props) {
  const baseline = useDocumentStore((s) => s.doc.baseline);
  const show = useViewStore((s) => s.showBaseline);
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);

  if (!show || !baseline) return null;
  const byId = new Map(baseline.tasks.map((t) => [t.id, t] as const));

  return (
    <g pointerEvents="none">
      {layout.tasks.map((lt) => {
        const b = byId.get(lt.task.id);
        if (!b) return null;
        const x = dateToX(b.start, viewStart, pxPerDay);
        const endX = dateToX(b.end, viewStart, pxPerDay);
        const w = Math.max(2, endX - x + pxPerDay);
        return (
          <rect
            key={lt.task.id}
            x={x}
            y={lt.y + BAR_HEIGHT + 1}
            width={w}
            height={3}
            rx={1.5}
            fill="var(--color-text-soft)"
            opacity={0.35}
          />
        );
      })}
    </g>
  );
}
