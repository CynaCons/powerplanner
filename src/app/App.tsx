import { useEffect, useRef, useState } from 'react';
import { ChartArea } from './ChartArea';
import { Inspector } from './Inspector';
import { Header } from './Header';
import { StatusBar } from './StatusBar';
import { RestoreBanner } from './RestoreBanner';
import { ToolPalette } from './ToolPalette';
import { ChartContextMenu } from './ChartContextMenu';
import { ShortcutsOverlay } from './ShortcutsOverlay';
import { CommandPalette } from './CommandPalette';
import { useDocumentStore } from '../stores/documentStore';
import { useKeyboardShortcuts } from './useKeyboardShortcuts';
import { readEmbeddedDocument } from '../persistence/embedded';
import { loadAutosave, saveAutosave, clearAutosave } from '../persistence/autosave';
import { saveToFile } from '../persistence/fileIo';
import type { GanttDocument } from '../types/document';

interface AppProps {
  /** Chart-first layout for narrow Office task panes (hides the inspector column). */
  compact?: boolean;
}

export function App({ compact = false }: AppProps) {
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);
  const didHydrate = useRef(false);
  const [pendingRestore, setPendingRestore] = useState<GanttDocument | null>(null);
  const [shortcutsOpen, setShortcutsOpen] = useState(false);
  const [paletteOpen, setPaletteOpen] = useState(false);

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

  // Ctrl+S → save; ? → show shortcuts overlay
  useEffect(() => {
    function onKey(e: KeyboardEvent) {
      const target = e.target as HTMLElement | null;
      const isEditable = target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable);
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        const current = useDocumentStore.getState().doc;
        void saveToFile(current, suggestedName(current.title));
        return;
      }
      if (!isEditable && (e.key === '?' || (e.shiftKey && e.key === '/'))) {
        e.preventDefault();
        setShortcutsOpen((v) => !v);
      }
      // Cmd/Ctrl+K opens the command palette
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        setPaletteOpen((v) => !v);
      }
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useKeyboardShortcuts();

  return (
    <div className={compact ? 'app-shell app-shell--compact' : 'app-shell'}>
      <Header />
      <Inspector />
      <ChartArea />
      <ToolPalette />
      <StatusBar />
      {pendingRestore && (
        <RestoreBanner
          onRestore={() => {
            setDocument(pendingRestore);
            setPendingRestore(null);
          }}
          onDismiss={() => {
            clearAutosave();
            setPendingRestore(null);
          }}
        />
      )}
      <ChartContextMenu />
      <ShortcutsOverlay open={shortcutsOpen} onClose={() => setShortcutsOpen(false)} />
      <CommandPalette open={paletteOpen} onClose={() => setPaletteOpen(false)} />
    </div>
  );
}

function suggestedName(title: string): string {
  const slug = title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
  return `${slug || 'plan'}.html`;
}
