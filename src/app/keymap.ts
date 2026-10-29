import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore, type Selectable, type SelectableKind } from '../stores/selectionStore';
import { bracketSpanFor } from './bracketSpan';
import { useViewStore } from '../stores/viewStore';
import { useViewportStore } from '../stores/viewportStore';
import { useEditStore } from '../stores/editStore';
import { useToolStore } from '../stores/toolStore';
import { addDays, todayISO } from '../utils/dates';
import { newId } from '../utils/ids';
import { saveCurrent } from './documentActions';
import { deleteSelection } from './documentActions';
import { resolveInsertTarget } from './insertTarget';

/**
 * Every keyboard binding the app has, declared once (SRS-EDIT-040/041).
 *
 * Bindings used to be an if-chain over `e.key` in which each branch opted in to
 * the modifier tests it happened to remember and then returned. Two failure
 * modes followed from that one shape, and both shipped:
 *
 *   A branch with no modifier test claims chords it was never meant to see.
 *   `e.key.toLowerCase() === 'n'` is true for N, Shift+N, Alt+N and Ctrl+N
 *   alike, so Ctrl+N inserted a task AND suppressed the user's "new".
 *
 *   An earlier bare-letter branch SHADOWS a later chord for the same letter.
 *   The bare-'m' insert returned before the scale block could run, so the month
 *   scale was unreachable from the keyboard while two surfaces taught ⇧M.
 *
 * The cure is to dispatch by EXACT full-chord match against this table. An
 * `'m'` entry cannot match a Shift+M or Ctrl+M event at all, so there is no
 * guard to forget and no ordering to get wrong — `shadowedPairs` proves the
 * table has no two entries claiming one chord, and an undeclared chord runs
 * nothing and preventDefaults nothing, which hands it back to the browser.
 */

export type BindingGroup = 'Insert' | 'Tools' | 'Scale' | 'Editing' | 'Navigation' | 'File' | 'Help';

export interface Binding {
  id: string;
  /** The chord this binding is taught as. */
  chord: string;
  /** Chords that also run it but are not taught — layout and platform variants. */
  aliases?: string[];
  label: string;
  group: BindingGroup;
  /** 'always' survives an open dialog or sheet; 'canvas' does not. */
  when: 'always' | 'canvas';
  /**
   * Runs even while the user is typing in a field. Only for chords that
   * cannot be typed AND whose meaning does not belong to the field: Save and
   * the palette. `mod+z` is deliberately NOT one — inside a text field, undo
   * is the field's.
   */
  whileTyping?: boolean;
  /** False where the browser's own meaning should survive (Escape). */
  preventDefault?: boolean;
  run: () => void;
}

const CHAR_ALIASES: Record<string, string> = {
  '+': 'plus',
  '=': 'plus',
  '-': 'minus',
  '_': 'minus',
  '?': 'slash',
  '/': 'slash',
};

/** One canonical name per physical key, independent of layout and shift. */
export function normKey(key: string): string {
  if (key.length === 1) return CHAR_ALIASES[key] ?? key.toLowerCase();
  return key.toLowerCase();
}

/**
 * The full chord, with EVERY modifier that is down, in one fixed order.
 *
 * `mod` is Ctrl on Windows and Linux, Cmd on Apple hardware — one token, so a
 * binding is declared once rather than twice.
 */
export function chordOf(e: Pick<KeyboardEvent, 'key' | 'metaKey' | 'ctrlKey' | 'altKey' | 'shiftKey'>): string {
  let chord = '';
  if (e.metaKey || e.ctrlKey) chord += 'mod+';
  if (e.altKey) chord += 'alt+';
  if (e.shiftKey) chord += 'shift+';
  return chord + normKey(e.key);
}

function doc() {
  return useDocumentStore.getState().doc;
}

/**
 * The single element a details dialog could edit, or null.
 *
 * `row` is accepted on its own, but a row selected on the chart drags every
 * task and milestone on it into the selection, so that case arrives as many
 * items and is correctly refused.
 */
export function detailsSubject(items: Selectable[]): Selectable | null {
  const EDITABLE: SelectableKind[] = ['task', 'milestone', 'bracket', 'row'];
  if (items.length === 1) {
    return EDITABLE.includes(items[0].kind) ? items[0] : null;
  }
  // A bar plus the row it sits on is one thing, not a multi-selection.
  if (items.length === 2 && items[1].kind === 'row' && EDITABLE.includes(items[0].kind)) {
    return items[0];
  }
  return null;
}

/**
 * Slip the selection by whole days, as ONE undo step however many items are
 * selected and however many times the key repeats in the same direction
 * (SRS-EDIT-022).
 *
 * The frame is keyed on the direction rather than ended on keyup: tapping the
 * arrow six times to move a week is one intention, and a user who then
 * presses Ctrl+Z means to be back where they started. The store's idle
 * backstop separates a later, deliberate press.
 */
