import { useIsSelected } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';

interface Props {
  layout: LayoutResult;
}

/**
 * Dependency connectors.
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
 */
export function Dependencies({ layout }: Props) {
  const isSelected = useIsSelected();

  return (
    <g>
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
          <g key={ld.dependency.id}>
            <path
              d={ld.path}
              fill="none"
              stroke={selected ? 'var(--color-accent)' : 'var(--color-text-soft)'}
              strokeWidth={selected ? 2 : 1.2}
              strokeLinejoin="round"
              markerEnd={selected ? 'url(#pp-arrow-sel)' : 'url(#pp-arrow)'}
              opacity={selected ? 1 : 0.8}
            />
            <path
              data-pp-kind="dependency"
              data-pp-id={ld.dependency.id}
              d={ld.path}
              fill="none"
              stroke="transparent"
              strokeWidth={10}
              style={{ cursor: 'pointer' }}
            />
          </g>
        );
      })}
    </g>
  );
}
