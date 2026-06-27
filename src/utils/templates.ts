import type { GanttDocument } from '../types/document';

export interface Template {
  id: string;
  name: string;
  description: string;
  doc: GanttDocument;
}

const COLORS = {
  rose: '#c47b7b',
  sage: '#8aa68a',
  slate: '#6b7a8f',
  ochre: '#c69749',
  indigo: '#7a82c9',
  terracotta: '#c47a55',
  plum: '#9a6b9a',
  teal: '#5fa19a',
} as const;

const productLaunch: GanttDocument = {
  schemaVersion: 1,
  title: 'Product Launch — Q3 2026',
  calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
  rows: [
    { id: 'row-pl-design', label: 'Design', groupId: null },
    { id: 'row-pl-eng', label: 'Engineering', groupId: null },
    { id: 'row-pl-mkt', label: 'Marketing', groupId: null },
    { id: 'row-pl-ops', label: 'Operations', groupId: null },
    { id: 'row-pl-launch', label: 'Launch', groupId: null },
  ],
  tasks: [
    { id: 'task-pl-1', rowId: 'row-pl-design', label: 'Concept & wireframes', start: '2026-06-01', end: '2026-06-14', percentComplete: 100, color: COLORS.rose },
    { id: 'task-pl-2', rowId: 'row-pl-design', label: 'Visual design & assets', start: '2026-06-15', end: '2026-07-05', percentComplete: 60, color: COLORS.rose },
    { id: 'task-pl-3', rowId: 'row-pl-eng', label: 'Architecture & APIs', start: '2026-06-08', end: '2026-06-28', percentComplete: 70, color: COLORS.sage },
    { id: 'task-pl-4', rowId: 'row-pl-eng', label: 'Feature implementation', start: '2026-06-22', end: '2026-07-19', percentComplete: 30, color: COLORS.sage },
    { id: 'task-pl-5', rowId: 'row-pl-eng', label: 'Hardening & QA', start: '2026-07-13', end: '2026-07-26', percentComplete: 0, color: COLORS.sage },
    { id: 'task-pl-6', rowId: 'row-pl-mkt', label: 'Positioning & messaging', start: '2026-06-15', end: '2026-07-05', percentComplete: 40, color: COLORS.ochre },
    { id: 'task-pl-7', rowId: 'row-pl-mkt', label: 'Launch assets & press kit', start: '2026-07-06', end: '2026-07-26', percentComplete: 10, color: COLORS.ochre },
    { id: 'task-pl-8', rowId: 'row-pl-ops', label: 'Support playbook & training', start: '2026-07-13', end: '2026-07-31', percentComplete: 0, color: COLORS.slate },
    { id: 'task-pl-9', rowId: 'row-pl-launch', label: 'Beta rollout', start: '2026-07-20', end: '2026-07-31', percentComplete: 0, color: COLORS.indigo },
  ],
  milestones: [
    { id: 'ms-pl-1', rowId: 'row-pl-design', label: 'Design freeze', date: '2026-07-05' },
    { id: 'ms-pl-2', rowId: 'row-pl-eng', label: 'Code complete', date: '2026-07-19' },
    { id: 'ms-pl-3', rowId: 'row-pl-launch', label: 'Public launch', date: '2026-08-01' },
  ],
  brackets: [
    { id: 'br-pl-1', label: 'Phase 1 — Build', start: '2026-06-01', end: '2026-07-19', rowIds: ['row-pl-design', 'row-pl-eng'], color: COLORS.sage },
    { id: 'br-pl-2', label: 'Phase 2 — GTM', start: '2026-07-06', end: '2026-08-01', rowIds: ['row-pl-mkt', 'row-pl-ops', 'row-pl-launch'], color: COLORS.ochre },
  ],
  dependencies: [
    { id: 'dep-pl-1', from: 'task-pl-1', to: 'task-pl-2', type: 'finish-to-start' },
    { id: 'dep-pl-2', from: 'task-pl-3', to: 'task-pl-4', type: 'finish-to-start' },
    { id: 'dep-pl-3', from: 'task-pl-4', to: 'task-pl-5', type: 'finish-to-start' },
    { id: 'dep-pl-4', from: 'task-pl-5', to: 'task-pl-9', type: 'finish-to-start' },
  ],
  markers: [{ id: 'mk-pl-1', type: 'deadline', label: 'Board review', date: '2026-07-15' }],
  style: { theme: 'light', preset: 'default' },
};

