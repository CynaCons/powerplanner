export type ISODate = string;

export type TimeScale = 'day' | 'week' | 'month' | 'quarter' | 'year';

export type DependencyType = 'finish-to-start' | 'start-to-start' | 'finish-to-finish' | 'start-to-finish';

export type LabelPlacement = 'on-bar' | 'left' | 'right' | 'above' | 'below' | 'hidden';

export type ThemeName = 'light' | 'dark' | 'print';

export interface CalendarSettings {
  scale: TimeScale;
  fiscalYearStart: number;
  workingDays: number[];
  holidays: ISODate[];
}

export interface Row {
  id: string;
  label: string;
  groupId: string | null;
  collapsed?: boolean;
}

export interface Task {
  id: string;
  rowId: string;
  label: string;
  start: ISODate;
  end: ISODate;
  percentComplete?: number;
  color?: string;
  labelPlacement?: LabelPlacement;
  notes?: string;
}

export interface Milestone {
  id: string;
  rowId: string;
  label: string;
  date: ISODate;
  labelPlacement?: LabelPlacement;
  color?: string;
}

export interface Bracket {
  id: string;
  label: string;
  start: ISODate;
  end: ISODate;
  rowIds: string[];
  color?: string;
}

export interface Dependency {
  id: string;
  from: string;
  to: string;
  type: DependencyType;
}

export type MarkerType = 'today' | 'deadline' | 'note';

export interface Marker {
  id: string;
  type: MarkerType;
  label: string;
  date: ISODate;
  color?: string;
}

export interface StyleSettings {
  theme: ThemeName;
  preset: string;
}

export interface Viewport {
  startDate: ISODate;
  endDate: ISODate;
  pxPerDay: number;
}

export interface BaselineSnapshot {
  capturedAt: ISODate;
  tasks: Array<Pick<Task, 'id' | 'start' | 'end'>>;
}

export interface GanttDocument {
  schemaVersion: 1;
  title: string;
  calendar: CalendarSettings;
  rows: Row[];
  tasks: Task[];
  milestones: Milestone[];
  brackets: Bracket[];
  dependencies: Dependency[];
  markers: Marker[];
  style: StyleSettings;
  baseline?: BaselineSnapshot;
}
