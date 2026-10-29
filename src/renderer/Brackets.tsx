import { useViewportStore } from '../stores/viewportStore';
import { useIsSelected } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';

interface Props {
  layout: LayoutResult;
}

/** How far below the top edge of its topmost row the rail runs. */
const RAIL_INSET = 6;
/** The drop on each end tick, and the reach of the grips either side of it. */
const TICK_DROP = 5;
const LABEL_SIZE = 10;
/** Roughly the advance of the 10px label face. */
const LABEL_CHAR_PX = 5.8;

export interface BracketRail {
  railY: number;
  labelBaseline: number;
  /** The chip that interrupts the rail behind the label. */
  chipTop: number;
  chipHeight: number;
  bottomY: number;
}

/**
 * Every y a bracket draws at (SRS-ELEM-031).
 *
 * The rail used to be drawn at `rowTop − 12` with its label six pixels above
 * that, which on row 0 is a negative y. `chart-plot-clip` begins at the top of
 * row 0, so the rail, the label and both resize grips were cut away and all
 * that survived was the faint bottom rule — the "horizontal line" the owner
 * could not identify, and the reason the bracket could then not be grabbed.
 *
 * Everything therefore hangs off the INSIDE of the top row. A 36px row leaves
 * only 7px above its 22px bar band, which is not enough for a rail and a
 * label, so the label sits in an opaque chip that interrupts the rail: the
 * name stays readable over whatever bar it crosses, and no part of the bracket
 * leaves the sheet.
 */
export function bracketRail(rowTop: number, bottomRowTop: number, bottomRowHeight: number): BracketRail {
  const railY = rowTop + RAIL_INSET;
  return {
    railY,
    labelBaseline: railY + 3.5,
    chipTop: railY - RAIL_INSET,
    chipHeight: RAIL_INSET * 2,
    bottomY: bottomRowTop + bottomRowHeight - 4,
  };
}

export function Brackets({ layout }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useIsSelected();

  return (
    <g>
      {layout.brackets.map((lb) => {
        const b = lb.bracket;
        const x = dateToX(b.start, viewStart, pxPerDay);
        const endX = dateToX(b.end, viewStart, pxPerDay) + pxPerDay;
        const w = Math.max(2, endX - x);
        const topRow = lb.topRow;
        const bottomRow = lb.bottomRow;
        const rail = bracketRail(
          layout.rowOffsets[topRow],
          layout.rowOffsets[bottomRow],
          layout.rowHeights[bottomRow] ?? 36,
        );
        const selected = isSelected(b.id);
        const color = b.color || 'var(--color-accent)';
        const labelW = b.label.length * LABEL_CHAR_PX + 8;
        const gapStart = x + w / 2 - labelW / 2;
        const gapEnd = x + w / 2 + labelW / 2;

        return (
          <g key={b.id}>
            {/* The rail, in two segments with the label's chip between them.
                Drawing it under the chip instead would show the line through
                the glyphs wherever the label is wider than the gap. */}
            <line x1={x} y1={rail.railY} x2={Math.max(x, gapStart)} y2={rail.railY} stroke={color} strokeWidth={1.5} />
            <line x1={Math.min(x + w, gapEnd)} y1={rail.railY} x2={x + w} y2={rail.railY} stroke={color} strokeWidth={1.5} />
            <line x1={x} y1={rail.railY} x2={x} y2={rail.railY + TICK_DROP} stroke={color} strokeWidth={1.5} />
            <line x1={x + w} y1={rail.railY} x2={x + w} y2={rail.railY + TICK_DROP} stroke={color} strokeWidth={1.5} />
            <line
              x1={x}
              y1={rail.bottomY}
              x2={x + w}
              y2={rail.bottomY}
              stroke={color}
              strokeWidth={1.5}
              opacity={0.4}
            />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              x={x}
              y={rail.railY - TICK_DROP}
              width={w}
              height={TICK_DROP * 2}
              fill="transparent"
              style={{ cursor: 'move' }}
            />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              data-pp-sub="start"
              x={x - 4}
              y={rail.railY - TICK_DROP}
              width={8}
              height={TICK_DROP * 2 + 2}
              fill="transparent"
              style={{ cursor: 'ew-resize' }}
            />
            <rect
              data-pp-kind="bracket"
              data-pp-id={b.id}
              data-pp-sub="end"
              x={x + w - 4}
              y={rail.railY - TICK_DROP}
              width={8}
              height={TICK_DROP * 2 + 2}
              fill="transparent"
              style={{ cursor: 'ew-resize' }}
            />
            <rect
              x={gapStart}
              y={rail.chipTop}
              width={labelW}
              height={rail.chipHeight}
              fill="var(--color-bg-panel)"
              pointerEvents="none"
            />
            <text
              x={x + w / 2}
              y={rail.labelBaseline}
              textAnchor="middle"
              fontSize={LABEL_SIZE}
              fontWeight={600}
              fill={color}
              pointerEvents="none"
            >
              {b.label}
            </text>
            {selected && (
              <rect
                x={x - 2}
                y={rail.chipTop}
                width={w + 4}
                height={Math.max(0, rail.bottomY - rail.chipTop + 4)}
                fill="none"
                stroke="var(--color-accent)"
                strokeDasharray="3 3"
                pointerEvents="none"
              />
            )}
          </g>
        );
      })}
    </g>
  );
}