const sprint: GanttDocument = {
  schemaVersion: 1,
  title: 'Sprint 24 — Two-Week Plan',
  calendar: { scale: 'day', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
  rows: [
    { id: 'row-sp-disc', label: 'Discovery', groupId: null },
    { id: 'row-sp-be', label: 'Backend', groupId: null },
    { id: 'row-sp-fe', label: 'Frontend', groupId: null },
    { id: 'row-sp-qa', label: 'QA', groupId: null },
    { id: 'row-sp-rel', label: 'Release', groupId: null },
  ],
  tasks: [
    { id: 'task-sp-1', rowId: 'row-sp-disc', label: 'Spec review', start: '2026-06-01', end: '2026-06-02', percentComplete: 100, color: COLORS.plum },
    { id: 'task-sp-2', rowId: 'row-sp-disc', label: 'Tech design doc', start: '2026-06-02', end: '2026-06-04', percentComplete: 80, color: COLORS.plum },
    { id: 'task-sp-3', rowId: 'row-sp-be', label: 'Schema migration', start: '2026-06-03', end: '2026-06-05', percentComplete: 60, color: COLORS.sage },
    { id: 'task-sp-4', rowId: 'row-sp-be', label: 'API endpoints', start: '2026-06-05', end: '2026-06-10', percentComplete: 20, color: COLORS.sage },
    { id: 'task-sp-5', rowId: 'row-sp-be', label: 'Background jobs', start: '2026-06-08', end: '2026-06-11', percentComplete: 0, color: COLORS.sage },
    { id: 'task-sp-6', rowId: 'row-sp-fe', label: 'Component scaffolding', start: '2026-06-04', end: '2026-06-08', percentComplete: 40, color: COLORS.indigo },
    { id: 'task-sp-7', rowId: 'row-sp-fe', label: 'Wire to API', start: '2026-06-09', end: '2026-06-12', percentComplete: 0, color: COLORS.indigo },
    { id: 'task-sp-8', rowId: 'row-sp-qa', label: 'Test plan', start: '2026-06-08', end: '2026-06-09', percentComplete: 0, color: COLORS.ochre },
    { id: 'task-sp-9', rowId: 'row-sp-qa', label: 'Regression sweep', start: '2026-06-11', end: '2026-06-13', percentComplete: 0, color: COLORS.ochre },
    { id: 'task-sp-10', rowId: 'row-sp-rel', label: 'Staging deploy', start: '2026-06-12', end: '2026-06-13', percentComplete: 0, color: COLORS.terracotta },
    { id: 'task-sp-11', rowId: 'row-sp-rel', label: 'Release notes & ship', start: '2026-06-13', end: '2026-06-14', percentComplete: 0, color: COLORS.terracotta },
  ],
  milestones: [
    { id: 'ms-sp-1', rowId: 'row-sp-be', label: 'API frozen', date: '2026-06-10' },
    { id: 'ms-sp-2', rowId: 'row-sp-qa', label: 'QA sign-off', date: '2026-06-13' },
    { id: 'ms-sp-3', rowId: 'row-sp-rel', label: 'Sprint demo', date: '2026-06-14' },
  ],
  brackets: [
    { id: 'br-sp-1', label: 'Week 1 — Build', start: '2026-06-01', end: '2026-06-07', rowIds: ['row-sp-disc', 'row-sp-be', 'row-sp-fe'], color: COLORS.indigo },
    { id: 'br-sp-2', label: 'Week 2 — Stabilize & ship', start: '2026-06-08', end: '2026-06-14', rowIds: ['row-sp-be', 'row-sp-fe', 'row-sp-qa', 'row-sp-rel'], color: COLORS.terracotta },
  ],
  dependencies: [
    { id: 'dep-sp-1', from: 'task-sp-2', to: 'task-sp-3', type: 'finish-to-start' },
    { id: 'dep-sp-2', from: 'task-sp-4', to: 'task-sp-7', type: 'finish-to-start' },
    { id: 'dep-sp-3', from: 'task-sp-9', to: 'task-sp-10', type: 'finish-to-start' },
    { id: 'dep-sp-4', from: 'task-sp-10', to: 'task-sp-11', type: 'finish-to-start' },
  ],
  markers: [{ id: 'mk-sp-1', type: 'deadline', label: 'Ship by EOD', date: '2026-06-14' }],
  style: { theme: 'light', preset: 'default' },
};

const hiringPlan: GanttDocument = {
  schemaVersion: 1,
  title: 'H2 2026 Hiring Plan',
  calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
  rows: [
    { id: 'row-hp-src', label: 'Sourcing', groupId: null },
    { id: 'row-hp-scr', label: 'Screening', groupId: null },
    { id: 'row-hp-int', label: 'Interviews', groupId: null },
    { id: 'row-hp-off', label: 'Offer', groupId: null },
    { id: 'row-hp-onb', label: 'Onboarding', groupId: null },
  ],
  tasks: [
    { id: 'task-hp-1', rowId: 'row-hp-src', label: 'Role scoping & JDs', start: '2026-06-01', end: '2026-06-14', percentComplete: 80, color: COLORS.slate },
    { id: 'task-hp-2', rowId: 'row-hp-src', label: 'Outbound sourcing', start: '2026-06-15', end: '2026-07-26', percentComplete: 20, color: COLORS.slate },
    { id: 'task-hp-3', rowId: 'row-hp-src', label: 'Referral campaign', start: '2026-06-22', end: '2026-07-12', percentComplete: 10, color: COLORS.slate },
    { id: 'task-hp-4', rowId: 'row-hp-scr', label: 'Recruiter phone screens', start: '2026-06-29', end: '2026-08-02', percentComplete: 0, color: COLORS.teal },
    { id: 'task-hp-5', rowId: 'row-hp-scr', label: 'Take-home reviews', start: '2026-07-06', end: '2026-08-09', percentComplete: 0, color: COLORS.teal },
    { id: 'task-hp-6', rowId: 'row-hp-int', label: 'Onsite loops', start: '2026-07-13', end: '2026-08-16', percentComplete: 0, color: COLORS.indigo },
    { id: 'task-hp-7', rowId: 'row-hp-int', label: 'Debrief & calibration', start: '2026-07-20', end: '2026-08-23', percentComplete: 0, color: COLORS.indigo },
    { id: 'task-hp-8', rowId: 'row-hp-off', label: 'Offer negotiation', start: '2026-07-27', end: '2026-08-23', percentComplete: 0, color: COLORS.ochre },
    { id: 'task-hp-9', rowId: 'row-hp-onb', label: 'Onboarding cohort 1', start: '2026-08-10', end: '2026-08-30', percentComplete: 0, color: COLORS.sage },
  ],
  milestones: [
    { id: 'ms-hp-1', rowId: 'row-hp-src', label: 'Job posts live', date: '2026-06-15' },
    { id: 'ms-hp-2', rowId: 'row-hp-off', label: 'First offers signed', date: '2026-08-03' },
    { id: 'ms-hp-3', rowId: 'row-hp-onb', label: 'First hires onboarded', date: '2026-08-24' },
  ],
  brackets: [
    { id: 'br-hp-1', label: 'Top of funnel', start: '2026-06-01', end: '2026-07-26', rowIds: ['row-hp-src', 'row-hp-scr'], color: COLORS.slate },
    { id: 'br-hp-2', label: 'Close & onboard', start: '2026-07-13', end: '2026-08-30', rowIds: ['row-hp-int', 'row-hp-off', 'row-hp-onb'], color: COLORS.sage },
  ],
  dependencies: [
    { id: 'dep-hp-1', from: 'task-hp-1', to: 'task-hp-2', type: 'finish-to-start' },
    { id: 'dep-hp-2', from: 'task-hp-4', to: 'task-hp-6', type: 'finish-to-start' },
    { id: 'dep-hp-3', from: 'task-hp-7', to: 'task-hp-8', type: 'finish-to-start' },
    { id: 'dep-hp-4', from: 'task-hp-8', to: 'task-hp-9', type: 'finish-to-start' },
  ],
  markers: [{ id: 'mk-hp-1', type: 'deadline', label: 'Headcount target', date: '2026-08-30' }],
  style: { theme: 'light', preset: 'default' },
};

const marketingCampaign: GanttDocument = {
  schemaVersion: 1,
  title: 'Q3 Marketing Campaign — New Feature',
  calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
  rows: [
    { id: 'row-mc-strat', label: 'Strategy', groupId: null },
    { id: 'row-mc-creat', label: 'Creative', groupId: null },
    { id: 'row-mc-chan', label: 'Channels', groupId: null },
    { id: 'row-mc-paid', label: 'Paid', groupId: null },
    { id: 'row-mc-earn', label: 'Earned', groupId: null },
    { id: 'row-mc-meas', label: 'Measurement', groupId: null },
  ],
  tasks: [
    { id: 'task-mc-1', rowId: 'row-mc-strat', label: 'Audience & positioning', start: '2026-06-01', end: '2026-06-14', percentComplete: 100, color: COLORS.plum },
    { id: 'task-mc-2', rowId: 'row-mc-strat', label: 'Campaign brief', start: '2026-06-15', end: '2026-06-21', percentComplete: 80, color: COLORS.plum },
    { id: 'task-mc-3', rowId: 'row-mc-creat', label: 'Concepts & copy', start: '2026-06-22', end: '2026-07-12', percentComplete: 40, color: COLORS.rose },
    { id: 'task-mc-4', rowId: 'row-mc-creat', label: 'Video & motion assets', start: '2026-07-06', end: '2026-07-26', percentComplete: 10, color: COLORS.rose },
    { id: 'task-mc-5', rowId: 'row-mc-chan', label: 'Landing page build', start: '2026-07-06', end: '2026-07-26', percentComplete: 0, color: COLORS.indigo },
    { id: 'task-mc-6', rowId: 'row-mc-chan', label: 'Lifecycle emails', start: '2026-07-13', end: '2026-08-02', percentComplete: 0, color: COLORS.indigo },
    { id: 'task-mc-7', rowId: 'row-mc-paid', label: 'Paid social flight', start: '2026-07-27', end: '2026-08-30', percentComplete: 0, color: COLORS.ochre },
    { id: 'task-mc-8', rowId: 'row-mc-paid', label: 'Search & retargeting', start: '2026-07-27', end: '2026-08-30', percentComplete: 0, color: COLORS.ochre },
    { id: 'task-mc-9', rowId: 'row-mc-earn', label: 'PR & analyst briefings', start: '2026-07-20', end: '2026-08-09', percentComplete: 0, color: COLORS.teal },
    { id: 'task-mc-10', rowId: 'row-mc-earn', label: 'Community & partners', start: '2026-08-03', end: '2026-08-23', percentComplete: 0, color: COLORS.teal },
    { id: 'task-mc-11', rowId: 'row-mc-meas', label: 'Dashboards & tracking', start: '2026-07-13', end: '2026-07-26', percentComplete: 0, color: COLORS.slate },
    { id: 'task-mc-12', rowId: 'row-mc-meas', label: 'Wrap-up analysis', start: '2026-08-24', end: '2026-09-06', percentComplete: 0, color: COLORS.slate },
  ],
  milestones: [
    { id: 'ms-mc-1', rowId: 'row-mc-chan', label: 'Campaign live', date: '2026-07-27' },
    { id: 'ms-mc-2', rowId: 'row-mc-paid', label: 'Mid-campaign review', date: '2026-08-16' },
    { id: 'ms-mc-3', rowId: 'row-mc-meas', label: 'Wrap report', date: '2026-09-06' },
  ],
  brackets: [
    { id: 'br-mc-1', label: 'Pre-launch', start: '2026-06-01', end: '2026-07-26', rowIds: ['row-mc-strat', 'row-mc-creat', 'row-mc-chan'], color: COLORS.rose },
    { id: 'br-mc-2', label: 'In-market', start: '2026-07-27', end: '2026-08-30', rowIds: ['row-mc-paid', 'row-mc-earn'], color: COLORS.ochre },
    { id: 'br-mc-3', label: 'Wrap-up', start: '2026-08-24', end: '2026-09-06', rowIds: ['row-mc-meas'], color: COLORS.slate },
  ],
  dependencies: [
    { id: 'dep-mc-1', from: 'task-mc-2', to: 'task-mc-3', type: 'finish-to-start' },
    { id: 'dep-mc-2', from: 'task-mc-3', to: 'task-mc-5', type: 'finish-to-start' },
    { id: 'dep-mc-3', from: 'task-mc-5', to: 'task-mc-7', type: 'finish-to-start' },
    { id: 'dep-mc-4', from: 'task-mc-7', to: 'task-mc-12', type: 'finish-to-start' },
  ],
  markers: [{ id: 'mk-mc-1', type: 'deadline', label: 'Earnings call mention', date: '2026-08-30' }],
  style: { theme: 'light', preset: 'default' },
};

export const TEMPLATES: Template[] = [
  { id: 'product-launch', name: 'Product Launch', description: 'Cross-functional Q3 launch — design, build, GTM, ship.', doc: productLaunch },
  { id: 'sprint', name: 'Two-Week Sprint', description: 'Day-by-day engineering sprint from kickoff to demo.', doc: sprint },
  { id: 'hiring-plan', name: 'Hiring Plan', description: 'Source-to-onboard pipeline across a recruiting half.', doc: hiringPlan },
  { id: 'marketing-campaign', name: 'Marketing Campaign', description: 'Ten-week campaign arc: pre-launch, in-market, wrap-up.', doc: marketingCampaign },
  {
    id: 'blank',
    name: 'Blank Chart',
    description: 'Start from scratch — one row, no tasks.',
    doc: {
      schemaVersion: 1,
      title: 'Untitled plan',
      calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
      rows: [{ id: 'row-blank-1', label: 'Untitled row', groupId: null }],
      tasks: [],
      milestones: [],
      brackets: [],
      dependencies: [],
      markers: [],
      style: { theme: 'light', preset: 'default' },
    },
  },
];
