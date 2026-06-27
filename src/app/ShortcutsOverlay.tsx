import { useEffect } from 'react';
import { X } from 'lucide-react';

interface Shortcut { keys: string[]; label: string; }
interface Group { title: string; items: Shortcut[]; }

const GROUPS: Group[] = [
  {
    title: 'Tools',
    items: [
      { keys: ['V'], label: 'Select' },
      { keys: ['T'], label: 'Add task' },
      { keys: ['Y'], label: 'Add milestone' },
      { keys: ['H'], label: 'Pan' },
      { keys: ['R'], label: 'Marquee' },
      { keys: ['Esc'], label: 'Revert to Select' },
    ],
  },
  {
    title: 'Scale',
    items: [
      { keys: ['⇧', 'D'], label: 'Day' },
      { keys: ['⇧', 'W'], label: 'Week' },
      { keys: ['⇧', 'M'], label: 'Month' },
      { keys: ['⇧', 'Q'], label: 'Quarter' },
      { keys: ['⇧', 'Y'], label: 'Year' },
    ],
  },
  {
    title: 'Selection',
    items: [
      { keys: ['Click'], label: 'Select item' },
      { keys: ['⇧', 'Click'], label: 'Add to selection' },
      { keys: ['Drag'], label: 'Move (on bar)' },
      { keys: ['Esc'], label: 'Clear selection' },
    ],
  },
  {
    title: 'Editing',
    items: [
      { keys: ['⌘', 'Z'], label: 'Undo' },
      { keys: ['⌘', '⇧', 'Z'], label: 'Redo' },
      { keys: ['Del'], label: 'Delete selection' },
      { keys: ['F2'], label: 'Rename' },
      { keys: ['←', '→'], label: 'Nudge by day (⇧ = 7 days)' },
      { keys: ['S'], label: 'Toggle snap' },
    ],
  },
  {
    title: 'Navigation',
    items: [
      { keys: ['Wheel'], label: 'Zoom at cursor' },
      { keys: ['⇧', 'Wheel'], label: 'Pan horizontally' },
      { keys: ['Drag bg'], label: 'Pan view (default tool)' },
      { keys: ['+', '−'], label: 'Zoom in / out' },
      { keys: ['Home'], label: 'Fit to data' },
    ],
  },
  {
    title: 'File',
    items: [
      { keys: ['⌘', 'S'], label: 'Save (embeds chart in HTML)' },
      { keys: ['?'], label: 'Show shortcuts' },
    ],
  },
];

interface ShortcutsOverlayProps { open: boolean; onClose: () => void; }

export function ShortcutsOverlay({ open, onClose }: ShortcutsOverlayProps) {
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="Keyboard shortcuts"
      onClick={onClose}
      style={{
        position: 'fixed', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: 'var(--color-overlay-scrim)', backdropFilter: 'blur(8px)', WebkitBackdropFilter: 'blur(8px)',
        zIndex: 9999,
      }}
    >
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          width: 'min(720px, 92vw)', maxHeight: '86vh', overflowY: 'auto',
          background: 'var(--color-bg-elev)', border: '1px solid var(--color-border-hairline)',
          borderRadius: 'var(--radius-lg)', boxShadow: 'var(--shadow-3)', padding: 22,
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 18 }}>
          <h2 style={{ margin: 0, fontSize: 15, fontWeight: 600, letterSpacing: '-0.2px' }}>
            Keyboard shortcuts
          </h2>
          <button
            onClick={onClose}
            aria-label="Close"
            style={{ width: 28, height: 28, padding: 0, display: 'inline-flex', alignItems: 'center', justifyContent: 'center', color: 'var(--color-text-soft)' }}
          >
            <X size={16} strokeWidth={1.75} />
          </button>
        </div>
        <div
          style={{
            display: 'grid',
            gridTemplateColumns: 'repeat(auto-fill, minmax(220px, 1fr))',
            gap: '18px 28px',
          }}
        >
          {GROUPS.map((g) => (
            <section key={g.title}>
              <h3 style={{ margin: '0 0 8px', fontSize: 10.5, fontWeight: 600, letterSpacing: 0.8, textTransform: 'uppercase', color: 'var(--color-text-dim)' }}>
                {g.title}
              </h3>
              <ul style={{ listStyle: 'none', margin: 0, padding: 0 }}>
                {g.items.map((s) => (
                  <li key={s.label} style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12, padding: '4px 0', fontSize: 12, color: 'var(--color-text-soft)' }}>
                    <span>{s.label}</span>
                    <span style={{ display: 'inline-flex', gap: 3 }}>
                      {s.keys.map((k, i) => (
                        <kbd key={i}>{k}</kbd>
                      ))}
                    </span>
                  </li>
                ))}
              </ul>
            </section>
          ))}
        </div>
      </div>
    </div>
  );
}
