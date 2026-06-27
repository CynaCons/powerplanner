import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { TaskPaneApp } from './TaskPaneApp';
import '../styles/global.css';

declare const Office: {
  onReady: (cb: (info: { host: unknown; platform: unknown }) => void) => void;
};

const OFFICE_READY_TIMEOUT_MS = 3000;

function mountTaskPane(hostHint?: string) {
  const rootEl = document.getElementById('root');
  if (!rootEl) return;
  createRoot(rootEl).render(
    <StrictMode>
      <TaskPaneApp hostHint={hostHint} />
    </StrictMode>,
  );
}

/**
 * PowerPlanner task pane entry point.
 *
 * Office.js MUST load + signal ready before bridge calls work inside PowerPoint.
 * When opened in a normal browser (or Office is slow), fall back after a short
 * timeout so UI development is not blocked by a blank pane.
 */
if (typeof Office !== 'undefined' && typeof Office.onReady === 'function') {
  let mounted = false;
  const mountOnce = (hostHint?: string) => {
    if (mounted) return;
    mounted = true;
    mountTaskPane(hostHint);
  };

  const timeout = window.setTimeout(() => {
    mountOnce('browser');
  }, OFFICE_READY_TIMEOUT_MS);

  Office.onReady((info) => {
    window.clearTimeout(timeout);
    const host = typeof info.host === 'string' ? info.host : undefined;
    mountOnce(host);
  });
} else {
  mountTaskPane('browser');
}