import { Trash2, SlidersHorizontal, Brackets as BracketIcon, Plus, Rows3 } from 'lucide-react';
import { useDocumentStore } from '../stores/documentStore';
import { useSelectionStore, type Selectable } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { addDays, todayISO } from '../utils/dates';
import { newId } from '../utils/ids';
import { ColorPalette } from './ColorPalette';
import { deleteSelection } from './documentActions';
import type { Task, Milestone, Bracket, Dependency, DependencyType } from '../types/document';

/**
 * Quick actions for what is selected (PLAN.md v2.20.0).
 *
 * The rule the owner set, three times: the bottom bar is not an editing
 * surface. What survives here is what reduces to a single press and changes
 * one thing — a colour, a day, ten percent, a delete. Anything with a text
 * field, a date picker or a dropdown in it moved to the details dialog, which
 * opens from this bar's one door, from the right-click menu, and from Enter.
 *
 * What is NOT here is the four stepper buttons (SRS-EDIT-075). SR-IXC-01
 * forbids "stepper buttons, +/- incrementers, or similar discrete controls for
 * continuous values" in those words, and a date and a percentage are both
 * continuous: the date verb is dragging the bar (SR-IXC-02) and the progress
 * verb is dragging its fill edge (SR-IXC-06). Precise numeric entry is the
 * details dialog's job, which is the secondary editor SR-IXC-02 reserves for
 * it — not a pair of presses that can only move in tens.
 */
export function SelectionGroup() {
  const items = useSelectionStore((s) => s.items);
  const doc = useDocumentStore((s) => s.doc);

  if (items.length === 0) return null;

  // The thing you clicked owns the bar; the row it sits on is selected too,
  // but only so the gutter highlights with it. Keying off the whole selection
  // instead would mean clicking a bar showed the ROW's actions, and colour,
  // dates, percent and notes would have no home at all.
  const sel = items[0];
  if (sel.kind === 'row') return <RowFields rowId={sel.id} />;

  // A bar plus its row is two items by design, so it is not a multi-select.
  const isBarWithItsRow = items.length === 2 && items[1]?.kind === 'row';
  if (items.length > 1 && !isBarWithItsRow) {
    return <MultiFields count={items.length} />;
  }
  if (sel.kind === 'task') {
    const task = doc.tasks.find((t) => t.id === sel.id);
    return task ? <TaskFields task={task} /> : null;
  }
  if (sel.kind === 'milestone') {
    const m = doc.milestones.find((x) => x.id === sel.id);
    return m ? <MilestoneFields milestone={m} /> : null;
  }
  if (sel.kind === 'bracket') {
    const b = doc.brackets.find((x) => x.id === sel.id);
    return b ? <BracketFields bracket={b} /> : null;
  }
  if (sel.kind === 'dependency') {
    const d = doc.dependencies.find((x) => x.id === sel.id);
    return d ? <DependencyFields dependency={d} /> : null;
  }
  return null;
}

function Group({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="cmdbar__group">
      <span className="cmdbar__group-label">{label}</span>
      {children}
    </div>
  );
}

/*
 * The name used to be restated here (SRS-EDIT-029).
 *
 * It was the only indication of what was selected, because the chart never
 * repainted to show it (SRS-EDIT-023). With the chart answering the click,
 * repeating the name is duplication that cost the width the bar's actual
 * controls needed, and the owner asked for it to go.
 */

function DeleteButton({ what, onClick }: { what: string; onClick: () => void }) {
  return (
    <button className="cmdbar__btn" title={`Delete ${what}`} aria-label={`Delete ${what}`} onClick={onClick}>
      <Trash2 size={14} strokeWidth={1.75} />
    </button>
  );
}

function WrapInBracket({ taskIds }: { taskIds: string[] }) {
  const doc = useDocumentStore((s) => s.doc);
  const addBracket = useDocumentStore((s) => s.addBracket);
  const tasks = doc.tasks.filter((t) => taskIds.includes(t.id));
  if (tasks.length === 0) return null;

  const wrap = () => {
    const start = tasks.reduce((a, t) => (t.start < a ? t.start : a), tasks[0].start);
    const end = tasks.reduce((a, t) => (t.end > a ? t.end : a), tasks[0].end);
    const rowIds = Array.from(new Set(tasks.map((t) => t.rowId)));
    addBracket({ id: newId('br'), label: 'Phase', start, end, rowIds });
  };

  return (
    <button className="cmdbar__btn" title="Wrap in bracket (B)" aria-label="Wrap in bracket" onClick={wrap}>
      <BracketIcon size={14} strokeWidth={1.75} />
    </button>
  );
}

/**
 * The door to the details dialog. Opens it; never edits anything itself.
 *
 * It names its own subject. Letting the dialog infer one from the selection
 * meant the Row button opened Task details for the first task on that row,
 * because selecting a row selects everything on it.
 */
function DetailsButton({ what, target }: { what: string; target: Selectable }) {
  const openDetails = useViewStore((s) => s.openDetails);
  return (
    <button
      className="cmdbar__btn"
      title={`${what} details… (Enter)`}
      aria-label={`${what} details`}
      onClick={() => openDetails(target)}
    >
      <SlidersHorizontal size={14} strokeWidth={1.75} />
    </button>
  );
}

function TaskFields({ task }: { task: Task }) {
  const updateTask = useDocumentStore((s) => s.updateTask);
  const deleteTask = useDocumentStore((s) => s.deleteTask);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Task">
      <ColorPalette value={task.color ?? '#1F4E79'} onChange={(color) => updateTask(task.id, { color })} />
      <WrapInBracket taskIds={[task.id]} />
      <DetailsButton what="Task" target={{ kind: 'task', id: task.id }} />
      <DeleteButton what="task" onClick={() => { deleteTask(task.id); clear(); }} />
    </Group>
  );
}

