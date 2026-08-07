import { useState, useEffect } from 'react';
import { ChevronRight, ChevronLeft } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import type { TimeScale, ThemeName } from '../types/document';
import { SelectionInspector } from './SelectionInspector';
import { diffDays } from '../utils/dates';

const SCALES: { id: TimeScale; label: string }[] = [
  { id: 'day', label: 'D' },
  { id: 'week', label: 'W' },
  { id: 'month', label: 'M' },
  { id: 'quarter', label: 'Q' },
  { id: 'year', label: 'Y' },
];

const THEMES: { id: ThemeName; label: string }[] = [
  { id: 'dark', label: 'Dark' },
  { id: 'light', label: 'Light' },
  { id: 'print', label: 'Print' },
];

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

const PANEL_KEY = 'powerplanner:inspector:panelCollapsed:v1';

/**
 * Collapses the whole panel to a thin rail.
 *
 * Section-level collapse reclaims vertical space but the column keeps its
 * 320px, which on a dense chart is the binding constraint on legibility — the
 * chart had 580px for 18 months. Collapsing the panel hands that width back.
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
    // The grid column width lives in CSS; drive it from a document-level class
    // so the chart column actually reflows.
    document.querySelector('.app-shell')?.classList.toggle('app-shell--inspector-collapsed', next);
  };

  useEffect(() => {
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

function InspectorBody({ onCollapsePanel }: { onCollapsePanel: () => void }) {
  const scale = useDocumentStore((s) => s.doc.calendar.scale);
  const setScale = useDocumentStore((s) => s.setScale);
  const fiscalYearStart = useDocumentStore((s) => s.doc.calendar.fiscalYearStart);
  const setFiscalYearStart = useDocumentStore((s) => s.setFiscalYearStart);
  const theme = useDocumentStore((s) => s.doc.style.theme);
  const setTheme = useDocumentStore((s) => s.setTheme);
  const selection = useSelectionStore((s) => s.items);
  const doc = useDocumentStore((s) => s.doc);

  if (selection.length > 0) {
    return (
      <aside className="inspector">
        <PanelCollapseBar onCollapse={onCollapsePanel} />
        <SelectionInspector />
      </aside>
    );
  }

  // Statistics
  const dates = [
    ...doc.tasks.flatMap((t) => [t.start, t.end]),
    ...doc.milestones.map((m) => m.date),
  ];
  const min = dates.length > 0 ? dates.reduce((a, b) => (a < b ? a : b)) : null;
  const max = dates.length > 0 ? dates.reduce((a, b) => (a > b ? a : b)) : null;
  const spanDays = min && max ? diffDays(min, max) + 1 : 0;
  const spanLabel =
    spanDays === 0
      ? '—'
      : spanDays < 31
        ? `${spanDays}d`
        : spanDays < 365
          ? `${Math.round(spanDays / 7)}w`
          : `${(spanDays / 365).toFixed(1)}y`;
  const avgPct =
    doc.tasks.length > 0
      ? Math.round(doc.tasks.reduce((a, t) => a + (t.percentComplete ?? 0), 0) / doc.tasks.length)
      : 0;

  return (
    <aside className="inspector">
      <PanelCollapseBar onCollapse={onCollapsePanel} />
      <Section title="Document">
        <Field label="Scale">
          <Segmented
            options={SCALES}
            value={scale}
            onChange={(v) => setScale(v as TimeScale)}
            ariaLabel="Time scale"
          />
        </Field>
        <Field label="Fiscal year starts">
          <select
            value={fiscalYearStart}
            onChange={(e) => setFiscalYearStart(Number(e.target.value))}
          >
            {Array.from({ length: 12 }, (_, i) => i + 1).map((m) => (
              <option key={m} value={m}>
                {MONTHS[m - 1]}
              </option>
            ))}
          </select>
        </Field>
      </Section>

      <Section title="Appearance">
        <Field label="Theme">
          <Segmented options={THEMES} value={theme} onChange={(v) => setTheme(v as ThemeName)} ariaLabel="Theme" />
        </Field>
      </Section>

      <Section title="Statistics">
        <div
          style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: 1,
            background: 'var(--color-border-hairline)',
            border: '1px solid var(--color-border-hairline)',
            borderRadius: 'var(--radius)',
            overflow: 'hidden',
          }}
        >
          <Stat label="Tasks" value={String(doc.tasks.length)} />
          <Stat label="Milestones" value={String(doc.milestones.length)} />
          <Stat label="Span" value={spanLabel} mono />
          <Stat label="Avg complete" value={`${avgPct}%`} mono />
        </div>
        {min && max && (
          <div
            style={{
              marginTop: 10,
              fontSize: 10.5,
              color: 'var(--color-text-dim)',
              letterSpacing: 0.3,
              fontFeatureSettings: '"tnum"',
              textAlign: 'center',
            }}
          >
            {min} → {max}
          </div>
        )}
      </Section>

      <div
        style={{
          marginTop: 8,
          padding: '10px 12px',
          fontSize: 11,
          lineHeight: 1.55,
          color: 'var(--color-text-dim)',
          background: 'transparent',
        }}
      >
        Select any item to edit, or press <kbd>?</kbd> to see all shortcuts.
      </div>
    </aside>
  );
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */

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