function nudge(days: number) {
  const sel = useSelectionStore.getState();
  if (sel.items.length === 0) return;
  const store = useDocumentStore.getState();
  const key = `nudge:${days}`;
  const d = doc();
  for (const item of sel.items) {
    if (item.kind === 'task') {
      const t = d.tasks.find((x) => x.id === item.id);
      if (!t) continue;
      store.updateTask(t.id, { start: addDays(t.start, days), end: addDays(t.end, days) }, key);
    } else if (item.kind === 'milestone') {
      const m = d.milestones.find((x) => x.id === item.id);
      if (!m) continue;
      store.updateMilestone(m.id, { date: addDays(m.date, days) }, key);
    }
  }
}

function setScale(scale: 'day' | 'week' | 'month' | 'quarter' | 'year') {
  useDocumentStore.getState().setScale(scale);
}

function setTool(tool: 'select' | 'add-task' | 'add-milestone' | 'pan' | 'marquee') {
  // No explicit stickiness: the store knows that only pan is a mode
  // (SRS-EDIT-026). Forcing it here is what kept `t` armed forever.
  useToolStore.getState().setTool(tool);
}

/**
 * Where a keyboard insert lands (SRS-EDIT-028).
 *
 * `N` and `M` both wrote to `doc.rows[0]`, which in every shipped template is
 * a section header and is usually scrolled off the top, so the key appeared to
 * do nothing at all.
 */
function insertRowId(): string | null {
  const d = doc();
  const target = resolveInsertTarget(d, useSelectionStore.getState().items);
  if (!target) return null;
  if (target.kind === 'row') return target.rowId;
  const id = newId('row');
  useDocumentStore.getState().addRow({ id, label: 'New row', groupId: target.parentId }, target.index);
  return id;
}

/** Select what was just inserted and bring its row into view. */
function landOn(rowId: string, item: Selectable) {
  useSelectionStore.getState().setSelection([item]);
  useViewportStore.getState().reveal(rowId);
}

