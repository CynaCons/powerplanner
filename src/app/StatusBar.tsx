import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';

export function StatusBar() {
  const tasks = useDocumentStore((s) => s.doc.tasks.length);
  const milestones = useDocumentStore((s) => s.doc.milestones.length);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const startDate = useViewportStore((s) => s.startDate);
  const selection = useSelectionStore((s) => s.items);

  return (
    <footer className="app-status">
      <div>
        {tasks} tasks · {milestones} milestones · view starts {startDate}
      </div>
      <div>
        scale: {scale} · {pxPerDay.toFixed(2)} px/day · {selection.length} selected
      </div>
    </footer>
  );
}
