import { useState, useRef, useEffect } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { exportJson, exportYaml, exportSvg, exportPng, saveToFile, saveAs, openFile } from '../persistence/fileIo';
import { ChevronDown, Download, Save, FolderOpen } from 'lucide-react';

export function ExportMenu() {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  const doc = useDocumentStore((s) => s.doc);
  const setDocument = useDocumentStore((s) => s.setDocument);

  useEffect(() => {
    function onClick(e: MouseEvent) {
      if (!ref.current?.contains(e.target as Node)) setOpen(false);
    }
    if (open) document.addEventListener('mousedown', onClick);
    return () => document.removeEventListener('mousedown', onClick);
  }, [open]);

  const onSave = async () => {
    await saveToFile(doc, suggestedName(doc.title));
    setOpen(false);
  };

  const onSaveAs = async () => {
    await saveAs(doc, suggestedName(doc.title));
    setOpen(false);
  };

  const onOpen = async () => {
    setOpen(false);
    const next = await openFile();
    if (next) setDocument(next);
  };

  return (
    <div ref={ref} style={{ position: 'relative' }}>
      <button onClick={() => setOpen((o) => !o)} title="File menu" className="icon-btn">
        <Save size={14} /> <span>File</span> <ChevronDown size={12} />
      </button>
      {open && (
        <div
          style={{
            position: 'absolute',
            right: 0,
            top: '110%',
            background: 'var(--color-bg-panel)',
            border: '1px solid var(--color-border)',
            borderRadius: 6,
            minWidth: 200,
            boxShadow: '0 6px 20px rgba(0,0,0,0.3)',
            padding: 4,
            zIndex: 50,
          }}
          role="menu"
        >
          <MenuItem icon={<Save size={14} />} label="Save (Ctrl+S)" onClick={onSave} />
          <MenuItem icon={<Save size={14} />} label="Save as…" onClick={onSaveAs} />
          <MenuItem icon={<FolderOpen size={14} />} label="Open file…" onClick={onOpen} />
          <Divider />
          <MenuItem icon={<Download size={14} />} label="Export JSON" onClick={() => { exportJson(doc); setOpen(false); }} />
          <MenuItem icon={<Download size={14} />} label="Export YAML" onClick={() => { exportYaml(doc); setOpen(false); }} />
          <MenuItem icon={<Download size={14} />} label="Export SVG" onClick={() => { exportSvg(); setOpen(false); }} />
          <MenuItem icon={<Download size={14} />} label="Export PNG 1×" onClick={() => { exportPng(1); setOpen(false); }} />
          <MenuItem icon={<Download size={14} />} label="Export PNG 2×" onClick={() => { exportPng(2); setOpen(false); }} />
          <MenuItem icon={<Download size={14} />} label="Print / PDF" onClick={() => { window.print(); setOpen(false); }} />
        </div>
      )}
    </div>
  );
}

function MenuItem({ icon, label, onClick }: { icon: React.ReactNode; label: string; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      style={{
        display: 'flex',
        width: '100%',
        gap: 8,
        alignItems: 'center',
        padding: '6px 8px',
        background: 'transparent',
        border: 'none',
        textAlign: 'left',
        cursor: 'pointer',
        borderRadius: 4,
        color: 'var(--color-text)',
      }}
      onMouseEnter={(e) => ((e.currentTarget.style.background = 'var(--color-bg-elev)'))}
      onMouseLeave={(e) => ((e.currentTarget.style.background = 'transparent'))}
    >
      {icon}
      {label}
    </button>
  );
}

function Divider() {
  return <div style={{ height: 1, background: 'var(--color-border-soft)', margin: '4px 2px' }} />;
}

function suggestedName(title: string): string {
  const slug = title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
  return `${slug || 'plan'}.html`;
}
