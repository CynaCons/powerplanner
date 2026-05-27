import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { TaskPaneApp } from './TaskPaneApp';
import '../styles/global.css';

declare const Office: {
  onReady: (cb: (info: { host: unknown; platform: unknown }) => void) => void;
};

/**
 * PowerPlanner task pane entry point.
 *
 * Office.js MUST load + signal ready before we mount the React tree.
 * Otherwise the bridge calls (PowerPoint.run, Office.context) won't work.
 */
Office.onReady(() => {
  const rootEl = document.getElementById('root');
  if (!rootEl) return;
  createRoot(rootEl).render(
    <StrictMode>
      <TaskPaneApp />
    </StrictMode>,
  );
});