const COLLAPSE_KEY = 'powerplanner:inspector:collapsed:v1';

function readCollapsed(): Record<string, boolean> {
  try {
    return JSON.parse(localStorage.getItem(COLLAPSE_KEY) || '{}') as Record<string, boolean>;
  } catch {
    return {};
  }
}

function writeCollapsed(state: Record<string, boolean>): void {
  try {
    localStorage.setItem(COLLAPSE_KEY, JSON.stringify(state));
  } catch {
    // Quota / private mode — collapsing still works for this session.
  }
}

/**
 * A collapsible inspector panel.
 *
 * The panel occupies a fixed 320px of a viewport the chart also needs; on a
 * dense chart that is most of the reading area. Collapsed state persists so the
 * choice survives a reload (PLAN.md v2.12.1).
 */
function Section({ title, children }: { title: string; children: React.ReactNode }) {
  const [collapsed, setCollapsed] = useState(() => readCollapsed()[title] ?? false);

  const toggle = () => {
    const next = !collapsed;
    setCollapsed(next);
    writeCollapsed({ ...readCollapsed(), [title]: next });
  };

  return (
    <section
      style={{
        position: 'relative',
        background: 'var(--color-bg-elev)',
        border: '1px solid var(--color-border-hairline)',
        borderRadius: 'var(--radius-lg)',
        padding: collapsed ? '10px 14px' : '14px 14px 12px',
        marginBottom: 12,
        boxShadow: 'var(--shadow-1)',
      }}
    >
      {/* Inset top highlight — Stripe-style craft signal */}
      <div
        aria-hidden="true"
        style={{
          position: 'absolute',
          inset: '0 1px auto 1px',
          top: 0,
          height: 1,
          background: 'linear-gradient(to right, transparent, var(--color-card-highlight) 40%, var(--color-card-highlight) 60%, transparent)',
          borderTopLeftRadius: 'var(--radius-lg)',
          borderTopRightRadius: 'var(--radius-lg)',
          pointerEvents: 'none',
        }}
      />
      <button
        onClick={toggle}
        aria-expanded={!collapsed}
        title={collapsed ? `Expand ${title}` : `Collapse ${title}`}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 6,
          width: '100%',
          margin: 0,
          marginBottom: collapsed ? 0 : 12,
          padding: 0,
          border: 'none',
          background: 'transparent',
          fontSize: 10.5,
          fontWeight: 600,
          letterSpacing: 0.9,
          textTransform: 'uppercase',
          color: 'var(--color-text-dim)',
          cursor: 'pointer',
          textAlign: 'left',
        }}
      >
        <ChevronRight
          size={12}
          strokeWidth={2}
          aria-hidden="true"
          style={{
            transform: collapsed ? 'rotate(0deg)' : 'rotate(90deg)',
            transition: 'transform 120ms ease',
            flexShrink: 0,
          }}
        />
        {title}
      </button>
      {!collapsed && children}
    </section>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 10 }}>
      <label style={{ fontSize: 11, color: 'var(--color-text-soft)' }}>{label}</label>
      {children}
    </div>
  );
}

interface SegmentedProps<T extends string> {
  options: { id: T; label: string }[];
  value: T;
  onChange: (v: T) => void;
  ariaLabel: string;
}

function Segmented<T extends string>({ options, value, onChange, ariaLabel }: SegmentedProps<T>) {
  return (
    <div
      role="radiogroup"
      aria-label={ariaLabel}
      style={{
        display: 'grid',
        gridTemplateColumns: `repeat(${options.length}, 1fr)`,
        background: 'var(--color-bg-subtle)',
        border: '1px solid var(--color-border-hairline)',
        borderRadius: 'var(--radius)',
        padding: 2,
        gap: 1,
      }}
    >
      {options.map((o) => {
        const active = o.id === value;
        return (
          <button
            key={o.id}
            role="radio"
            aria-checked={active}
            onClick={() => onChange(o.id)}
            style={{
              padding: '5px 6px',
              fontSize: 11.5,
              fontWeight: active ? 600 : 500,
              border: 'none',
              borderRadius: 4,
              background: active ? 'var(--color-bg-panel)' : 'transparent',
              color: active ? 'var(--color-text)' : 'var(--color-text-soft)',
              cursor: 'pointer',
              transition: 'background 120ms ease, color 120ms ease',
              boxShadow: active ? 'var(--shadow-1)' : 'none',
              letterSpacing: 0.2,
            }}
          >
            {o.label}
          </button>
        );
      })}
    </div>
  );
}

function Stat({ label, value, mono }: { label: string; value: string; mono?: boolean }) {
  return (
    <div
      style={{
        background: 'var(--color-bg-elev)',
        padding: '12px 10px 10px',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-start',
        gap: 4,
      }}
    >
      <span
        style={{
          fontFamily: mono
            ? 'ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace'
            : undefined,
          fontFeatureSettings: '"tnum", "cv11"',
          fontSize: 18,
          fontWeight: 600,
          letterSpacing: -0.5,
          color: 'var(--color-text)',
          lineHeight: 1,
        }}
      >
        {value}
      </span>
      <span style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: 0.8, textTransform: 'uppercase', color: 'var(--color-text-dim)' }}>
        {label}
      </span>
    </div>
  );
}
