import { useEffect, useMemo, useRef, useState } from 'react';
import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import { useToolStore, type Tool } from '../stores/toolStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { saveToFile, openFile, exportJson, exportYaml, exportSvg, exportPng } from '../persistence/fileIo';
import { TEMPLATES } from '../utils/templates';
import { todayISO, addDays } from '../utils/dates';
import { newId } from '../utils/ids';
import type { TimeScale, ThemeName } from '../types/document';
import {
  Plus,
  Diamond,
  Flag,
  Brackets as BracketIcon,
  Rows3,
  Magnet,
  Maximize2,
  Save,
  FolderOpen,
  Download,
  Sun,
  Moon,
  Printer,
  MousePointer2,
  Hand,
  BoxSelect,
  CalendarDays,
  Layers,
  HelpCircle,
  FileText,
  Search,
  Undo2,
  Redo2,
  Trash2,
  Zap,
  GitCompare,
  Map as MapIcon,
} from 'lucide-react';
import type { LucideIcon } from 'lucide-react';

interface Command {
  id: string;
  label: string;
  group: 'Create' | 'Tools' | 'Scale' | 'View' | 'Theme' | 'File' | 'Templates' | 'Edit' | 'Help';
  icon?: LucideIcon;
  shortcut?: string;
  keywords?: string;
  run: () => void;
}

interface Props {
  open: boolean;
  onClose: () => void;
}

