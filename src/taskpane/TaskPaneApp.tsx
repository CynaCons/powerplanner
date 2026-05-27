import { useState } from 'react';
import { App } from '../app/App';
import { useDocumentStore } from '../stores/documentStore';
import { insertGanttIntoSlide, readGanttFromSlide, isOfficeAvailable } from './officeBridge';
import { LayoutGrid, Download } from 'lucide-react';

/**
 * Office task pane shell.
 *
 * Wraps the existing PowerPlanner editor and adds two extra controls at the
 * top: "Insert into slide" emits the current chart as native PowerPoint
 * shapes, and "Pull from slide" reads back a previously inserted chart.
 */
export function TaskPaneApp() {
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);
  const [status, setStatus] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const onInsert = async () => {
    setBusy(true);
    setStatus(null);
    try {
      const result = await insertGanttIntoSlide(doc);
      setStatus(`Inserted ${result.shapeCount} shapes into slide.`);
    } catch (err) {
      setStatus(`Insert failed: ${(err as Error).message}`);
    } finally {
      setBusy(false);
    }
  };

  const onPull = async () => {
    setBusy(true);
    setStatus(null);
    try {
      const next = await readGanttFromSlide();
      if (!next) {
        setStatus('No PowerPlanner chart found on the active slide.');
      } else {
        setDocument(next);
        setStatus(`Loaded "${next.title}" from slide.`);
      }
    } catch (err) {
      setStatus(`Pull failed: ${(err as Error).message}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div style={{ height: '100vh', display: 'flex', flexDirection: 'column' }}>
      {/* Office-specific top bar */}
      {isOfficeAvailable() && (
        <div
          style={{
            display: 'flex',
            gap: 8,
            alignItems: 'center',
            padding: '8px 12px',
            background: 'var(--color-bg-elev)',
            borderBottom: '1px solid var(--color-border-hairline)',
            fontSize: 12,
          }}
        >
          <button
            onClick={onInsert}
            disabled={busy}
            className="icon-btn"
            style={{
              background: 'var(--color-accent)',
              color: 'white',
              padding: '5px 10px',
              borderRadius: 6,
              border: 'none',
              cursor: busy ? 'wait' : 'pointer',
              display: 'inline-flex',
              alignItems: 'center',
              gap: 6,
            }}
          >
            <LayoutGrid size={13} strokeWidth={1.75} />
            Insert into slide
          </button>
          <button
            onClick={onPull}
            disabled={busy}
            className="icon-btn"
            style={{
              padding: '5px 10px',
              borderRadius: 6,
              border: '1px solid var(--color-border-hairline)',
              cursor: busy ? 'wait' : 'pointer',
              display: 'inline-flex',
              alignItems: 'center',
              gap: 6,
            }}
          >
            <Download size={13} strokeWidth={1.75} />
            Pull from slide
          </button>
          {status && (
            <span style={{ color: 'var(--color-text-soft)', marginLeft: 'auto', fontSize: 11 }}>{status}</span>
          )}
        </div>
      )}
      {/* Main editor */}
      <div style={{ flex: 1, minHeight: 0 }}>
        <App />
      </div>
    </div>
  );
}
