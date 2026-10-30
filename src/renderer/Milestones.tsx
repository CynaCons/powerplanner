import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { MouseEvent as ReactMouseEvent } from 'react';
import { useViewportStore } from '../stores/viewportStore';
import { useIsSelected, useSelectionStore } from '../stores/selectionStore';
import { useDocumentStore } from '../stores/documentStore';
import { MILESTONE_HALF, ROW_HEIGHT, type LayoutResult } from '../layout/engine';
import { carriedY } from '../layout/geometry';
import { gridDelimiterOf } from '../types/document';
import { anchoredX, dateToX } from '../layout/timeAxis';
import { allocateLabels, textWidth, type Placement } from './labelFit';
import type { DragPreviewMap } from '../app/useChartInteractions';
import { InlineLabel } from '../app/InlineLabel';

interface Props {
  layout: LayoutResult;
  dragPreview?: DragPreviewMap | null;
}

const SIZE = MILESTONE_HALF;
/** How far an 11px glyph's descender reaches under its baseline. */
const DESCENDER_PX = 3;
/** A press must travel this far before it is a drag and not a click. */
const LABEL_DRAG_THRESHOLD = 3;

export function Milestones({ layout, dragPreview }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useIsSelected();
  const updateMilestone = useDocumentStore((s) => s.updateMilestone);
  const delimiter = useDocumentStore((s) => gridDelimiterOf(s.doc.calendar));

  // One editor at a time, held here. `editStore` keys its open editor on
  // `editingTaskId`, and a milestone is not a task; the row gutter keeps its
  // own rename state for the same reason.
  const [editingId, setEditingId] = useState<string | null>(null);

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

  // A click on the label selects what the label names, exactly as a click on
  // the diamond does (SRS-EDIT-054, SR-TASK-UNIT-02).
  const select = useCallback((id: string, additive: boolean) => {
    const sel = useSelectionStore.getState();
    if (additive) sel.add({ kind: 'milestone', id });
    else sel.setSelection([{ kind: 'milestone', id }]);
  }, []);

  return (
    <g>
      {layout.milestones.map((lm) => {
        const m = lm.milestone;
        /**
         * Where the diamond is while a drag holds it (SRS-EDIT-073).
         *
         * The drag machinery had been writing this into `dragPreview.milestones`
         * since the preview existed and nothing read it: the x came straight
         * off `m.date`, so the diamond stayed nailed to its old date for the
         * whole gesture and jumped on release. The date pill was right all
         * along, which is why the readout moved and the marker did not.
         */
        const preview = dragPreview?.milestones.get(m.id);
        /*
         * The diamond has always been centred in its own day. That stays the
         * meaning of `start` and of a mark with no anchor at all, so nothing
         * already drawn moves; `middle` and `end` place it in the column the
         * grid is drawing instead, and the half-day would be a half-day past
         * the column's edge (SRS-ELEM-070).
         */
        const shownDate = preview?.date ?? m.date;
        const placed = !m.anchor || m.anchor === 'start';
        const x = placed
          ? dateToX(shownDate, viewStart, pxPerDay) + pxPerDay / 2
          : anchoredX(shownDate, viewStart, pxPerDay, delimiter, m.anchor);
        const y = preview?.rowId ? carriedY(layout, m.rowId, preview.rowId, lm.y, SIZE, SIZE) : lm.y;
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
              // The row it is on, as a task bar has carried all along. A
              // milestone's row is not readable from its y: when it leaves a
              // lane-stacked row that row collapses from 72px to 36 and
              // everything below moves up by exactly one row, so a carry to
              // the next row down can land at the y it started from.
              data-pp-row={m.rowId}
              points={`${x},${y - SIZE} ${x + SIZE},${y} ${x},${y + SIZE} ${x - SIZE},${y}`}
              fill={color}
              stroke={selected ? 'var(--color-accent)' : 'rgba(0,0,0,0.3)'}
              strokeWidth={selected ? 2 : 1}
              style={{ cursor: 'move' }}
              // Double-click renames in place, here as on a row and on a bar
              // (SRS-EDIT-052, SR-IXC-20). A milestone used to be the one
              // element whose name could only be reached through the dialog.
              onDoubleClick={() => setEditingId(m.id)}
            />
            <MilestoneLabel
              id={m.id}
              x={x}
              y={y}
              placement={alloc?.placement}
              text={m.label}
              textColor={m.textColor}
              offset={dragPreview?.labels.get(m.id) ?? m.labelOffset ?? null}
              editing={editingId === m.id}
              onBeginEdit={() => setEditingId(m.id)}
              onSelect={(additive) => select(m.id, additive)}
              onPin={(offset) => updateMilestone(m.id, { labelOffset: offset })}
              onCommit={(text) => {
                if (text.trim()) updateMilestone(m.id, { label: text.trim() });
                setEditingId(null);
              }}
              onCancel={() => setEditingId(null)}
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
  editing: boolean;
  onBeginEdit: () => void;
  onSelect: (additive: boolean) => void;
  onPin: (offset: { dx: number; dy: number }) => void;
  onCommit: (text: string) => void;
  onCancel: () => void;
  /** The planner's chosen ink for this label (SRS-ELEM-073). */
  textColor?: string;
}

/** Where a label's anchor sits for each automatic placement, in px from the
 *  marker centre. Dragging seeds its offset from here, so a label does not
 *  jump the moment it is grabbed. */
export function anchorFor(placement: Placement): { dx: number; dy: number } {
  switch (placement) {
    case 'above':
      return { dx: 0, dy: -SIZE - 8 };
    case 'below':
      // The row is the whole budget (SRS-EDIT-055, SRS-EDIT-056). `SIZE + 17`
      // put the baseline ten pixels into the next row's band, and on the last
      // row past the sheet's bottom edge, where the plot clip removed it
      // entirely — the owner chose "below" and the label disappeared.
      return { dx: 0, dy: ROW_HEIGHT / 2 - DESCENDER_PX };
    case 'left':
      return { dx: -SIZE - 4, dy: 4 };
    default:
      return { dx: SIZE + 4, dy: 4 };
  }
}

/**
 * How a label reads against its marker, from where it sits (SRS-EDIT-053).
 *
 * A label the user had placed was forced to `start`, so pinning one that was
 * centred above or below the marker switched its anchor at an unchanged anchor
 * point and threw the text right by half its own width — 38px and 55px on the
 * shipped sample. That is what a click which never moved looked like: "it
 * teleports away". Reading the anchor off the offset instead means a pin at
 * the place the label already occupies draws it exactly where it already was,
 * and the automatic placements keep the anchors they had.
 */
export function anchorAttrFor(dx: number): 'start' | 'middle' | 'end' {
  if (dx > SIZE) return 'start';
  if (dx < -SIZE) return 'end';
  return 'middle';
}

function MilestoneLabel({
  id,
  x,
  y,
  text,
  textColor,
  placement,
  offset,
  editing,
  onBeginEdit,
  onSelect,
  onPin,
  onCommit,
  onCancel,
}: MlProps) {
  // Where the label is while the pointer still holds it. Local, because the
  // gesture is owned here rather than by the surface's drag machinery.
  const [live, setLive] = useState<{ dx: number; dy: number } | null>(null);
  const releaseListeners = useRef<(() => void) | null>(null);
  useEffect(() => () => releaseListeners.current?.(), []);

  const placed = live ?? offset;
  const anchor = placed ?? anchorFor(placement ?? 'right');
  const tx = x + anchor.dx;
  const ty = y + anchor.dy;
  const anchorAttr = anchorAttrFor(anchor.dx);

  /**
   * The label's own press-drag-release (SRS-EDIT-053, SRS-EDIT-054).
   *
   * The surface's delegated mousedown starts a `label-move` whose release
   * commits an offset however far the pointer travelled, so a plain click
   * pinned the label — and pinning opts it out of lane allocation for good.
   * Owning the gesture here is what lets a press that never moved stay a
   * selection, which is the gesture a user reaches for first.
   */
  function beginGesture(e: ReactMouseEvent) {
    if (e.button !== 0) return; // right-click still reaches the context menu
    e.stopPropagation();
    const startX = e.clientX;
    const startY = e.clientY;
    const base = anchor;
    let moved = false;

    const move = (ev: MouseEvent) => {
      const dx = ev.clientX - startX;
      const dy = ev.clientY - startY;
      if (!moved && Math.hypot(dx, dy) < LABEL_DRAG_THRESHOLD) return;
      moved = true;
      setLive({ dx: base.dx + dx, dy: base.dy + dy });
    };
    const up = (ev: MouseEvent) => {
      releaseListeners.current?.();
      setLive(null);
      if (moved) {
        onPin({
          dx: Math.round(base.dx + ev.clientX - startX),
          dy: Math.round(base.dy + ev.clientY - startY),
        });
      } else {
        onSelect(ev.shiftKey || ev.metaKey || ev.ctrlKey);
      }
    };

    releaseListeners.current = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      releaseListeners.current = null;
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  }

  // The editor replaces the label rather than covering it: the field is 0.92
  // alpha and the old name ghosted through (SRS-EDIT-051). It is checked
  // before the hidden case below, so a milestone whose label the allocator
  // could not place is still renameable from the diamond.
  if (editing) {
    const w = Math.max(90, textWidth(text) + 24);
    const h = 18;
    const bx = anchorAttr === 'middle' ? tx - w / 2 : anchorAttr === 'end' ? tx - w + 4 : tx - 4;
    return (
      <InlineLabel text={text} x={bx} y={ty - 13} width={w} height={h} onCommit={onCommit} onCancel={onCancel} />
    );
  }

  if (!placed && (!placement || placement === 'hidden')) return null;

  // A label the user placed can end up anywhere, so it always keeps a leader;
  // the automatic vertical placements keep the short one they had.
  const needsLeader = placed != null || placement === 'above' || placement === 'below';
  const len = Math.hypot(anchor.dx, anchor.dy) || 1;
  const ux = anchor.dx / len;
  const uy = anchor.dy / len;
  const edge = SIZE + 1;
  const stop = Math.max(edge + 2, len - 4);

  return (
    <>
      {needsLeader && stop > edge + 2 && (
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
        fill={textColor || 'var(--color-text)'}
        style={{ cursor: 'move' }}
        onMouseDown={beginGesture}
        onDoubleClick={onBeginEdit}
      >
        {text}
      </text>
    </>
  );
}