export function CommandPalette({ open, onClose }: Props) {
  const [query, setQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const listRef = useRef<HTMLDivElement | null>(null);

  const commands = useCommands(onClose);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return commands;
    return commands.filter((c) => {
      const haystack = `${c.label} ${c.group} ${c.keywords ?? ''}`.toLowerCase();
      return haystack.includes(q);
    });
  }, [commands, query]);

  useEffect(() => {
    if (open) {
      setQuery('');
      setSelectedIndex(0);
      setTimeout(() => inputRef.current?.focus(), 0);
    }
  }, [open]);

  useEffect(() => {
    setSelectedIndex(0);
  }, [query]);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        onClose();
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        setSelectedIndex((i) => Math.min(filtered.length - 1, i + 1));
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        setSelectedIndex((i) => Math.max(0, i - 1));
      } else if (e.key === 'Enter') {
        e.preventDefault();
        const cmd = filtered[selectedIndex];
        if (cmd) cmd.run();
      }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, [open, filtered, selectedIndex, onClose]);

  // Auto-scroll selected into view
  useEffect(() => {
    const list = listRef.current;
    if (!list) return;
    const el = list.querySelector<HTMLElement>(`[data-idx="${selectedIndex}"]`);
    el?.scrollIntoView({ block: 'nearest' });
  }, [selectedIndex]);

  if (!open) return null;

  // Group adjacent items with the same group header
  const groups: { group: string; items: Command[] }[] = [];
  for (const cmd of filtered) {
    const last = groups[groups.length - 1];
    if (last && last.group === cmd.group) last.items.push(cmd);
    else groups.push({ group: cmd.group, items: [cmd] });
  }

  return (
    <div
      role="dialog"
      aria-modal="true"
      aria-label="Command palette"
      onClick={onClose}
      style={{
        position: 'fixed', inset: 0, display: 'flex', alignItems: 'flex-start', justifyContent: 'center',
        paddingTop: '12vh', background: 'var(--color-overlay-scrim)',
        backdropFilter: 'blur(10px)', WebkitBackdropFilter: 'blur(10px)', zIndex: 10000,
      }}
    >
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          width: 'min(600px, 92vw)', maxHeight: '70vh',
          background: 'var(--color-bg-elev)', border: '1px solid var(--color-border-hairline)',
          borderRadius: 'var(--radius-lg)', boxShadow: 'var(--shadow-3)',
          overflow: 'hidden', display: 'flex', flexDirection: 'column',
        }}
      >
        {/* Search bar */}
        <div
          style={{
            display: 'flex', alignItems: 'center', gap: 10, padding: '14px 16px',
            borderBottom: '1px solid var(--color-border-hairline)',
          }}
        >
          <Search size={16} strokeWidth={1.75} style={{ color: 'var(--color-text-soft)', flexShrink: 0 }} />
          <input
            ref={inputRef}
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Type a command, search templates or anything…"
            style={{
              flex: 1, padding: 0, background: 'transparent', border: 'none', outline: 'none',
              fontSize: 14, color: 'var(--color-text)',
            }}
          />
          <kbd>Esc</kbd>
        </div>

        {/* Results */}
        <div ref={listRef} style={{ overflowY: 'auto', padding: '6px 6px 8px' }}>
          {filtered.length === 0 ? (
            <div style={{ padding: '24px 16px', textAlign: 'center', fontSize: 12, color: 'var(--color-text-dim)' }}>
              No commands match “{query}”
            </div>
          ) : (
            groups.map((g) => (
              <div key={g.group}>
                <div
                  style={{
                    padding: '10px 12px 4px', fontSize: 10, fontWeight: 600,
                    letterSpacing: 0.9, textTransform: 'uppercase', color: 'var(--color-text-dim)',
                  }}
                >
                  {g.group}
                </div>
                {g.items.map((cmd) => {
                  const idx = filtered.indexOf(cmd);
                  const active = idx === selectedIndex;
                  const Icon = cmd.icon;
                  return (
                    <button
                      key={cmd.id}
                      data-idx={idx}
                      onClick={() => cmd.run()}
                      onMouseEnter={() => setSelectedIndex(idx)}
                      style={{
                        display: 'flex', alignItems: 'center', gap: 10, width: '100%',
                        padding: '8px 12px', border: 'none', borderRadius: 'var(--radius)',
                        background: active ? 'var(--color-accent-soft)' : 'transparent',
                        color: active ? 'var(--color-accent)' : 'var(--color-text)',
                        fontSize: 13, textAlign: 'left', cursor: 'pointer',
                      }}
                    >
                      {Icon ? <Icon size={15} strokeWidth={1.75} /> : <span style={{ width: 15 }} />}
                      <span style={{ flex: 1 }}>{cmd.label}</span>
                      {cmd.shortcut && <kbd>{cmd.shortcut}</kbd>}
                    </button>
                  );
                })}
              </div>
            ))
          )}
        </div>

        {/* Footer hint */}
        <div
          style={{
            padding: '8px 14px', fontSize: 11, color: 'var(--color-text-dim)',
            borderTop: '1px solid var(--color-border-hairline)', display: 'flex',
            justifyContent: 'space-between', alignItems: 'center', fontFeatureSettings: '"tnum"',
          }}
        >
          <span>
            <kbd>↑</kbd> <kbd>↓</kbd> to navigate, <kbd>Enter</kbd> to run
          </span>
          <span>{filtered.length} commands</span>
        </div>
      </div>
    </div>
  );
}

