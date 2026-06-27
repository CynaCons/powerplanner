import { useMemo, useState } from 'react';
import { App } from '../app/App';
import { useDocumentStore } from '../stores/documentStore';
import { insertGanttIntoSlide, readGanttFromSlide, isOfficeAvailable } from './officeBridge';
import { LayoutGrid, Download } from 'lucide-react';

declare const Office: {
  context?: {
    requirements?: {
      isSetSupported: (set: string, version: string) => boolean;
    };
  };
};
declare const PowerPoint: unknown;

function powerPointApiSupported(): boolean {
  return !!Office?.context?.requirements?.isSetSupported('PowerPointApi', '1.4');
}

interface TaskPaneAppProps {
  /** Set when mounted outside PowerPoint or before Office.onReady resolves. */
  hostHint?: string;
}

/**
 * Office task pane shell.
 *
 * Wraps the existing PowerPlanner editor and adds two extra controls at the
 * top: "Insert into slide" emits the current chart as native PowerPoint
 * shapes, and "Pull from slide" reads back a previously inserted chart.
 */
export function TaskPaneApp({ hostHint }: TaskPaneAppProps) {
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);
  const [status, setStatus] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const inOffice = isOfficeAvailable();
  const inBrowser = hostHint === 'browser' || !inOffice;
  const apiSupported = useMemo(() => (inOffice ? powerPointApiSupported() : false), [inOffice]);
  const bridgeReady = inOffice && apiSupported && typeof PowerPoint !== 'undefined';

  const onInsert = async () => {
    if (!bridgeReady) {
      setStatus('Insert requires PowerPoint with PowerPointApi 1.4 or newer.');
      return;
    }
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
    if (!bridgeReady) {
      setStatus('Pull requires PowerPoint with PowerPointApi 1.4 or newer.');
      return;
    }
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
    <div className="taskpane-shell" style={{ height: '100vh', display: 'flex', flexDirection: 'column' }}>
      {inBrowser && (
        <div className="taskpane-banner taskpane-banner--info">
          Browser preview — open via PowerPoint sideload to use Insert / Pull.
        </div>
      )}
      {inOffice && !apiSupported && (
        <div className="taskpane-banner taskpane-banner--warn">
          This PowerPoint build does not support PowerPointApi 1.4. Update Office to insert native shapes.
        </div>
      )}
      {inOffice && (
        <div
          style={{
            display: 'flex',
            gap: 8,
            alignItems: 'center',
            padding: '8px 12px',
            background: 'var(--color-bg-elev)',
            borderBottom: '1px solid var(--color-border-hairline)',
            fontSize: 12,
            flexWrap: 'wrap',
          }}
        >
          <button
            onClick={onInsert}
            disabled={busy || !bridgeReady}
            className="icon-btn"
            style={{
              background: 'var(--color-accent)',
              color: 'white',
              padding: '5px 10px',
              borderRadius: 6,
              border: 'none',
              cursor: busy || !bridgeReady ? 'not-allowed' : 'pointer',
              opacity: bridgeReady ? 1 : 0.55,
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
            disabled={busy || !bridgeReady}
            className="icon-btn"
            style={{
              padding: '5px 10px',
              borderRadius: 6,
              border: '1px solid var(--color-border-hairline)',
              cursor: busy || !bridgeReady ? 'not-allowed' : 'pointer',
              opacity: bridgeReady ? 1 : 0.55,
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
      <div style={{ flex: 1, minHeight: 0 }}>
        <App compact />
      </div>
    </div>
  );
}