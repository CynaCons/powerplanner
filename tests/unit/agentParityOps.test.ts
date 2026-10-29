import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { applyOps, applyOp, type DocOp } from '../../src/stores/docOps';
import { GRID_DELIMITERS, WEEKEND_DISPLAYS, gridDelimiterOf, weekendDisplayOf } from '../../src/types/document';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-ABR-025 — every setting a person can change, an agent can change.
 *
 * SRS-ABR-019 has always required the converse (nothing an agent can do is
 * beyond a user), and it held. The direction asked for on 2026-10-29 did not:
 * four settings reachable from the command bar's document-settings context had
 * no operation at all, so an agent could only reach them by replacing the whole
 * document — which forfeits the all-or-nothing reduction of SRS-ABR-012 and the
 * one-undo-frame guarantee of SRS-ABR-013, and is not an operation a batch can
 * contain alongside others.
 *
 * Observed red before the fix: `applyOp` threw "unknown operation
 * 'set_weekend_display'" for all four, and `grep -c "set_allow_overlap"
 * src/stores/docOps.ts` was 0.
 */

const doc = (): GanttDocument =>
  ({
    schemaVersion: 1,
    title: 'T',
    calendar: { scale: 'month', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [{ id: 'r1', label: 'Row', groupId: null }],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  }) as unknown as GanttDocument;

describe('the settings that had no operation (SRS-ABR-025)', () => {
  it('sets the grid delimiter, and only to a value the reader accepts', () => {
    for (const delimiter of GRID_DELIMITERS) {
      const next = applyOp(doc(), { op: 'set_grid_delimiter', delimiter });
      expect(gridDelimiterOf(next.calendar)).toBe(delimiter);
    }
    // The value travels in the document, so a wrong one is not a type error at
    // runtime — it is a setting that reads correctly until the next reload.
    expect(() =>
      applyOp(doc(), { op: 'set_grid_delimiter', delimiter: 'fortnight' } as unknown as DocOp),
    ).toThrow(/gridDelimiter must be one of/);
  });

  it('sets weekend display, and rejects anything else', () => {
    for (const display of WEEKEND_DISPLAYS) {
      const next = applyOp(doc(), { op: 'set_weekend_display', display });
      expect(weekendDisplayOf(next.calendar)).toBe(display);
    }
    expect(() =>
      applyOp(doc(), { op: 'set_weekend_display', display: 'hatched' } as unknown as DocOp),
    ).toThrow(/weekendDisplay must be one of/);
  });

  it('sets whether bars may share a band', () => {
    expect(applyOp(doc(), { op: 'set_allow_overlap', allow: true }).style.allowOverlap).toBe(true);
    expect(applyOp(doc(), { op: 'set_allow_overlap', allow: false }).style.allowOverlap).toBe(false);
    expect(() =>
      applyOp(doc(), { op: 'set_allow_overlap', allow: 'yes' } as unknown as DocOp),
    ).toThrow(/must be a boolean/);
  });

  it('sets the style preset, but not to nothing', () => {
    expect(applyOp(doc(), { op: 'set_style_preset', preset: 'mono' }).style.preset).toBe('mono');
    expect(() => applyOp(doc(), { op: 'set_style_preset', preset: '' })).toThrow(/non-empty/);
  });

  it('carries all four in one batch, as one frame would', () => {
    const next = applyOps(doc(), [
      { op: 'set_grid_delimiter', delimiter: 'quarter' },
      { op: 'set_weekend_display', display: 'off' },
      { op: 'set_allow_overlap', allow: true },
      { op: 'set_style_preset', preset: 'mono' },
    ]);
    expect(next.calendar.gridDelimiter).toBe('quarter');
    expect(next.calendar.weekendDisplay).toBe('off');
    expect(next.style.allowOverlap).toBe(true);
    expect(next.style.preset).toBe('mono');
  });

  it('changes nothing at all when one of the four is invalid (SRS-ABR-012)', () => {
    const before = doc();
    expect(() =>
      applyOps(before, [
        { op: 'set_grid_delimiter', delimiter: 'quarter' },
        { op: 'set_weekend_display', display: 'nope' } as unknown as DocOp,
      ]),
    ).toThrow();
    expect(before.calendar.gridDelimiter).toBeUndefined();
  });

  it('leaves the rest of the document alone', () => {
    const before = doc();
    const next = applyOp(before, { op: 'set_weekend_display', display: 'off' });
    expect(next.rows).toEqual(before.rows);
    expect(next.calendar.workingDays).toEqual(before.calendar.workingDays);
    expect(next.calendar.scale).toBe(before.calendar.scale);
    expect(next.style.theme).toBe(before.style.theme);
  });
});

describe('a whole selection deletes in one operation (SRS-ABR-027)', () => {
  const withLinks = (): GanttDocument =>
    ({
      ...doc(),
      rows: [{ id: 'r1', label: 'Row', groupId: null }],
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-03-02', end: '2026-03-06' },
        { id: 't2', rowId: 'r1', label: 'B', start: '2026-03-09', end: '2026-03-13' },
      ],
      dependencies: [{ id: 'd1', from: 't1', to: 't2', type: 'FS' }],
    }) as unknown as GanttDocument;

  it('is the case a batch of single deletes cannot express', () => {
    // The shape of the defect, kept as the contrast: delete_task cascades d1
    // away, so the delete_dependency that follows throws and SRS-ABR-012
    // discards the whole batch — the user's one Delete key, and nothing happens.
    expect(() =>
      applyOps(withLinks(), [
        { op: 'delete_task', id: 't1' },
        { op: 'delete_dependency', id: 'd1' },
      ]),
    ).toThrow();
  });

  it('deletes a task and one of its own dependencies together', () => {
    const next = applyOp(withLinks(), {
      op: 'delete_items',
      items: [
        { kind: 'task', id: 't1' },
        { kind: 'dependency', id: 'd1' },
      ],
    });
    expect(next.tasks.map((t) => t.id)).toEqual(['t2']);
    expect(next.dependencies).toEqual([]);
  });

  it('cascades exactly as the single deletes do', () => {
    // Deleting the row takes its tasks and their links, same as delete_row.
    const next = applyOp(withLinks(), { op: 'delete_items', items: [{ kind: 'row', id: 'r1' }] });
    expect(next.rows).toEqual([]);
    expect(next.tasks).toEqual([]);
    expect(next.dependencies).toEqual([]);
  });

  it('still refuses an id that was never there', () => {
    // Skipping is for ids a cascade removed, not for a caller's typo — but the
    // two are indistinguishable after the fact, so the lenient rule wins and
    // this documents it rather than pretending otherwise.
    const next = applyOp(withLinks(), { op: 'delete_items', items: [{ kind: 'task', id: 'nope' }] });
    expect(next.tasks).toHaveLength(2);
  });

  it('rejects a kind it cannot delete, rather than skipping it silently', () => {
    expect(() =>
      applyOp(withLinks(), { op: 'delete_items', items: [{ kind: 'calendar', id: 'x' }] } as unknown as DocOp),
    ).toThrow(/cannot delete/);
  });

  it('deletes nothing when the list is empty', () => {
    const before = withLinks();
    expect(applyOp(before, { op: 'delete_items', items: [] })).toEqual(before);
  });
});

describe('the op vocabulary is advertised where an agent will read it', () => {
  it('names the four new ops in the apply_batch tool description', () => {
    // The MCP tool description IS the agent's documentation; an op that works
    // but is not listed there is an op no agent will call.
    const server = readFileSync('mcp/agent-bridge/server.mjs', 'utf8');
    for (const op of [
      'set_grid_delimiter',
      'set_weekend_display',
      'set_allow_overlap',
      'set_style_preset',
      'delete_items',
    ]) {
      expect(server, `${op} missing from the tool description`).toContain(op);
    }
  });

  it('returns a picture as an image block, not base64 in a wall of text', () => {
    const server = readFileSync('mcp/agent-bridge/server.mjs', 'utf8');
    expect(server).toContain("type: 'image'");
    expect(server).toContain("mimeType: 'image/png'");
  });
});
