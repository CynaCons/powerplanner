import { useIsSelected } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';

interface Props {
  layout: LayoutResult;
}

/**
 * Dependency connectors — the ink.
 *
 * The head is an `orient="auto"` marker, so it points wherever the path's last
 * segment points — which is why a backwards final leg used to draw an arrow
 * aimed at the predecessor. The direction is the engine's job now
 * (SRS-ELEM-042); this file only has to make sure the head is drawn, and drawn
 * in the same ink as the line it ends.
 *
 * Two marker ids rather than one: a selected connector is accented, and a
 * marker keeps the fill it was defined with, so an accented line with the soft
 * head would end in a grey point.
 *
 * Nothing here takes a press. The ink is painted over the bars, because a
 * connector that disappeared behind every bar it crossed would not read as a
 * schedule; the press targets are `DependencyHits`, painted UNDER the bars
 * (SRS-EDIT-077).
 */
export function Dependencies({ layout }: Props) {
  const isSelected = useIsSelected();

  return (
    <g pointerEvents="none">
      <defs>
        <marker id="pp-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
          <path d="M0,0 L10,5 L0,10 Z" fill="var(--color-text-soft)" />
        </marker>
        <marker id="pp-arrow-sel" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
          <path d="M0,0 L10,5 L0,10 Z" fill="var(--color-accent)" />
        </marker>
      </defs>
      {layout.dependencies.map((ld) => {
        const selected = isSelected(ld.dependency.id);
        return (
          <path
            key={ld.dependency.id}
            d={ld.path}
            fill="none"
            stroke={selected ? 'var(--color-accent)' : 'var(--color-text-soft)'}
            strokeWidth={selected ? 2 : 1.2}
            strokeLinejoin="round"
            markerEnd={selected ? 'url(#pp-arrow-sel)' : 'url(#pp-arrow)'}
            opacity={selected ? 1 : 0.8}
          />
        );
      })}
    </g>
  );
}

/**
 * Dependency connectors — the press targets (SRS-EDIT-077).
 *
 * A 1.2px line is not something a pointer can find, so every connector carries
 * a transparent 10px stroke along its whole length. That stroke used to be
 * painted with the ink, i.e. after the bars, which made it the topmost
 * hit-testable element wherever a connector crossed a bar. `resolveHit` ranks
 * the stack and so the PRESS still landed on the bar — but nothing else
 * consults that ranking. The cursor came from the topmost element, so a bar
 * asking for `move` showed `pointer`; `.task-bar-body:hover` never fired, and
 * because the grips, anchors and progress handle are gated on hover, a bar's
 * whole affordance set blinked out as the pointer crossed a connector.
 *
 * This group is rendered before the bars instead. Paint order is SVG's only
 * stacking lever, and it now says what `hitRank` says: every element that
 * outranks a connector is also above it. The connector keeps the whole of its
 * length that no bar covers, which is nearly all of it.
 */
export function DependencyHits({ layout }: Props) {
  return (
    <g>
      {layout.dependencies.map((ld) => (
        <path
          key={ld.dependency.id}
          data-pp-kind="dependency"
          data-pp-id={ld.dependency.id}
          d={ld.path}
          fill="none"
          stroke="transparent"
          strokeWidth={10}
          style={{ cursor: 'pointer' }}
        />
      ))}
    </g>
  );
}
