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
import { loadAutosave, saveAutosave, clearAutosave } from '../persistence/autosave';
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

  // Autosave debounced
  useEffect(() => {
    const t = setTimeout(() => saveAutosave(doc), 800);
    return () => clearTimeout(t);
  }, [doc]);

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
