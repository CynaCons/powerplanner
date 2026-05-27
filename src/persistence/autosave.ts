import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';

const KEY = 'powerplanner:autosave:v1';

export function loadAutosave(): GanttDocument | null {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    return validateDocument(JSON.parse(raw));
  } catch {
    return null;
  }
}

export function saveAutosave(doc: GanttDocument): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(doc));
  } catch {
    // Ignore quota errors
  }
}

export function clearAutosave(): void {
  try {
    localStorage.removeItem(KEY);
  } catch {
    // ignore
  }
}
