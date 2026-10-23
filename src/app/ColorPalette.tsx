import { useRef } from 'react';
import { Plus } from 'lucide-react';

interface ColorPaletteProps {
  value: string;
  onChange: (color: string) => void;
}

// The stream palette, one hue per discipline (DECISIONS.md D6). These used to
// be a different, softer set left over from before the 2026-10-23 rebuild, so
// picking a colour by hand produced one that no bar in the chart used.
const PRESETS: { name: string; hex: string }[] = [
  { name: 'Prussian', hex: '#1F4E79' },
  { name: 'Teal', hex: '#17736B' },
  { name: 'Moss', hex: '#4F7A2A' },
  { name: 'Rust', hex: '#A8452B' },
  { name: 'Plum', hex: '#6B3F7A' },
  { name: 'Amber', hex: '#C07A14' },
  { name: 'Steel', hex: '#6E8299' },
  { name: 'Graphite', hex: '#3D4551' },
];

function eqHex(a: string, b: string): boolean {
  return (a || '').trim().toLowerCase() === (b || '').trim().toLowerCase();
}

export function ColorPalette({ value, onChange }: ColorPaletteProps) {
  const customRef = useRef<HTMLInputElement | null>(null);
  const isPreset = PRESETS.some((p) => eqHex(p.hex, value));

  return (
    <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', position: 'relative' }}>
      {PRESETS.map((p) => {
        const active = eqHex(p.hex, value);
        return (
          <button
            key={p.hex}
            type="button"
            title={p.name}
            aria-label={p.name}
            onClick={() => onChange(p.hex)}
            style={{
              width: 22, height: 22, padding: 0, borderRadius: '50%',
              background: p.hex, border: '1px solid var(--color-border-hairline)',
              boxShadow: active
                ? `0 0 0 2px var(--color-bg-panel), 0 0 0 4px var(--color-accent)`
                : 'inset 0 1px 0 rgba(255, 255, 255, 0.12)',
              cursor: 'pointer', transition: 'transform 100ms ease',
            }}
            onMouseEnter={(e) => (e.currentTarget.style.transform = 'scale(1.08)')}
            onMouseLeave={(e) => (e.currentTarget.style.transform = 'scale(1)')}
          />
        );
      })}
      <button
        type="button"
        title="Custom color"
        aria-label="Custom color"
        onClick={() => customRef.current?.click()}
        style={{
          width: 22, height: 22, padding: 0, borderRadius: '50%',
          background: isPreset ? 'var(--color-bg-subtle)' : value,
          border: '1px dashed var(--color-border)', color: 'var(--color-text-soft)',
          display: 'inline-flex', alignItems: 'center', justifyContent: 'center', cursor: 'pointer',
          boxShadow: !isPreset
            ? `0 0 0 2px var(--color-bg-panel), 0 0 0 4px var(--color-accent)`
            : 'none',
        }}
      >
        {isPreset && <Plus size={12} strokeWidth={2} />}
      </button>
      <input
        ref={customRef}
        type="color"
        value={value || '#6366f1'}
        onChange={(e) => onChange(e.target.value)}
        style={{ position: 'absolute', width: 0, height: 0, opacity: 0, pointerEvents: 'none' }}
      />
    </div>
  );
}
