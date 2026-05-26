import type { GanttDocument } from '../types/document';

export function sampleDocument(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Q3 Launch Plan',
    calendar: {
      scale: 'week',
      fiscalYearStart: 1,
      workingDays: [1, 2, 3, 4, 5],
      holidays: [],
    },
    rows: [
      { id: 'row-design', label: 'Design', groupId: null },
      { id: 'row-eng', label: 'Engineering', groupId: null },
      { id: 'row-mkt', label: 'Marketing', groupId: null },
      { id: 'row-launch', label: 'Launch', groupId: null },
    ],
    tasks: [
      { id: 'task-1', rowId: 'row-design', label: 'Wireframes', start: '2026-06-01', end: '2026-06-14', percentComplete: 80, color: '#6366f1' },
      { id: 'task-2', rowId: 'row-design', label: 'Visual design', start: '2026-06-15', end: '2026-06-28', percentComplete: 40, color: '#6366f1' },
      { id: 'task-3', rowId: 'row-eng', label: 'API scaffolding', start: '2026-06-08', end: '2026-06-28', percentComplete: 50, color: '#10b981' },
      { id: 'task-4', rowId: 'row-eng', label: 'Frontend build', start: '2026-06-22', end: '2026-07-19', percentComplete: 20, color: '#10b981' },
      { id: 'task-5', rowId: 'row-mkt', label: 'Campaign prep', start: '2026-07-01', end: '2026-07-26', percentComplete: 10, color: '#f59e0b' },
      { id: 'task-6', rowId: 'row-launch', label: 'Beta rollout', start: '2026-07-20', end: '2026-07-31', percentComplete: 0, color: '#ec4899' },
    ],
    milestones: [
      { id: 'ms-1', rowId: 'row-design', label: 'Design freeze', date: '2026-06-28' },
      { id: 'ms-2', rowId: 'row-launch', label: 'Public launch', date: '2026-08-01' },
    ],
    brackets: [],
    dependencies: [],
    markers: [{ id: 'mk-deadline-1', type: 'deadline', label: 'Board review', date: '2026-07-15' }],
    style: { theme: 'dark', preset: 'default' },
  };
}
