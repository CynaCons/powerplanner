import { useEffect, useRef } from 'react';
import { X } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import type { ThemeName } from '../types/document';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

const THEMES: { id: ThemeName; label: string }[] = [
  { id: 'dark', label: 'Dark' },
  { id: 'light', label: 'Light' },
  { id: 'print', label: 'Print' },
];

/**
 * Document-level settings — theme and fiscal year start.
 *
 * These were two sections of the left inspector, permanently on screen for
 * settings you touch about once per document (PLAN.md v2.13.0 / C0b). They now
 * live with the other document-level actions in the File menu.
 *
 * Note what did NOT move here: the time scale. It was a duplicate of the tool
 * palette's control, rendered simultaneously, so it was deleted rather than
 * relocated.
 */
export function DocumentSettings({ open, onClose }: { open: boolean; onClose: () => void }) {
  const fiscalYearStart = useDocumentStore((s) => s.doc.calendar.fiscalYearStart);
  const setFiscalYearStart = useDocumentStore((s) => s.setFiscalYearStart);
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const setTheme = useDocumentStore((s) => s.setTheme);
  const closeRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (!open) return;
    closeRef.current?.focus();
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div
      className="doc-settings__scrim"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <div className="doc-settings" role="dialog" aria-modal="true" aria-label="Document settings">
        <header className="doc-settings__head">
          <h2 className="doc-settings__title">Document settings</h2>
          <button ref={closeRef} onClick={onClose} title="Close" className="icon-btn">
            <X size={14} />
          </button>
        </header>

        <div className="doc-settings__field">
          <label htmlFor="doc-theme">Theme</label>
          <div id="doc-theme" role="radiogroup" aria-label="Theme" className="doc-settings__segmented">
            {THEMES.map((t) => (
              <button
                key={t.id}
                role="radio"
                aria-checked={theme === t.id}
                onClick={() => setTheme(t.id)}
                className={theme === t.id ? 'is-active' : ''}
              >
                {t.label}
              </button>
            ))}
          </div>
        </div>

        <div className="doc-settings__field">
          <label htmlFor="doc-fy">Fiscal year starts</label>
          <select
            id="doc-fy"
            value={fiscalYearStart}
            onChange={(e) => setFiscalYearStart(Number(e.target.value))}
          >
            {MONTHS.map((label, i) => (
              <option key={label} value={i + 1}>
                {label}
              </option>
            ))}
          </select>
        </div>
      </div>
    </div>
  );
}
