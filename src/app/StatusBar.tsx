import { useEffect, useMemo } from 'react';
import { useDocumentStore, useIsDirty } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { getCurrentHandle } from '../persistence/fileIo';
import { computeDocStats } from '../utils/docStats';
import { APP_VERSION } from '../version';
import { Notices } from './Notices';

/**
 * What the footer says about the file (SRS-PERS-029).
 *
 * Two facts, and both were missing: which file a Ctrl+S would write, and
 * whether what is on screen has reached it. The binding lives in `fileIo` as
 * module state and the dirty flag was read only by the New and Open guards, so
 * the app knew both and said neither — "I am missing the information about
 * which file I am connected to and whether I am connected to a file or not."
 *
 * Kept pure and separate from the component because the window title has to
 * state the same thing (below), and two places phrasing it independently is
 * how they come to disagree.
 */
export function fileBindingLabel(
  name: string | null,
  dirty: boolean,
): { text: string; title: string; unsaved: boolean } {
  if (!name) {
    return {
      text: dirty ? 'No file · unsaved' : 'No file',
      title: 'This plan is in no file. Ctrl+S picks one and writes it there.',
      unsaved: dirty,
    };
  }
  return {
    text: dirty ? `${name} · unsaved` : name,
    title: dirty ? `Unsaved changes. Ctrl+S writes them to ${name}.` : `Saved to ${name}`,
    unsaved: dirty,
  };
}

/** The same two facts, where a taskbar entry or a tab shows them. */
export function windowTitle(name: string | null, docTitle: string, dirty: boolean): string {
  const subject = name ?? (docTitle.trim() || 'Untitled roadmap');
  return `${dirty ? '• ' : ''}${subject} — PowerRoadmap`;
}

/**
 * The name of the bound file, read at render.
 *
 * `getCurrentHandle` is not reactive, and it does not need to be: every event
 * that changes the binding — an open, a save, a New that detaches it — also
 * writes the document store this component already subscribes to, so the read
 * happens on the render that follows. The handle's `name` is not on the
 * narrow interface `fileIo` declares for it (that interface covers only what
 * the writer calls), so it is read off the real `FileSystemFileHandle`.
 */
function boundFileName(): string | null {
  const handle = getCurrentHandle() as { name?: string } | null;
  return handle?.name ?? null;
}

export function StatusBar() {
  const doc = useDocumentStore((s) => s.doc);
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const agentActive = useDocumentStore((s) => s.agentActive);
  const dirty = useIsDirty();
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

  const fileName = boundFileName();
  const binding = fileBindingLabel(fileName, dirty);

  // The title is set from here because this is the one component that holds
  // both facts; it is a document-level effect either way, and duplicating the
  // binding read in App would give the two surfaces separate answers.
  useEffect(() => {
    document.title = windowTitle(fileName, doc.title, dirty);
  }, [fileName, doc.title, dirty]);

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
        {/* Which file, and whether it holds what is on screen (SRS-PERS-029). */}
        <span
          className={`app-status-file ${binding.unsaved ? 'is-unsaved' : ''}`}
          title={binding.title}
        >
          {binding.text}
        </span>
        <span>
          scale: {scale} · {pxPerDay.toFixed(2)} px/day · {selectedCount} selected
        </span>
        {/* A released single-file HTML must say which build it is. */}
        <span className="app-status-version">v{APP_VERSION}</span>
      </div>
    </footer>
  );
}