function useCommands(close: () => void): Command[] {
  // Read-once handles (avoid resubscribing for static command list)
  const docActions = useDocumentStore.getState();
  const view = useViewportStore.getState();
  const edit = useEditStore.getState();
  const sel = useSelectionStore.getState();

  function run(fn: () => void) {
    return () => {
      fn();
      close();
    };
  }

  function withSuggested(name: string): string {
    const slug = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
    return `${slug || 'plan'}.html`;
  }

  function quickAddTask() {
    const d = useDocumentStore.getState();
    if (d.doc.rows.length === 0) return;
    const today = todayISO();
    const id = newId('task');
    d.addTask({ id, rowId: d.doc.rows[0].id, label: 'New task', start: today, end: addDays(today, 7), percentComplete: 0 });
    useSelectionStore.getState().setSelection([{ kind: 'task', id }]);
  }
  function quickAddMilestone() {
    const d = useDocumentStore.getState();
    if (d.doc.rows.length === 0) return;
    const id = newId('ms');
    d.addMilestone({ id, rowId: d.doc.rows[0].id, label: 'New milestone', date: todayISO() });
    useSelectionStore.getState().setSelection([{ kind: 'milestone', id }]);
  }

  function fitAll() {
    const d = useDocumentStore.getState().doc;
    const dates = [...d.tasks.flatMap((t) => [t.start, t.end]), ...d.milestones.map((m) => m.date)];
    if (dates.length === 0) return;
    const min = dates.reduce((a, b) => (a < b ? a : b));
    const max = dates.reduce((a, b) => (a > b ? a : b));
    const chartArea = document.querySelector('.chart-area');
    const totalWidth = chartArea?.clientWidth ?? 800;
    const gutter = totalWidth < 600 ? 120 : 200;
    view.fit(min, max, Math.max(100, totalWidth - gutter));
  }

  function setScale(s: TimeScale) {
    useDocumentStore.getState().setScale(s);
  }

  function setTool(t: Tool) {
    useToolStore.getState().setTool(t, true);
  }

  function setTheme(t: ThemeName) {
    useDocumentStore.getState().setTheme(t);
  }

  function applyTemplate(templateId: string) {
    const tpl = TEMPLATES.find((x) => x.id === templateId);
    if (!tpl) return;
    if (!confirm(`Replace the current chart with the "${tpl.name}" template? Your current chart will be lost unless saved.`)) return;
    // Deep clone so user edits don't mutate the template
    useDocumentStore.getState().setDocument(JSON.parse(JSON.stringify(tpl.doc)));
  }

  function showShortcuts() {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: '?', shiftKey: true }));
  }

  function deleteSelection() {
    const s = useSelectionStore.getState();
    const d = useDocumentStore.getState();
    for (const item of s.items) {
      if (item.kind === 'task') d.deleteTask(item.id);
      else if (item.kind === 'milestone') d.deleteMilestone(item.id);
      else if (item.kind === 'bracket') d.deleteBracket(item.id);
      else if (item.kind === 'dependency') d.deleteDependency(item.id);
      else if (item.kind === 'marker') d.deleteMarker(item.id);
    }
    s.clear();
  }

  const cmds: Command[] = [];

  // CREATE
  cmds.push({ id: 'create-task', label: 'New task', group: 'Create', icon: Plus, shortcut: 'T', keywords: 'add', run: run(quickAddTask) });
  cmds.push({ id: 'create-ms', label: 'New milestone', group: 'Create', icon: Diamond, shortcut: 'Y', keywords: 'add', run: run(quickAddMilestone) });
  cmds.push({
    id: 'create-bracket',
    label: 'New bracket from selection',
    group: 'Create',
    icon: BracketIcon,
    shortcut: 'B',
    keywords: 'phase group add',
    run: run(() => {
      const d = useDocumentStore.getState();
      const sIds = useSelectionStore.getState().items.filter((x) => x.kind === 'task').map((x) => x.id);
      const tasks = d.doc.tasks.filter((t) => sIds.includes(t.id));
      if (tasks.length === 0) {
        const t = todayISO();
        d.addBracket({ id: newId('br'), label: 'Phase', start: t, end: addDays(t, 14), rowIds: d.doc.rows[0] ? [d.doc.rows[0].id] : [] });
      } else {
        const start = tasks.reduce((a, t) => (t.start < a ? t.start : a), tasks[0].start);
        const end = tasks.reduce((a, t) => (t.end > a ? t.end : a), tasks[0].end);
        d.addBracket({ id: newId('br'), label: 'Phase', start, end, rowIds: Array.from(new Set(tasks.map((t) => t.rowId))) });
      }
    }),
  });
  cmds.push({ id: 'create-deadline', label: 'New deadline marker', group: 'Create', icon: Flag, keywords: 'add line', run: run(() => useDocumentStore.getState().addMarker({ id: newId('mk'), type: 'deadline', label: 'Deadline', date: todayISO() })) });
  cmds.push({ id: 'create-row', label: 'New row', group: 'Create', icon: Rows3, keywords: 'add lane swimlane', run: run(() => { const d = useDocumentStore.getState(); d.addRow({ id: newId('row'), label: `Row ${d.doc.rows.length + 1}`, groupId: null }); }) });

  // TOOLS
  cmds.push({ id: 'tool-select', label: 'Switch to Select tool', group: 'Tools', icon: MousePointer2, shortcut: 'V', run: run(() => setTool('select')) });
  cmds.push({ id: 'tool-task', label: 'Switch to Add Task tool', group: 'Tools', icon: Plus, shortcut: 'T', run: run(() => setTool('add-task')) });
  cmds.push({ id: 'tool-ms', label: 'Switch to Add Milestone tool', group: 'Tools', icon: Diamond, shortcut: 'Y', run: run(() => setTool('add-milestone')) });
  cmds.push({ id: 'tool-marquee', label: 'Switch to Marquee tool', group: 'Tools', icon: BoxSelect, shortcut: 'R', run: run(() => setTool('marquee')) });
  cmds.push({ id: 'tool-pan', label: 'Switch to Pan tool', group: 'Tools', icon: Hand, shortcut: 'H', run: run(() => setTool('pan')) });

  // SCALE
  cmds.push({ id: 'scale-d', label: 'Scale: Day', group: 'Scale', icon: CalendarDays, shortcut: '⇧D', run: run(() => setScale('day')) });
  cmds.push({ id: 'scale-w', label: 'Scale: Week', group: 'Scale', icon: CalendarDays, shortcut: '⇧W', run: run(() => setScale('week')) });
  cmds.push({ id: 'scale-m', label: 'Scale: Month', group: 'Scale', icon: CalendarDays, shortcut: '⇧M', run: run(() => setScale('month')) });
  cmds.push({ id: 'scale-q', label: 'Scale: Quarter', group: 'Scale', icon: CalendarDays, shortcut: '⇧Q', run: run(() => setScale('quarter')) });
  cmds.push({ id: 'scale-y', label: 'Scale: Year', group: 'Scale', icon: CalendarDays, shortcut: '⇧Y', run: run(() => setScale('year')) });

  // VIEW
  const vw = useViewStore.getState();
  cmds.push({ id: 'view-fit', label: 'Fit chart to data', group: 'View', icon: Maximize2, shortcut: 'Home', run: run(fitAll) });
  cmds.push({ id: 'view-snap', label: edit.snapEnabled ? 'Turn snap off' : 'Turn snap on', group: 'View', icon: Magnet, shortcut: 'S', keywords: 'snap toggle', run: run(() => useEditStore.getState().setSnap(!useEditStore.getState().snapEnabled)) });
  cmds.push({ id: 'view-zoom-in', label: 'Zoom in', group: 'View', shortcut: '+', run: run(() => view.zoom(1.25, 400)) });
  cmds.push({ id: 'view-zoom-out', label: 'Zoom out', group: 'View', shortcut: '−', run: run(() => view.zoom(0.8, 400)) });
  cmds.push({
    id: 'view-critical',
    label: vw.showCriticalPath ? 'Hide critical path' : 'Show critical path',
    group: 'View',
    icon: Zap,
    keywords: 'cpm slack longest path',
    run: run(() => useViewStore.getState().toggle('showCriticalPath')),
  });
  cmds.push({
    id: 'view-baseline',
    label: vw.showBaseline ? 'Hide baseline' : 'Show baseline (drift)',
    group: 'View',
    icon: GitCompare,
    keywords: 'snapshot drift compare',
    run: run(() => useViewStore.getState().toggle('showBaseline')),
  });
  cmds.push({
    id: 'view-minimap',
    label: vw.showMinimap ? 'Hide minimap' : 'Show minimap',
    group: 'View',
    icon: MapIcon,
    run: run(() => useViewStore.getState().toggle('showMinimap')),
  });

  // THEME
  cmds.push({ id: 'theme-dark', label: 'Theme: Dark', group: 'Theme', icon: Moon, run: run(() => setTheme('dark')) });
  cmds.push({ id: 'theme-light', label: 'Theme: Light', group: 'Theme', icon: Sun, run: run(() => setTheme('light')) });
  cmds.push({ id: 'theme-print', label: 'Theme: Print (high-contrast)', group: 'Theme', icon: Printer, run: run(() => setTheme('print')) });

  // FILE
  cmds.push({ id: 'file-save', label: 'Save', group: 'File', icon: Save, shortcut: '⌘S', keywords: 'embed html', run: run(() => { const d = useDocumentStore.getState().doc; void saveToFile(d, withSuggested(d.title)); }) });
  cmds.push({ id: 'file-open', label: 'Open file…', group: 'File', icon: FolderOpen, keywords: 'load', run: run(() => { void openFile().then((d) => { if (d) useDocumentStore.getState().setDocument(d); }); }) });
  cmds.push({ id: 'file-export-json', label: 'Export as JSON', group: 'File', icon: Download, run: run(() => exportJson(useDocumentStore.getState().doc)) });
  cmds.push({ id: 'file-export-yaml', label: 'Export as YAML', group: 'File', icon: Download, run: run(() => exportYaml(useDocumentStore.getState().doc)) });
  cmds.push({ id: 'file-export-svg', label: 'Export as SVG', group: 'File', icon: Download, run: run(() => exportSvg()) });
  cmds.push({ id: 'file-export-png-1', label: 'Export as PNG (1×)', group: 'File', icon: Download, run: run(() => exportPng(1)) });
  cmds.push({ id: 'file-export-png-2', label: 'Export as PNG (2×)', group: 'File', icon: Download, run: run(() => exportPng(2)) });
  cmds.push({ id: 'file-print', label: 'Print or save as PDF…', group: 'File', icon: Printer, run: run(() => window.print()) });

  // TEMPLATES
  for (const t of TEMPLATES) {
    cmds.push({
      id: `tpl-${t.id}`,
      label: `New from template — ${t.name}`,
      group: 'Templates',
      icon: t.id === 'blank' ? FileText : Layers,
      keywords: `${t.description} starter sample`,
      run: run(() => applyTemplate(t.id)),
    });
  }

  // EDIT
  cmds.push({ id: 'edit-undo', label: 'Undo', group: 'Edit', icon: Undo2, shortcut: '⌘Z', run: run(() => docActions.undo()) });
  cmds.push({ id: 'edit-redo', label: 'Redo', group: 'Edit', icon: Redo2, shortcut: '⌘⇧Z', run: run(() => docActions.redo()) });
  cmds.push({
    id: 'edit-baseline-capture',
    label: useDocumentStore.getState().doc.baseline ? 'Re-capture baseline (overwrite)' : 'Capture baseline',
    group: 'Edit',
    icon: GitCompare,
    keywords: 'snapshot save current dates',
    run: run(() => useDocumentStore.getState().captureBaseline()),
  });
  if (useDocumentStore.getState().doc.baseline) {
    cmds.push({
      id: 'edit-baseline-clear',
      label: 'Clear baseline',
      group: 'Edit',
      icon: Trash2,
      run: run(() => useDocumentStore.getState().clearBaseline()),
    });
  }
  if (sel.items.length > 0) {
    cmds.push({ id: 'edit-delete', label: `Delete ${sel.items.length} selected`, group: 'Edit', icon: Trash2, shortcut: 'Del', run: run(deleteSelection) });
  }

  // HELP
  cmds.push({ id: 'help-shortcuts', label: 'Show keyboard shortcuts', group: 'Help', icon: HelpCircle, shortcut: '?', run: run(showShortcuts) });

  return cmds;
}