export const KEYMAP: readonly Binding[] = [
  // ---- Insert. These three mutate the document and were taught nowhere. ----
  {
    id: 'insert-task',
    chord: 'n',
    label: 'New task on the selected row',
    group: 'Insert',
    when: 'canvas',
    run: () => {
      const rowId = insertRowId();
      if (!rowId) return;
      const today = todayISO();
      const id = newId('task');
      useDocumentStore.getState().addTask({
        id,
        rowId,
        label: 'New task',
        start: today,
        end: addDays(today, 7),
        percentComplete: 0,
      });
      landOn(rowId, { kind: 'task', id });
    },
  },
  {
    id: 'insert-milestone',
    chord: 'm',
    label: 'New milestone on the selected row',
    group: 'Insert',
    when: 'canvas',
    run: () => {
      const rowId = insertRowId();
      if (!rowId) return;
      const id = newId('ms');
      useDocumentStore.getState().addMilestone({ id, rowId, label: 'New milestone', date: todayISO() });
      landOn(rowId, { kind: 'milestone', id });
    },
  },
  {
    id: 'insert-bracket',
    chord: 'b',
    label: 'Bracket the selected tasks',
    group: 'Insert',
    when: 'canvas',
    run: () => {
      const { addBracket } = useDocumentStore.getState();
      const d = doc();
      if (d.rows.length === 0) return;
      // One rule for the span, shared with the button and the palette: a
      // selected row counts as much as a selected task, and with nothing
      // selected the bracket covers the plan instead of landing on the first
      // section header for a fortnight (SRS-ELEM-032).
      const span = bracketSpanFor(d, useSelectionStore.getState().items);
      addBracket({ id: newId('br'), label: 'Phase', ...span });
    },
  },

  // ---- Tools ----
  { id: 'tool-select', chord: 'v', label: 'Select', group: 'Tools', when: 'canvas', run: () => setTool('select') },
  { id: 'tool-task', chord: 't', label: 'Add task', group: 'Tools', when: 'canvas', run: () => setTool('add-task') },
  { id: 'tool-milestone', chord: 'y', label: 'Add milestone', group: 'Tools', when: 'canvas', run: () => setTool('add-milestone') },
  { id: 'tool-pan', chord: 'h', label: 'Pan', group: 'Tools', when: 'canvas', run: () => setTool('pan') },
  { id: 'tool-marquee', chord: 'r', label: 'Marquee', group: 'Tools', when: 'canvas', run: () => setTool('marquee') },
  {
    id: 'escape',
    chord: 'escape',
    label: 'Clear selection, revert to Select',
    group: 'Tools',
    when: 'canvas',
    preventDefault: false,
    run: () => {
      useSelectionStore.getState().clear();
      useEditStore.getState().endEdit();
      const tool = useToolStore.getState();
      if (tool.activeTool !== 'select') tool.setTool('select', true);
    },
  },

  // ---- Scale ----
  { id: 'scale-day', chord: 'shift+d', label: 'Day', group: 'Scale', when: 'canvas', run: () => setScale('day') },
  { id: 'scale-week', chord: 'shift+w', label: 'Week', group: 'Scale', when: 'canvas', run: () => setScale('week') },
  { id: 'scale-month', chord: 'shift+m', label: 'Month', group: 'Scale', when: 'canvas', run: () => setScale('month') },
  { id: 'scale-quarter', chord: 'shift+q', label: 'Quarter', group: 'Scale', when: 'canvas', run: () => setScale('quarter') },
  { id: 'scale-year', chord: 'shift+y', label: 'Year', group: 'Scale', when: 'canvas', run: () => setScale('year') },

  // ---- Editing ----
  { id: 'undo', chord: 'mod+z', label: 'Undo', group: 'Editing', when: 'canvas', run: () => useDocumentStore.getState().undo() },
  {
    id: 'redo',
    chord: 'mod+shift+z',
    // Ctrl+Y is the Windows convention and was bound before; keep it.
    aliases: ['mod+y'],
    label: 'Redo',
    group: 'Editing',
    when: 'canvas',
    run: () => useDocumentStore.getState().redo(),
  },
  {
    id: 'delete-selection',
    chord: 'delete',
    // Backspace is the same gesture; Cmd+Delete is macOS muscle memory and
    // deleted the selection before this table existed, so it is declared
    // rather than quietly dropped.
    aliases: ['backspace', 'mod+delete', 'mod+backspace'],
    label: 'Delete selection',
    group: 'Editing',
    when: 'canvas',
    run: () => deleteSelection(),
  },
  {
    id: 'rename',
    chord: 'f2',
    label: 'Rename the selected task',
    group: 'Editing',
    when: 'canvas',
    run: () => {
      const sel = useSelectionStore.getState();
      if (sel.items.length !== 1 || sel.items[0].kind !== 'task') return;
      useEditStore.getState().beginEdit(sel.items[0].id);
    },
  },
  {
    id: 'details',
    chord: 'enter',
    label: 'Open details for the selection',
    group: 'Editing',
    when: 'canvas',
    run: () => {
      const subject = detailsSubject(useSelectionStore.getState().items);
      if (!subject) return;
      useViewStore.getState().openDetails(subject);
    },
  },
  { id: 'nudge-left', chord: 'arrowleft', label: 'Nudge back a day', group: 'Editing', when: 'canvas', run: () => nudge(-1) },
  { id: 'nudge-right', chord: 'arrowright', label: 'Nudge on a day', group: 'Editing', when: 'canvas', run: () => nudge(1) },
  { id: 'nudge-left-week', chord: 'shift+arrowleft', label: 'Nudge back a week', group: 'Editing', when: 'canvas', run: () => nudge(-7) },
  { id: 'nudge-right-week', chord: 'shift+arrowright', label: 'Nudge on a week', group: 'Editing', when: 'canvas', run: () => nudge(7) },
  {
    id: 'snap',
    chord: 's',
    label: 'Toggle snap to the scale grid',
    group: 'Editing',
    when: 'canvas',
    run: () => {
      const edit = useEditStore.getState();
      edit.setSnap(!edit.snapEnabled);
    },
  },

  // ---- Navigation ----
  {
    id: 'zoom-in',
    chord: 'plus',
    // On a US layout '+' needs Shift; both land on the same physical key.
    // `mod+plus` is NOT declared: Ctrl+= is the browser's page zoom and the
    // user pressing it means the page, not the chart.
    aliases: ['shift+plus'],
    label: 'Zoom in',
    group: 'Navigation',
    when: 'canvas',
    run: () => useViewportStore.getState().zoom(1.25),
  },
  {
    id: 'zoom-out',
    chord: 'minus',
    aliases: ['shift+minus'],
    label: 'Zoom out',
    group: 'Navigation',
    when: 'canvas',
    run: () => useViewportStore.getState().zoom(0.8),
  },
  {
    id: 'fit',
    chord: 'home',
    label: 'Fit to data',
    group: 'Navigation',
    when: 'canvas',
    run: () => {
      const d = doc();
      const dates = [...d.tasks.flatMap((t) => [t.start, t.end]), ...d.milestones.map((m) => m.date)];
      if (dates.length === 0) return;
      const min = dates.reduce((a, b) => (a < b ? a : b));
      const max = dates.reduce((a, b) => (a > b ? a : b));
      const chartArea = document.querySelector('.chart-area');
      const width = (chartArea?.clientWidth ?? 800) - 200;
      useViewportStore.getState().fit(min, max, Math.max(200, width));
    },
  },

  // ---- File and help. These survive an open dialog. ----
  { id: 'save', chord: 'mod+s', label: 'Save (embeds the chart in the HTML)', group: 'File', when: 'always', whileTyping: true, run: () => { void saveCurrent(); } },
  {
    id: 'palette',
    chord: 'mod+k',
    label: 'Command palette',
    group: 'Help',
    when: 'always',
    // The palette's own search field has focus while it is open, so without
    // this the chord that opens it could never close it.
    whileTyping: true,
    run: () => {
      const v = useViewStore.getState();
      v.setPaletteOpen(!v.paletteOpen);
    },
  },
  {
    id: 'shortcuts',
    chord: 'shift+slash',
    label: 'Show keyboard shortcuts',
    group: 'Help',
    when: 'always',
    run: () => {
      const v = useViewStore.getState();
      v.setShortcutsOpen(!v.shortcutsOpen);
    },
  },
];

