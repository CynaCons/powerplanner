import type { GanttDocument, Row, Task, Milestone, Dependency } from '../types/document';
import { addDays, startOfMonth, todayISO } from './dates';

/**
 * The default document — the first and often the only thing anyone sees.
 *
 * It is a real programme, not a toy: an ASIL-B body control module on AUTOSAR
 * Classic, with the workstreams such a project actually carries. The owner
 * asked for this (2026-10-23) because a four-row sample hides every problem
 * that only shows up at density — label collisions, lane packing, whether the
 * row gutter reads as a hierarchy or as a list.
 *
 * Two rules it has to keep:
 *
 * 1. **Anchored to today**, never to a hardcoded quarter, so it always opens on
 *    a live plan with the today line in view.
 * 2. **Colour is a stream tag.** One hue per discipline, from the stream
 *    palette in global.css. Alerts are never a stream colour.
 */

/** Keep in sync with --stream-N in src/styles/global.css. */
const STREAM = {
  prussian: '#1F4E79',
  teal: '#17736B',
  moss: '#4F7A2A',
  rust: '#A8452B',
  plum: '#6B3F7A',
  amber: '#C07A14',
  steel: '#6E8299',
} as const;

interface Pkg {
  id: string;
  label: string;
  /** Offsets in days from the plan anchor. */
  start: number;
  end: number;
  percentComplete: number;
}

interface Group {
  id: string;
  label: string;
  color: string;
  packages: Pkg[];
}

const GROUPS: Group[] = [
  {
    id: 'req',
    label: 'Requirements & architecture',
    color: STREAM.prussian,
    packages: [
      { id: 'stkreq', label: 'Stakeholder requirements', start: 0, end: 80, percentComplete: 100 },
      { id: 'sysarch', label: 'System architecture', start: 55, end: 165, percentComplete: 100 },
      { id: 'ecuex', label: 'ECU extract', start: 110, end: 165, percentComplete: 100 },
      { id: 'swarch', label: 'SW architecture, SWC design', start: 140, end: 250, percentComplete: 90 },
    ],
  },
  {
    id: 'plat',
    label: 'Platform — BSW and MCAL',
    color: STREAM.teal,
    packages: [
      { id: 'mcal', label: 'MCAL integration', start: 165, end: 250, percentComplete: 85 },
      { id: 'ecuc', label: 'BSW configuration, EcuC', start: 195, end: 335, percentComplete: 70 },
      { id: 'com', label: 'COM stack, CAN-FD', start: 220, end: 330, percentComplete: 65 },
      { id: 'diag', label: 'Diagnostics, Dcm and Dem', start: 250, end: 390, percentComplete: 40 },
      { id: 'nvm', label: 'NvM and memory stack', start: 280, end: 360, percentComplete: 30 },
      { id: 'os', label: 'OS configuration', start: 220, end: 275, percentComplete: 80 },
      { id: 'rte', label: 'RTE generation', start: 305, end: 390, percentComplete: 20 },
    ],
  },
  {
    id: 'asw',
    label: 'Application software',
    color: STREAM.moss,
    packages: [
      { id: 'sens', label: 'Sensor acquisition SWC', start: 250, end: 360, percentComplete: 55 },
      { id: 'ctrl', label: 'Control strategy SWC', start: 280, end: 445, percentComplete: 35 },
      { id: 'act', label: 'Actuator SWC', start: 305, end: 415, percentComplete: 25 },
      { id: 'mode', label: 'Mode management', start: 335, end: 415, percentComplete: 10 },
    ],
  },
  {
    id: 'fusa',
    label: 'Functional safety, ISO 26262',
    color: STREAM.rust,
    packages: [
      { id: 'hara', label: 'HARA and safety goals', start: 0, end: 80, percentComplete: 100 },
      { id: 'tsc', label: 'Technical safety concept', start: 80, end: 195, percentComplete: 100 },
      { id: 'asilreq', label: 'ASIL-B software requirements', start: 140, end: 250, percentComplete: 85 },
      { id: 'safemech', label: 'Safety mechanisms, E2E and watchdog', start: 250, end: 390, percentComplete: 45 },
      { id: 'ffi', label: 'Freedom from interference', start: 305, end: 415, percentComplete: 20 },
      { id: 'safecase', label: 'Safety case and confirmation review', start: 390, end: 500, percentComplete: 0 },
    ],
  },
  {
    id: 'sec',
    label: 'Cybersecurity, ISO 21434',
    color: STREAM.plum,
    packages: [
      { id: 'tara', label: 'TARA', start: 25, end: 110, percentComplete: 100 },
      { id: 'secboot', label: 'Secure boot and key provisioning', start: 220, end: 330, percentComplete: 50 },
      { id: 'crypto', label: 'Crypto stack, Csm and CryIf', start: 250, end: 360, percentComplete: 40 },
      { id: 'secoc', label: 'SecOC configuration', start: 280, end: 360, percentComplete: 30 },
      { id: 'pentest', label: 'Penetration test', start: 415, end: 500, percentComplete: 0 },
    ],
  },
  {
    id: 'vv',
    label: 'Verification and validation',
    color: STREAM.amber,
    packages: [
      { id: 'unit', label: 'Unit test, MC/DC coverage', start: 280, end: 445, percentComplete: 35 },
      { id: 'swcint', label: 'SWC integration test', start: 335, end: 445, percentComplete: 15 },
      { id: 'timing', label: 'Timing and resource analysis', start: 335, end: 415, percentComplete: 10 },
      { id: 'hil', label: 'HIL campaign', start: 360, end: 500, percentComplete: 5 },
      { id: 'fault', label: 'Fault injection', start: 415, end: 500, percentComplete: 0 },
    ],
  },
  {
    id: 'rel',
    label: 'Release',
    color: STREAM.steel,
    packages: [
      { id: 'bsample', label: 'B-sample build', start: 335, end: 390, percentComplete: 10 },
      { id: 'csample', label: 'C-sample build', start: 415, end: 470, percentComplete: 0 },
    ],
  },
];

