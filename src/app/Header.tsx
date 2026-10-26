import { useDocumentStore } from '../stores/documentStore';
import { useEditStore } from '../stores/editStore';
import { Undo2, Redo2, Magnet, HelpCircle, Settings } from 'lucide-react';
import { ExportMenu } from './ExportMenu';
import { BrandLogo } from './BrandLogo';
import { lazy, Suspense, useState } from 'react';
import { SettingsDialog } from './SettingsDialog';

/**
 * Agent bridge toggle — dev builds only.
 *
 * `import.meta.env.DEV` is statically false in production, so the dynamic
 * import sits in a dead branch and the whole bridge is dropped from the bundle
 * rather than merely hidden (SRS-ABR-050; gated by a build-output test).
 */
const AgentBridgeToggle = import.meta.env.DEV
  ? lazy(() =>
      import('../agent/AgentBridgeToggle').then((m) => ({ default: m.AgentBridgeToggle })),
    )
  : null;

export function Header() {
  const title = useDocumentStore((s) => s.doc.title);
  const setTitle = useDocumentStore((s) => s.setTitle);
  const undo = useDocumentStore((s) => s.undo);
  const redo = useDocumentStore((s) => s.redo);
  const canUndo = useDocumentStore((s) => s.past.length > 0);
  const canRedo = useDocumentStore((s) => s.future.length > 0);
  const snapEnabled = useEditStore((s) => s.snapEnabled);
  const setSnap = useEditStore((s) => s.setSnap);
  const [settingsOpen, setSettingsOpen] = useState(false);







  return (
    <header className="app-header">
      <div className="brand">
        <BrandLogo variant="full" size={22} />
        <div className="brand__sep" aria-hidden="true" />
        <TitleChip title={title} onChange={setTitle} />
      </div>

      <div className="toolbar">
        {/* Inserting lives in the command bar's INSERT group, on the chart's
            right-click menu, and on T / Y / B. It was in the header too, where
            it dropped everything on the first row at today's date and left the
            user to drag it into place — the worst of the three (DECISIONS.md
            D7, PLAN.md v2.13.4 / C4b). */}
        <Cluster>
          <IconBtn
            onClick={() => setSnap(!snapEnabled)}
            title="Toggle snap to scale (S)"
            icon={<Magnet size={14} strokeWidth={1.75} />}
            label="Snap"
            active={snapEnabled}
          />
          {/* Fit lives in the tool palette only — it was rendered in both
              places at once (PLAN.md v2.13.0 / C0c). */}
        </Cluster>

        <Cluster>
          <IconBtn
            onClick={undo}
            disabled={!canUndo}
            title="Undo (Ctrl+Z)"
            icon={<Undo2 size={14} strokeWidth={1.75} />}
          />
          <IconBtn
            onClick={redo}
            disabled={!canRedo}
            title="Redo (Ctrl+Shift+Z)"
            icon={<Redo2 size={14} strokeWidth={1.75} />}
          />
        </Cluster>

        <IconBtn
          onClick={() => setSettingsOpen(true)}
          title="Settings, about and updates"
          icon={<Settings size={14} strokeWidth={1.75} />}
        />
        <IconBtn
          onClick={() => window.dispatchEvent(new KeyboardEvent('keydown', { key: '?', shiftKey: true }))}
          title="Keyboard shortcuts (?)"
          icon={<HelpCircle size={14} strokeWidth={1.75} />}
        />
        {AgentBridgeToggle && (
          <Suspense fallback={null}>
            <AgentBridgeToggle />
          </Suspense>
        )}
        <ExportMenu />
      </div>
      {settingsOpen && <SettingsDialog onClose={() => setSettingsOpen(false)} />}
    </header>
  );
}

/* ------------------------------------------------------------------ */
/* sub-components                                                      */
/* ------------------------------------------------------------------ */

function TitleChip({ title, onChange }: { title: string; onChange: (v: string) => void }) {
  return (
    <input
      aria-label="Chart title"
      value={title}
      onChange={(e) => onChange(e.target.value)}
      placeholder="Untitled plan"
      className="title-chip"
    />
  );
}

function Cluster({ children }: { children: React.ReactNode }) {
  return <div className="app-header__cluster">{children}</div>;
}

interface IconBtnProps {
  onClick?: () => void;
  title?: string;
  icon: React.ReactNode;
  label?: string;
  disabled?: boolean;
  active?: boolean;
}

function IconBtn({ onClick, title, icon, label, disabled, active }: IconBtnProps) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      title={title}
      aria-pressed={active}
      className={`app-header__btn ${label ? 'label-hideable' : ''} ${active ? 'is-active' : ''}`}
    >
      {icon}
      {label && <span>{label}</span>}
    </button>
  );
}
