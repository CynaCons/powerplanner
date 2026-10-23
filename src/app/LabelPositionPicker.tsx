import type { LabelPlacement } from '../types/document';

interface Props {
  current: LabelPlacement | undefined;
  onPick: (p: LabelPlacement | undefined) => void;
  /** What the label belongs to. The tiles draw it, so a task previews a bar
   *  and a milestone previews a diamond — a preview of the wrong shape is not
   *  a preview. A bar also has an interior, so tasks get `on-bar` as well. */
  kind: 'task' | 'milestone';
}

type Option = { value: LabelPlacement | undefined; name: string };

const OPTIONS: Option[] = [
  { value: undefined, name: 'Automatic' },
  { value: 'right', name: 'Right' },
  { value: 'above', name: 'Above' },
  { value: 'below', name: 'Below' },
  { value: 'left', name: 'Left' },
  { value: 'hidden', name: 'Hidden' },
];

const W = 42;
const H = 30;
const CX = W / 2;
const CY = H / 2;

/** Half-width of the previewed marker: a diamond is small, a bar is a span. */
const half = (kind: 'task' | 'milestone') => (kind === 'task' ? 10 : 4);

/** The marker the label is being positioned against. */
function Marker({ kind }: { kind: 'task' | 'milestone' }) {
  if (kind === 'task') {
    return <rect x={CX - 10} y={CY - 3} width={20} height={6} rx={1} fill="var(--color-text)" />;
  }
  return (
    <polygon
      points={`${CX},${CY - 4} ${CX + 4},${CY} ${CX},${CY + 4} ${CX - 4},${CY}`}
      fill="var(--color-text)"
    />
  );
}

/** A stand-in for the label text, so the tile shows a position, not a word. */
function Bar({ x, y, w = 14, dim = false }: { x: number; y: number; w?: number; dim?: boolean }) {
  return (
    <rect
      x={x}
      y={y}
      width={w}
      height={3}
      rx={1.5}
      fill="var(--color-text)"
      opacity={dim ? 0.3 : 0.75}
    />
  );
}

function Preview({ value, kind }: { value: LabelPlacement | undefined; kind: 'task' | 'milestone' }) {
  const hw = half(kind);
  return (
    <svg width={W} height={H} aria-hidden="true" style={{ display: 'block' }}>
      <Marker kind={kind} />
      {value === undefined && (
        <>
          {/* Automatic: it will take whichever of these is free. */}
          <Bar x={CX + hw + 3} y={CY - 1.5} w={11} dim />
          <Bar x={CX - 5.5} y={CY - 10} w={11} dim />
          <Bar x={CX - 5.5} y={CY + 7} w={11} dim />
        </>
      )}
      {value === 'right' && <Bar x={CX + hw + 3} y={CY - 1.5} />}
      {value === 'left' && <Bar x={CX - hw - 17} y={CY - 1.5} />}
      {value === 'above' && (
        <>
          <line x1={CX} y1={CY - 5} x2={CX} y2={CY - 8} stroke="var(--color-text-mute)" strokeWidth={1} />
          <Bar x={CX - 7} y={CY - 12} />
        </>
      )}
      {value === 'below' && (
        <>
          <line x1={CX} y1={CY + 5} x2={CX} y2={CY + 8} stroke="var(--color-text-mute)" strokeWidth={1} />
          <Bar x={CX - 7} y={CY + 9} />
        </>
      )}
      {value === 'on-bar' && (
        <rect x={CX - 7} y={CY - 1.5} width={14} height={3} rx={1.5} fill="var(--color-bg-elev)" />
      )}
      {value === 'hidden' && (
        <>
          {/* The label, struck out — a bare slash named nothing on its own. */}
          <Bar x={CX + hw + 3} y={CY - 1.5} dim />
          <line
            x1={CX + hw + 1}
            y1={CY + 4}
            x2={CX + hw + 19}
            y2={CY - 7}
            stroke="var(--color-text)"
            strokeWidth={1}
            opacity={0.65}
          />
        </>
      )}
    </svg>
  );
}

/**
 * Where a label sits, chosen by looking rather than by reading.
 *
 * The first version of this was six words in a menu — "Above", "Below",
 * "Left" — which name a position without showing one (owner report
 * 2026-10-23). Each tile now draws the result: the marker, the label, and for
 * the two that step off the line, the leader that ties them back.
 */
export function LabelPositionPicker({ current, onPick, kind }: Props) {
  const options: Option[] = kind === 'task'
    ? [OPTIONS[0], { value: 'on-bar', name: 'On the bar' }, ...OPTIONS.slice(1)]
    : OPTIONS;

  return (
    <div style={{ padding: '4px 8px 8px 8px' }}>
      <div style={{ fontSize: 10, color: 'var(--color-text-dim)', padding: '2px 0 6px 0' }}>
        Label position
      </div>
      <div role="radiogroup" aria-label="Label position" style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {options.map((o) => {
          const selected = current === o.value;
          return (
            <button
              key={o.value ?? 'auto'}
              role="radio"
              aria-checked={selected}
              aria-label={o.name}
              title={o.name}
              onClick={() => onPick(o.value)}
              style={{
                padding: 2,
                lineHeight: 0,
                cursor: 'pointer',
                background: selected ? 'var(--color-accent-soft)' : 'transparent',
                border: `1px solid ${selected ? 'var(--color-accent)' : 'var(--color-border-soft)'}`,
                borderRadius: 'var(--radius)',
              }}
            >
              <Preview value={o.value} kind={kind} />
            </button>
          );
        })}
      </div>
    </div>
  );
}