function chordsOf(b: Binding): string[] {
  return [b.chord, ...(b.aliases ?? [])];
}

/** Entries claiming the same chord. `[]` is the invariant the table must hold. */
export function shadowedPairs(table: readonly Binding[]): [string, string][] {
  const owner = new Map<string, string>();
  const clashes: [string, string][] = [];
  for (const b of table) {
    for (const c of chordsOf(b)) {
      const held = owner.get(c);
      if (held) clashes.push([held, b.id]);
      else owner.set(c, b.id);
    }
  }
  return clashes;
}

const BY_CHORD = new Map<string, Binding>();
for (const b of KEYMAP) for (const c of chordsOf(b)) if (!BY_CHORD.has(c)) BY_CHORD.set(c, b);

export function resolve(e: Pick<KeyboardEvent, 'key' | 'metaKey' | 'ctrlKey' | 'altKey' | 'shiftKey'>): Binding | undefined {
  return BY_CHORD.get(chordOf(e));
}

export function bindingById(id: string): Binding | undefined {
  return KEYMAP.find((b) => b.id === id);
}

/** Apple hardware reads ⌘ where everything else reads Ctrl. */
export function isApplePlatform(): boolean {
  if (typeof navigator === 'undefined') return false;
  return /Mac|iPhone|iPad|iPod/.test(navigator.platform || navigator.userAgent);
}

const GLYPHS: Record<string, string> = {
  shift: '⇧',
  alt: '⌥',
  escape: 'Esc',
  enter: '↵',
  delete: 'Del',
  backspace: '⌫',
  arrowleft: '←',
  arrowright: '→',
  home: 'Home',
  plus: '+',
  minus: '−',
  slash: '?',
  f2: 'F2',
};

/**
 * One binding as the keys a person presses. The ONLY place Ctrl vs ⌘ is
 * decided — six surfaces used to decide it themselves, four printing ⌘ and two
 * printing "Ctrl" an inch apart.
 */
export function chordKeys(chord: string, mac = isApplePlatform()): string[] {
  return chord.split('+').map((part) => {
    if (part === 'mod') return mac ? '⌘' : 'Ctrl';
    return GLYPHS[part] ?? (part.length === 1 ? part.toUpperCase() : part);
  });
}

/** The same thing as one string, for a menu label or a palette row. */
export function shortcutFor(id: string, mac = isApplePlatform()): string {
  const b = bindingById(id);
  if (!b) throw new Error(`keymap: no binding '${id}'`);
  return chordKeys(b.chord, mac).join('');
}

/**
 * The pointer gestures, declared beside the keys so the shortcuts sheet stops
 * being hand-written. Sourced from `useChartInteractions`'s wheel and press
 * handling — the sheet used to teach "Wheel → Zoom at cursor", which has been
 * wrong since the wheel became a scroll (owner directive 2026-10-26).
 */
export const GESTURES: readonly { keys: string[]; label: string }[] = [
  { keys: ['Click'], label: 'Select item' },
  { keys: ['⇧', 'Click'], label: 'Add to selection' },
  { keys: ['Drag'], label: 'Move (on a bar)' },
  { keys: ['Wheel'], label: 'Scroll the rows' },
  { keys: ['⇧', 'Wheel'], label: 'Pan through time' },
  { keys: [isApplePlatform() ? '⌘' : 'Ctrl', 'Wheel'], label: 'Zoom at the cursor' },
  { keys: ['Drag', 'axis'], label: 'Sweep a window to zoom to' },
  { keys: ['Drag', 'bg'], label: 'Pan the view (Pan tool)' },
];
