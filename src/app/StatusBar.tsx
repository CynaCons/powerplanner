import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';

export function StatusBar() {
  const tasks = useDocumentStore((s) => s.doc.tasks.length);
  const milestones = useDocumentStore((s) => s.doc.milestones.length);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const agentActive = useDocumentStore((s) => s.agentActive);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const startDate = useViewportStore((s) => s.startDate);
  const selection = useSelectionStore((s) => s.items);

  return (
    <footer className="app-status">
      <div>
        {tasks} tasks · {milestones} milestones · view starts {startDate}
      </div>
      <div className="app-status-right">
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
          scale: {scale} · {pxPerDay.toFixed(2)} px/day · {selection.length} selected
        </span>
      </div>
    </footer>
  );
}
