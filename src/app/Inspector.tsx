import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { TimeScale, ThemeName } from '../types/document';
import { SelectionInspector } from './SelectionInspector';

const SCALES: TimeScale[] = ['day', 'week', 'month', 'quarter', 'year'];
const THEMES: ThemeName[] = ['dark', 'light', 'print'];

export function Inspector() {
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const setScale = useDocumentStore((s) => s.setScale);
  const fiscalYearStart = useDocumentStore((s) => s.doc.calendar.fiscalYearStart);
  const setFiscalYearStart = useDocumentStore((s) => s.setFiscalYearStart);
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const setTheme = useDocumentStore((s) => s.setTheme);
  const selection = useSelectionStore((s) => s.items);

  return (
    <aside className="inspector">
      {selection.length > 0 ? (
        <SelectionInspector />
      ) : (
        <>
          <h3>Document</h3>
          <div className="field">
            <label>Time scale</label>
            <select value={scale} onChange={(e) => setScale(e.target.value as TimeScale)}>
              {SCALES.map((s) => (
                <option key={s} value={s}>
                  {s[0].toUpperCase() + s.slice(1)}
                </option>
              ))}
            </select>
          </div>
          <div className="field">
            <label>Fiscal year starts</label>
            <select value={fiscalYearStart} onChange={(e) => setFiscalYearStart(Number(e.target.value))}>
              {Array.from({ length: 12 }, (_, i) => i + 1).map((m) => (
                <option key={m} value={m}>
                  {monthName(m)}
                </option>
              ))}
            </select>
          </div>
          <hr />
          <h3>Style</h3>
          <div className="field">
            <label>Theme</label>
            <select value={theme} onChange={(e) => setTheme(e.target.value as ThemeName)}>
              {THEMES.map((t) => (
                <option key={t} value={t}>
                  {t[0].toUpperCase() + t.slice(1)}
                </option>
              ))}
            </select>
          </div>
          <hr />
          <p style={{ fontSize: 11, color: 'var(--color-text-dim)' }}>
            Select a task, milestone or bracket to edit it.
          </p>
        </>
      )}
    </aside>
  );
}

function monthName(m: number): string {
  return ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][m - 1];
}
