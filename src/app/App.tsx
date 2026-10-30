import { useEffect, useRef, useState } from 'react';
import { ChartArea } from './ChartArea';
import { Header } from './Header';
import { StatusBar } from './StatusBar';
import { RestoreBanner } from './RestoreBanner';
import { CommandBar } from './CommandBar';
import { ChartContextMenu } from './ChartContextMenu';
import { ShortcutsOverlay } from './ShortcutsOverlay';
import { CommandPalette } from './CommandPalette';
import { NewRoadmapDialog } from './NewRoadmapDialog';
import { DetailsDialog } from './DetailsDialog';
import { useDocumentStore } from '../stores/documentStore';
import { useKeyboardShortcuts } from './useKeyboardShortcuts';
import { readEmbeddedDocument } from '../persistence/embedded';
import { autosaveToFile, restoreBinding } from '../persistence/fileIo';
import { loadAutosave, saveAutosave, clearAutosave, shouldAutosave } from '../persistence/autosave';
import { notify } from '../stores/noticeStore';
import type { GanttDocument } from '../types/document';

interface AppProps {
  /** Chart-first layout for narrow Office task panes. */
  compact?: boolean;
}

export function App({ compact = false }: AppProps) {
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);
  const replaceDocument = useDocumentStore((s) => s.replaceDocument);
  const didHydrate = useRef(false);
  const [pendingRestore, setPendingRestore] = useState<GanttDocument | null>(null);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  // Hydrate from embedded data, or offer to restore autosave non-blockingly
  useEffect(() => {
    if (didHydrate.current) return;
    didHydrate.current = true;
    // Take back the file a previous session was bound to. This is async and
    // lands after mount, which is why the binding is a store rather than a
    // module variable the footer reads at render (SRS-PERS-035).
    void restoreBinding();
    const embedded = readEmbeddedDocument();
    if (embedded) {
      setDocument(embedded);
      return;
    }
    const auto = loadAutosave();
    if (auto) {
      setPendingRestore(auto);
    }
  }, [setDocument]);

  // Write the plan back to its own file, where the browser has already said
  // we may (SRS-PERS-037). Never prompts: a permission dialog needs a gesture
  // this timer does not have, so an unpermitted file just stays stale and the
  // footer says so.
  useEffect(() => {
    const t = setTimeout(() => {
      const before = useDocumentStore.getState().revision;
      void autosaveToFile(doc).then((written) => {
        // Clean only if nothing was edited while the write was in flight.
        if (written && useDocumentStore.getState().revision === before) {
          useDocumentStore.getState().markSaved();
        }
      });
    }, 1200);
    return () => clearTimeout(t);
  }, [doc]);

  // Autosave debounced
  useEffect(() => {
    const t = setTimeout(() => {
      const { revision, savedRevision } = useDocumentStore.getState();
      const untouched = revision === savedRevision;
      // The snapshot the restore banner is still offering is not ours to
      // overwrite (SRS-PERS-030).
      if (!shouldAutosave({ offerPending: pendingRestore !== null, dirty: !untouched })) return;
      if (saveAutosave(doc)) return;
      // Only once there is work to lose. The portable build is opened from
      // disk, where some browsers refuse local storage outright, and warning
      // about a backup of an untouched sample is a false alarm on every open.
      if (untouched) return;
      // The notice store collapses a repeat into a count, so a blocked or full
      // local storage says this once rather than every 800ms.
      notify('This browser will not keep a local backup of your changes — save to a file.', 'fail');
    }, 800);
    return () => clearTimeout(t);
  }, [doc, pendingRestore]);

  // Closing the tab on work that is in no file is the one unrecoverable
  // action left, and nothing stood in front of it (SRS-PERS-025).
  useEffect(() => {
    const onUnload = (e: BeforeUnloadEvent) => {
      const { revision, savedRevision } = useDocumentStore.getState();
      if (revision === savedRevision) return;
      e.preventDefault();
    };
    window.addEventListener('beforeunload', onUnload);
    return () => window.removeEventListener('beforeunload', onUnload);
  }, []);

  // Save, the shortcuts sheet and the palette were a second window listener
  // with its own idea of what a modifier means. They are entries in the keymap
  // now, so there is one listener and one declaration (SRS-EDIT-040).
  useKeyboardShortcuts();

  return (
    <div className={compact ? 'app-shell app-shell--compact' : 'app-shell'}>
      <Header />
      <ChartArea />
      <CommandBar />
      <StatusBar />
      {pendingRestore && (
        <RestoreBanner
          onRestore={() => {
            // `replaceDocument`, not `setDocument`: a restored snapshot is the
            // one whole document that by definition is in NO file, so it must
            // start dirty. Marking it clean made the New and Open guards stay
            // silent over the very work they exist to protect.
            replaceDocument(pendingRestore);
            setPendingRestore(null);
          }}
          onDismiss={() => {
            clearAutosave();
            setPendingRestore(null);
          }}
        />
      )}
      <ChartContextMenu />
      <ShortcutsOverlay />
      <CommandPalette />
      <NewRoadmapDialog />
      <DetailsDialog />
    </div>
  );
}
