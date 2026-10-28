import { useCallback, useEffect, useMemo } from 'react';
import { X } from 'lucide-react';
import { KEYMAP, GESTURES, chordKeys, type BindingGroup } from './keymap';
import { useViewStore } from '../stores/viewStore';

/**
 * The shortcuts sheet, rendered FROM the keymap (SRS-EDIT-041).
 *
 * It used to be a hand-maintained copy of the same facts. The copy drifted:
 * it taught ⇧M → Month, which the bare-'m' insert branch made unreachable; it
 * taught "Wheel → Zoom at cursor", which stopped being true when the wheel
 * became a scroll; and it had no row at all for N, M or B, the three bare
 * letters that mutate the document. There is now nothing to keep in step.
 */

const ORDER: BindingGroup[] = ['Tools', 'Insert', 'Scale', 'Editing', 'Navigation', 'File', 'Help'];

export function ShortcutsOverlay() {
  const open = useViewStore((v) => v.shortcutsOpen);
  const setOpen = useViewStore((v) => v.setShortcutsOpen);
  const onClose = useCallback(() => setOpen(false), [setOpen]);

  const groups = useMemo(() => {
    const byGroup = new Map<BindingGroup, { keys: string[]; label: string }[]>();
    for (const b of KEYMAP) {
      const rows = byGroup.get(b.group) ?? [];
      rows.push({ keys: chordKeys(b.chord), label: b.label });
      byGroup.set(b.group, rows);
    }
    const out = ORDER.filter((g) => byGroup.has(g)).map((g) => ({ title: g, items: byGroup.get(g)! }));
    out.push({ title: 'Pointer' as BindingGroup, items: [...GESTURES] });
    return out;
  }, []);

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
          {groups.map((g) => (
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
