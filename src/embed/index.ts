/**
 * Public embed API for PowerPlanner.
 *
 * Consumers (PowerNote, custom dashboards, docs) import from this barrel
 * only. Anything not re-exported here is considered internal.
 */

export { GanttRenderer } from './GanttRenderer';
export type { GanttRendererProps, GanttRendererOptions } from './GanttRenderer';

// Document types — needed for callers that want to construct or persist documents.
export type {
  GanttDocument,
  Task,
  Milestone,
  Bracket,
  Dependency,
  Marker,
  Row,
  CalendarSettings,
  StyleSettings,
  TimeScale,
  ThemeName,
  DependencyType,
  ISODate,
  BaselineSnapshot,
} from '../types/document';

// Schema utilities (so embedders can validate loaded JSON).
export { validateDocument, SchemaError } from '../persistence/schema';

// A minimal blank document factory for "Start fresh" affordances.
import type { GanttDocument } from '../types/document';
export function blankDocument(title = 'Untitled plan'): GanttDocument {
  return {
    schemaVersion: 1,
    title,
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [{ id: 'row-1', label: 'Row 1', groupId: null }],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}
