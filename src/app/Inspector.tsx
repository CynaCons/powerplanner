import { useState, useEffect } from 'react';
import { ChevronLeft } from 'lucide-react';
import { useSelectionStore } from '../stores/selectionStore';
import { SelectionInspector } from './SelectionInspector';

const PANEL_KEY = 'powerplanner:inspector:panelCollapsed:v1';

/**
 * Collapses the whole panel to a thin rail.
 *
 * Section-level collapse reclaims vertical space but the column keeps its
 * 320px, which on a dense chart is the binding constraint on legibility.
 * Collapsing the panel hands that width back.
 */
export function Inspector() {
  const [panelCollapsed, setPanelCollapsed] = useState(() => {
    try {
      return localStorage.getItem(PANEL_KEY) === '1';
    } catch {
      return false;
    }
  });

  const togglePanel = () => {
    const next = !panelCollapsed;
    setPanelCollapsed(next);
    try {
      localStorage.setItem(PANEL_KEY, next ? '1' : '0');
    } catch {
      /* non-fatal */
    }
  };

  useEffect(() => {
    // The grid column width lives in CSS; drive it from a document-level class
    // so the chart column actually reflows.
    document.querySelector('.app-shell')?.classList.toggle('app-shell--inspector-collapsed', panelCollapsed);
  }, [panelCollapsed]);

  if (panelCollapsed) {
    return (
      <aside className="inspector inspector--collapsed">
        <button
          onClick={togglePanel}
          aria-expanded={false}
          title="Show inspector"
          className="inspector__rail-btn"
        >
          <ChevronLeft size={14} strokeWidth={2} style={{ transform: 'rotate(180deg)' }} />
        </button>
      </aside>
    );
  }

  return <InspectorBody onCollapsePanel={togglePanel} />;
}

/**
 * The inspector is **selection-only** (PLAN.md v2.13.0 / C0).
 *
 * It used to carry Document, Appearance and Statistics sections in its empty
 * state. All three were chrome that already existed elsewhere: the scale
 * control was a live duplicate of the tool palette's — both painted at the same
 * time, both wired to `setScale` — and the counts duplicated the status bar.
 *
 * Theme and fiscal year moved to File → Document settings; statistics moved to
 * the status bar; the duplicate scale control was deleted rather than
 * relocated. What remains is the panel's real job: editing the selection.
 *
 * This is the first step toward retiring the column entirely in favour of a
 * contextual command bar (Phase 16 / v2.13.3).
 */
function InspectorBody({ onCollapsePanel }: { onCollapsePanel: () => void }) {
  const selection = useSelectionStore((s) => s.items);

  return (
    <aside className="inspector">
      <PanelCollapseBar onCollapse={onCollapsePanel} />
      {selection.length > 0 ? (
        <SelectionInspector />
      ) : (
        <div
          style={{
            padding: '10px 12px',
            fontSize: 11,
            lineHeight: 1.55,
            color: 'var(--color-text-dim)',
          }}
        >
          Select any item to edit, or press <kbd>?</kbd> to see all shortcuts.
        </div>
      )}
    </aside>
  );
}

/** Hide-the-whole-panel control, so the chart can take the 320px back. */
function PanelCollapseBar({ onCollapse }: { onCollapse: () => void }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: 8 }}>
      <button
        onClick={onCollapse}
        aria-expanded={true}
        title="Hide inspector (give the width to the chart)"
        className="inspector__rail-btn"
      >
        <ChevronLeft size={14} strokeWidth={2} />
      </button>
    </div>
  );
}
