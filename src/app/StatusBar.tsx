import { useMemo } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { computeDocStats } from '../utils/docStats';
import { APP_VERSION } from '../version';
import { Notices } from './Notices';

export function StatusBar() {
  const doc = useDocumentStore((s) => s.doc);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const agentActive = useDocumentStore((s) => s.agentActive);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const startDate = useViewportStore((s) => s.startDate);
  const selection = useSelectionStore((s) => s.items);
  // Selecting a bar also selects the row it sits on, so the gutter highlights
  // with it. That companion row is not a second thing the user picked, so it
  // is not counted here.
  const selectedCount = selection.some((i) => i.kind !== 'row')
    ? selection.filter((i) => i.kind !== 'row').length
    : selection.length;

  // Absorbed from the inspector's Statistics section (PLAN.md v2.13.0 / C0a) —
  // read-only telemetry belongs in the status bar, not in a 320px column.
  const stats = useMemo(() => computeDocStats(doc), [doc]);

  return (
    <footer className="app-status">
      <div title={stats.min && stats.max ? `${stats.min} → ${stats.max}` : undefined}>
        {stats.tasks} tasks · {stats.milestones} milestones · {stats.spanLabel} span ·{' '}
        {stats.avgPercent}% complete · view starts {startDate}
      </div>
      <div className="app-status-right">
        {/* What the app has just done. First, because it is the newest fact
            on screen and the only one that is news (SRS-PERS-025). */}
        <Notices />
        {/*
          While a bridge session is attached the user must always be able to
          explain why the document is changing (SRS-ABR-060).
        */}
        {agentActive && (
          <span className="agent-indicator" role="status">
            <span className="agent-indicator-dot" aria-hidden="true" />
            agent connected
          </span>
        )}
        <span>
          scale: {scale} · {pxPerDay.toFixed(2)} px/day · {selectedCount} selected
        </span>
        {/* A released single-file HTML must say which build it is. */}
        <span className="app-status-version">v{APP_VERSION}</span>
      </div>
    </footer>
  );
}
