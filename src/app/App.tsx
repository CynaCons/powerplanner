import { useEffect, useRef, useState } from 'react';
import { ChartArea } from './ChartArea';
import { Inspector } from './Inspector';
import { Header } from './Header';
import { StatusBar } from './StatusBar';
import { RestoreBanner } from './RestoreBanner';
import { useDocumentStore } from '../stores/documentStore';
import { useKeyboardShortcuts } from './useKeyboardShortcuts';
import { readEmbeddedDocument } from '../persistence/embedded';
import { loadAutosave, saveAutosave, clearAutosave } from '../persistence/autosave';
import { saveToFile } from '../persistence/fileIo';
import type { GanttDocument } from '../types/document';

export function App() {
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);
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

  // Ctrl+S → save
  useEffect(() => {
    function onKey(e: KeyboardEvent) {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        const current = useDocumentStore.getState().doc;
        void saveToFile(current, suggestedName(current.title));
      }
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useKeyboardShortcuts();

  return (
    <div className="app-shell">
      <Header />
      <Inspector />
      <ChartArea />
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
    </div>
  );
}

function suggestedName(title: string): string {
  const slug = title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
  return `${slug || 'plan'}.html`;
}
