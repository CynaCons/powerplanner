import { useSelectionStore } from '../stores/selectionStore';
import type { LayoutResult } from '../layout/engine';

interface Props {
  layout: LayoutResult;
}

export function Dependencies({ layout }: Props) {
  const isSelected = useSelectionStore((s) => s.isSelected);

  return (
    <g>
      <defs>
        <marker id="pp-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
          <path d="M0,0 L10,5 L0,10 Z" fill="var(--color-text-soft)" />
        </marker>
      </defs>
      {layout.dependencies.map((ld) => {
        const selected = isSelected(ld.dependency.id);
        return (
          <g key={ld.dependency.id}>
            <path
              d={ld.path}
              fill="none"
              stroke="var(--color-text-soft)"
              strokeWidth={selected ? 2 : 1.2}
              markerEnd="url(#pp-arrow)"
              opacity={0.8}
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
