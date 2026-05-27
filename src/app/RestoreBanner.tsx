import { X } from 'lucide-react';

interface Props {
  onRestore: () => void;
  onDismiss: () => void;
}

export function RestoreBanner({ onRestore, onDismiss }: Props) {
  return (
    <div
      style={{
        position: 'absolute',
        top: 8,
        left: '50%',
        transform: 'translateX(-50%)',
        background: 'var(--color-bg-elev)',
        border: '1px solid var(--color-border)',
        borderRadius: 6,
        padding: '8px 10px 8px 14px',
        display: 'flex',
        alignItems: 'center',
        gap: 12,
        boxShadow: '0 4px 16px rgba(0,0,0,0.3)',
        zIndex: 100,
        fontSize: 12,
        color: 'var(--color-text)',
      }}
      role="alert"
    >
      <span>An unsaved session from your last visit is available.</span>
      <button onClick={onRestore} className="primary" style={{ padding: '3px 10px', fontSize: 12 }}>
        Restore
      </button>
      <button
        onClick={onDismiss}
        aria-label="Dismiss"
        style={{
          padding: 2,
          background: 'transparent',
          border: 'none',
          color: 'var(--color-text-soft)',
          cursor: 'pointer',
          display: 'inline-flex',
          alignItems: 'center',
        }}
      >
        <X size={14} />
      </button>
    </div>
  );
}
