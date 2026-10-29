export type ISODate = string;

export type TimeScale = 'day' | 'week' | 'month' | 'quarter' | 'year';

export type DependencyType = 'finish-to-start' | 'start-to-start' | 'finish-to-finish' | 'start-to-finish';

export type LabelPlacement = 'on-bar' | 'left' | 'right' | 'above' | 'below' | 'hidden';

/**
 * A label dragged somewhere of the user's own choosing, in px from the
 * marker it belongs to. It overrides `labelPlacement`; clearing it returns
 * the label to automatic placement.
 */
export interface LabelOffset {
  dx: number;
  dy: number;
}

export type ThemeName = 'light' | 'dark' | 'print';

/**
 * How dense the vertical rules in the plot are (SRS-TAX-054).
 *
 * Deliberately not derived from `scale` or from the zoom: the owner's decision
 * on 2026-10-29 was that a planner reading days may still want to see where
 * the months break, so the granularity is theirs to state and travels with the
 * roadmap.
 */
export type GridDelimiter = 'none' | 'day' | 'week' | 'month' | 'quarter';

/** Whether the non-working-day mask is painted behind the bars (SRS-TAX-056). */
export type WeekendDisplay = 'off' | 'shaded';

/**
 * What a roadmap written before these two settings existed means by their
 * absence. Read through `gridDelimiterOf`/`weekendDisplayOf` rather than
 * inlined, so the four surfaces that need the answer cannot drift (SRS-TAX-057).
 */
export const DEFAULT_GRID_DELIMITER: GridDelimiter = 'week';
export const DEFAULT_WEEKEND_DISPLAY: WeekendDisplay = 'shaded';

export interface CalendarSettings {
  scale: TimeScale;
  fiscalYearStart: number;
  workingDays: number[];
  holidays: ISODate[];
  /** Absent in every file written before 2026-10-29; see DEFAULT_GRID_DELIMITER. */
  gridDelimiter?: GridDelimiter;
  /** Absent in every file written before 2026-10-29; see DEFAULT_WEEKEND_DISPLAY. */
  weekendDisplay?: WeekendDisplay;
}

export function gridDelimiterOf(calendar: CalendarSettings): GridDelimiter {
  return calendar.gridDelimiter ?? DEFAULT_GRID_DELIMITER;
}

export function weekendDisplayOf(calendar: CalendarSettings): WeekendDisplay {
  return calendar.weekendDisplay ?? DEFAULT_WEEKEND_DISPLAY;
}

export const GRID_DELIMITERS: readonly GridDelimiter[] = ['none', 'day', 'week', 'month', 'quarter'];
export const WEEKEND_DISPLAYS: readonly WeekendDisplay[] = ['off', 'shaded'];

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
  labelOffset?: LabelOffset | null;
  notes?: string;
}

export interface Milestone {
  id: string;
  rowId: string;
  label: string;
  date: ISODate;
  labelPlacement?: LabelPlacement;
  labelOffset?: LabelOffset | null;
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
  /**
   * Let bars that cover the same dates share one band instead of stacking
   * into lanes — think-cell's overlap (SRS-LAY-024).
   *
   * Absent means off, which is what every document written before this
   * setting existed implies, so the field is optional and old files load.
   */
  allowOverlap?: boolean;
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