export function sampleDocument(): GanttDocument {
  // Start of last month: puts today inside the run of work rather than at an
  // edge, which is how a plan is read in a review.
  const anchor = addDays(startOfMonth(todayISO()), -200);
  const d = (offset: number) => addDays(anchor, offset);

  const rows: Row[] = [];
  const tasks: Task[] = [];

  for (const g of GROUPS) {
    rows.push({ id: `row-${g.id}`, label: g.label, groupId: null });
    for (const p of g.packages) {
      rows.push({ id: `row-${p.id}`, label: p.label, groupId: `row-${g.id}` });
      tasks.push({
        id: `task-${p.id}`,
        rowId: `row-${p.id}`,
        label: p.label,
        start: d(p.start),
        end: d(p.end),
        percentComplete: p.percentComplete,
        color: g.color,
      });
    }
  }

  const milestones: Milestone[] = [
    { id: 'ms-swfreeze', rowId: 'row-swarch', label: 'SW architecture frozen', date: d(250) },
    { id: 'ms-bsw', rowId: 'row-rte', label: 'BSW integrated', date: d(390) },
    { id: 'ms-tara', rowId: 'row-tara', label: 'TARA approved', date: d(110) },
    { id: 'ms-safecase', rowId: 'row-safecase', label: 'Safety case released', date: d(500) },
    { id: 'ms-bsample', rowId: 'row-bsample', label: 'B-sample', date: d(390) },
    { id: 'ms-csample', rowId: 'row-csample', label: 'C-sample', date: d(470) },
    { id: 'ms-sop', rowId: 'row-rel', label: 'Start of production', date: d(530) },
  ];

  const dependencies: Dependency[] = [
    { id: 'dep-arch-sw', from: 'task-sysarch', to: 'task-swarch', type: 'finish-to-start' },
    { id: 'dep-sw-mcal', from: 'task-swarch', to: 'task-rte', type: 'finish-to-start' },
    { id: 'dep-rte-swcint', from: 'task-rte', to: 'task-swcint', type: 'finish-to-start' },
    { id: 'dep-safemech-case', from: 'task-safemech', to: 'task-safecase', type: 'finish-to-start' },
    { id: 'dep-secoc-pen', from: 'task-secoc', to: 'task-pentest', type: 'finish-to-start' },
    { id: 'dep-hil-csample', from: 'task-hil', to: 'task-csample', type: 'finish-to-start' },
  ];

  return {
    schemaVersion: 1,
    title: 'Body control module — ASIL-B platform',
    calendar: {
      scale: 'month',
      fiscalYearStart: 1,
      workingDays: [1, 2, 3, 4, 5],
      holidays: [],
    },
    rows,
    tasks,
    milestones,
    brackets: [],
    dependencies,
    markers: [{ id: 'mk-gate', type: 'deadline', label: 'Gate review', date: d(420) }],
    style: { theme: 'light', preset: 'default' },
  };
}
