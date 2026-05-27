import { useEffect, useRef } from 'react';
import type { LucideIcon } from 'lucide-react';

export interface ContextMenuItem {
  id: string;
  label?: string;
  icon?: LucideIcon;
  shortcut?: string;
  onClick?: () => void;
  danger?: boolean;
  separator?: boolean;
  disabled?: boolean;
}

interface ContextMenuProps {
  x: number;
  y: number;
  items: ContextMenuItem[];
  onClose: () => void;
}

const MENU_W = 220;
const MENU_PAD = 6;

export function ContextMenu({ x, y, items, onClose }: ContextMenuProps) {
  const ref = useRef<HTMLDivElement | null>(null);

  const vw = typeof window !== 'undefined' ? window.innerWidth : 1024;
  const vh = typeof window !== 'undefined' ? window.innerHeight : 768;
  const estH = items.reduce((h, it) => h + (it.separator ? 9 : 28), MENU_PAD * 2);
  const left = Math.min(x, vw - MENU_W - 8);
  const top = Math.min(y, vh - estH - 8);

  useEffect(() => {
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) onClose();
    };
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('mousedown', onDown);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('mousedown', onDown);
      window.removeEventListener('keydown', onKey);
    };
  }, [onClose]);

  return (
    <div
      ref={ref}
      role="menu"
      onContextMenu={(e) => e.preventDefault()}
      style={{
        position: 'fixed', left, top, width: MENU_W, padding: MENU_PAD,
        background: 'var(--color-bg-elev)', border: '1px solid var(--color-border-hairline)',
        borderRadius: 'var(--radius-lg)', boxShadow: 'var(--shadow-3)', zIndex: 9999,
        fontSize: 12, color: 'var(--color-text)', userSelect: 'none',
      }}
    >
      {items.map((it) => {
        if (it.separator) {
          return <div key={it.id} style={{ height: 1, margin: '4px 6px', background: 'var(--color-border-hairline)' }} />;
        }
        const Icon = it.icon;
        return (
          <button
            key={it.id}
            role="menuitem"
            disabled={it.disabled}
            onClick={() => { it.onClick?.(); onClose(); }}
            style={{
              display: 'flex', alignItems: 'center', gap: 8, width: '100%', padding: '5px 8px',
              border: 'none', borderRadius: 'var(--radius)', background: 'transparent',
              color: it.danger ? '#ef4444' : 'inherit', textAlign: 'left',
              cursor: it.disabled ? 'not-allowed' : 'pointer', opacity: it.disabled ? 0.4 : 1, height: 26,
            }}
            onMouseEnter={(e) => { if (!it.disabled) e.currentTarget.style.background = 'var(--color-bg-subtle)'; }}
            onMouseLeave={(e) => { e.currentTarget.style.background = 'transparent'; }}
          >
            {Icon ? <Icon size={14} strokeWidth={1.75} /> : <span style={{ width: 14 }} />}
            <span style={{ flex: 1 }}>{it.label}</span>
            {it.shortcut && <kbd>{it.shortcut}</kbd>}
          </button>
        );
      })}
    </div>
  );
}
