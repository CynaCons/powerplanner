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

export function Inspector() {
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

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section
      style={{
        position: 'relative',
        background: 'var(--color-bg-elev)',
        border: '1px solid var(--color-border-hairline)',
        borderRadius: 'var(--radius-lg)',
        padding: '14px 14px 12px',
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
      <h3 style={{ margin: '0 0 12px', fontSize: 10.5, fontWeight: 600, letterSpacing: 0.9, textTransform: 'uppercase', color: 'var(--color-text-dim)' }}>
        {title}
      </h3>
      {children}
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
