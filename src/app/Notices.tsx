import { createPortal } from 'react-dom';
import { X } from 'lucide-react';
import { useNoticeStore } from '../stores/noticeStore';

/**
 * The single owner of what the app has just said (SRS-PERS-025).
 *
 * Two surfaces, one source. The status bar carries a polite `role="status"`
 * region — the only announced copy — and the toast stack is its visual echo,
 * `aria-hidden` so a screen reader does not read every message twice. This is
 * read-only telemetry, which is exactly what the status bar is for
 * (DECISIONS.md D10 keeps actions out of it), so a notice is text and a
 * dismiss, never a button that edits the document.
 */
export function Notices() {
  const items = useNoticeStore((s) => s.items);
  const dismiss = useNoticeStore((s) => s.dismiss);
  const newest = items[items.length - 1];

  return (
    <>
      <span className="notice-status" role="status">
        {newest ? label(newest.text, newest.count) : ''}
      </span>
      {items.length > 0 &&
        createPortal(
          <div className="notice-stack" aria-hidden="true">
            {items.map((n) => (
              <div key={n.id} className={`notice notice--${n.tone}`}>
                <span>{label(n.text, n.count)}</span>
                <button type="button" aria-label="Dismiss notice" onClick={() => dismiss(n.id)}>
                  <X size={13} strokeWidth={2} />
                </button>
              </div>
            ))}
          </div>,
          document.body,
        )}
    </>
  );
}

function label(text: string, count: number): string {
  return count > 1 ? `${text} (×${count})` : text;
}
