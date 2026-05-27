import type { GanttDocument } from '../types/document';

export const SCHEMA_VERSION = 1 as const;

export class SchemaError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'SchemaError';
  }
}

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isString(v: unknown): v is string {
  return typeof v === 'string';
}

function isNumber(v: unknown): v is number {
  return typeof v === 'number' && Number.isFinite(v);
}

function isISODate(v: unknown): v is string {
  return isString(v) && /^\d{4}-\d{2}-\d{2}$/.test(v);
}

export function validateDocument(input: unknown): GanttDocument {
  if (!isObject(input)) throw new SchemaError('Document must be an object');
  const schemaVersion = (input as { schemaVersion?: unknown }).schemaVersion;
  if (schemaVersion !== 1) {
    throw new SchemaError(`Unsupported schemaVersion: ${schemaVersion}`);
  }
  const title = (input as { title?: unknown }).title;
  if (!isString(title)) throw new SchemaError('title must be a string');

  const calendar = (input as { calendar?: unknown }).calendar;
  if (!isObject(calendar)) throw new SchemaError('calendar must be an object');
  const scale = calendar.scale;
  if (scale !== 'day' && scale !== 'week' && scale !== 'month' && scale !== 'quarter' && scale !== 'year') {
    throw new SchemaError(`Invalid scale: ${String(scale)}`);
  }
  const fiscalYearStart = calendar.fiscalYearStart;
  if (!isNumber(fiscalYearStart) || fiscalYearStart < 1 || fiscalYearStart > 12) {
    throw new SchemaError('fiscalYearStart must be 1..12');
  }
  const workingDays = calendar.workingDays;
  if (!Array.isArray(workingDays) || !workingDays.every((n) => isNumber(n) && n >= 0 && n <= 6)) {
    throw new SchemaError('workingDays must be an array of 0..6');
  }
  const holidays = calendar.holidays;
  if (!Array.isArray(holidays) || !holidays.every(isISODate)) {
    throw new SchemaError('holidays must be an array of YYYY-MM-DD');
  }

  const rows = (input as { rows?: unknown }).rows;
  if (!Array.isArray(rows)) throw new SchemaError('rows must be an array');
  rows.forEach((r, i) => {
    if (!isObject(r) || !isString(r.id) || !isString(r.label)) {
      throw new SchemaError(`rows[${i}] invalid`);
    }
    if (r.groupId !== null && r.groupId !== undefined && !isString(r.groupId)) {
      throw new SchemaError(`rows[${i}].groupId invalid`);
    }
  });

  const tasks = (input as { tasks?: unknown }).tasks;
  if (!Array.isArray(tasks)) throw new SchemaError('tasks must be an array');
  tasks.forEach((t, i) => {
    if (!isObject(t) || !isString(t.id) || !isString(t.rowId) || !isString(t.label) || !isISODate(t.start) || !isISODate(t.end)) {
      throw new SchemaError(`tasks[${i}] invalid`);
    }
    if (t.percentComplete !== undefined && (!isNumber(t.percentComplete) || t.percentComplete < 0 || t.percentComplete > 100)) {
      throw new SchemaError(`tasks[${i}].percentComplete out of range`);
    }
  });

  const milestones = (input as { milestones?: unknown }).milestones;
  if (!Array.isArray(milestones)) throw new SchemaError('milestones must be an array');
  milestones.forEach((m, i) => {
    if (!isObject(m) || !isString(m.id) || !isString(m.rowId) || !isString(m.label) || !isISODate(m.date)) {
      throw new SchemaError(`milestones[${i}] invalid`);
    }
  });

  const brackets = (input as { brackets?: unknown }).brackets;
  if (!Array.isArray(brackets)) throw new SchemaError('brackets must be an array');
  brackets.forEach((b, i) => {
    if (!isObject(b) || !isString(b.id) || !isString(b.label) || !isISODate(b.start) || !isISODate(b.end) || !Array.isArray(b.rowIds)) {
      throw new SchemaError(`brackets[${i}] invalid`);
    }
  });

  const dependencies = (input as { dependencies?: unknown }).dependencies;
  if (!Array.isArray(dependencies)) throw new SchemaError('dependencies must be an array');
  dependencies.forEach((d, i) => {
    if (!isObject(d) || !isString(d.id) || !isString(d.from) || !isString(d.to) || !isString(d.type)) {
      throw new SchemaError(`dependencies[${i}] invalid`);
    }
  });

  const markers = (input as { markers?: unknown }).markers;
  if (!Array.isArray(markers)) throw new SchemaError('markers must be an array');
  markers.forEach((m, i) => {
    if (!isObject(m) || !isString(m.id) || !isString(m.type) || !isString(m.label) || !isISODate(m.date)) {
      throw new SchemaError(`markers[${i}] invalid`);
    }
  });

  const style = (input as { style?: unknown }).style;
  if (!isObject(style) || !isString(style.theme) || !isString(style.preset)) {
    throw new SchemaError('style invalid');
  }

  return input as unknown as GanttDocument;
}
