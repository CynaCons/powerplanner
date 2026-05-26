import { useEffect } from 'react';
import { ChartArea } from './ChartArea';
import { Inspector } from './Inspector';
import { Header } from './Header';
import { StatusBar } from './StatusBar';
import { useDocumentStore } from '../stores/documentStore';
import { useKeyboardShortcuts } from './useKeyboardShortcuts';

export function App() {
  const theme = useDocumentStore((s) => s.doc.style.theme);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  useKeyboardShortcuts();

  return (
    <div className="app-shell">
      <Header />
      <Inspector />
      <ChartArea />
      <StatusBar />
    </div>
  );
}