function MilestoneFields({ milestone }: { milestone: Milestone }) {
  const remove = useDocumentStore((s) => s.deleteMilestone);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Milestone">
      <DetailsButton what="Milestone" target={{ kind: 'milestone', id: milestone.id }} />
      <DeleteButton what="milestone" onClick={() => { remove(milestone.id); clear(); }} />
    </Group>
  );
}

function BracketFields({ bracket }: { bracket: Bracket }) {
  const remove = useDocumentStore((s) => s.deleteBracket);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Bracket">
      <DetailsButton what="Bracket" target={{ kind: 'bracket', id: bracket.id }} />
      <DeleteButton what="bracket" onClick={() => { remove(bracket.id); clear(); }} />
    </Group>
  );
}

/**
 * The four link types, short enough to sit in a bar.
 *
 * The right-click menu spells them out ("Finish → Start"); here the pair of
 * caps is the label, because the bar is a row of presses and the title says
 * the rest.
 */
const DEP_TYPES: { value: DependencyType; short: string; title: string }[] = [
  { value: 'finish-to-start', short: 'FS', title: 'Finish → Start' },
  { value: 'start-to-start', short: 'SS', title: 'Start → Start' },
  { value: 'finish-to-finish', short: 'FF', title: 'Finish → Finish' },
  { value: 'start-to-finish', short: 'SF', title: 'Start → Finish' },
];

/**
 * A selected connector's actions (SRS-ELEM-045).
 *
 * This group did not exist: the bar returned nothing for a dependency, so
 * clicking a connector selected it — the chart drew it accented — and offered
 * no way to do anything with it. Delete and the link type lived only on the
 * right-click menu. Both reduce to one press and change one thing, which is
 * what this bar is for (D10), so they are here as well; the menu keeps them
 * too, since that is where the owner found them.
 */
function DependencyFields({ dependency }: { dependency: Dependency }) {
  const update = useDocumentStore((s) => s.updateDependency);
  const remove = useDocumentStore((s) => s.deleteDependency);
  const clear = useSelectionStore((s) => s.clear);

  return (
    <Group label="Dependency">
      <span className="cmdbar__field-label">Link</span>
      <div className="cmdbar__scale" role="radiogroup" aria-label="Link type">
        {DEP_TYPES.map((dt) => (
          <button
            key={dt.value}
            role="radio"
            aria-checked={dependency.type === dt.value}
            className={dependency.type === dt.value ? 'is-active' : ''}
            title={dt.title}
            onClick={() => update(dependency.id, { type: dt.value })}
          >
            {dt.short}
          </button>
        ))}
      </div>
      <DeleteButton what="dependency" onClick={() => { remove(dependency.id); clear(); }} />
    </Group>
  );
}

function RowFields({ rowId }: { rowId: string }) {
  const doc = useDocumentStore((s) => s.doc);
  const addTask = useDocumentStore((s) => s.addTask);
  const addRow = useDocumentStore((s) => s.addRow);
  const moveRow = useDocumentStore((s) => s.moveRow);
  const deleteRow = useDocumentStore((s) => s.deleteRow);
  const setSelection = useSelectionStore((s) => s.setSelection);
  const clear = useSelectionStore((s) => s.clear);

  const row = doc.rows.find((r) => r.id === rowId);
  if (!row) return null;
  const index = doc.rows.findIndex((r) => r.id === rowId);
  const onRow = doc.tasks.filter((t) => t.rowId === rowId);

  const addTaskHere = () => {
    const start = onRow.length
      ? onRow.reduce((a, t) => (t.end > a ? t.end : a), onRow[0].end)
      : todayISO();
    const id = newId('task');
    addTask({ id, rowId, label: 'New task', start, end: addDays(start, 7), percentComplete: 0 });
    setSelection([{ kind: 'task', id }, { kind: 'row', id: rowId }]);
  };

  const addRowBelow = () => {
    const id = newId('row');
    addRow({ id, label: 'New row', groupId: row.groupId ?? null });
    moveRow(id, index + 1);
    setSelection([{ kind: 'row', id }]);
  };

  return (
    <Group label="Row">
      <button className="cmdbar__btn" title="Add a task on this row" aria-label="Add task here" onClick={addTaskHere}>
        <Plus size={14} strokeWidth={1.75} /><span>Task</span>
      </button>
      <button className="cmdbar__btn" title="Add a row below this one" aria-label="Add row below" onClick={addRowBelow}>
        <Rows3 size={14} strokeWidth={1.75} /><span>Row below</span>
      </button>
      <WrapInBracket taskIds={onRow.map((t) => t.id)} />
      <DetailsButton what="Row" target={{ kind: 'row', id: rowId }} />
      <DeleteButton what="row" onClick={() => { deleteRow(rowId); clear(); }} />
    </Group>
  );
}

function MultiFields({ count }: { count: number }) {
  const items = useSelectionStore((s) => s.items);

  // One function for all three delete surfaces, so the rules cannot drift and
  // every one of them reports (SRS-EDIT-019).
  const deleteAll = () => { deleteSelection(); };

  return (
    <Group label="Selection">
      <span className="cmdbar__selname">{count} items selected</span>
      <WrapInBracket taskIds={items.filter((i) => i.kind === 'task').map((i) => i.id)} />
      <DeleteButton what="selection" onClick={deleteAll} />
    </Group>
  );
}
