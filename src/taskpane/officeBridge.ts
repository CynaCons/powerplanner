/**
 * Office.js bridge for PowerPoint shape emission + round-trip.
 *
 * Insert: walks the GanttDocument and adds native PowerPoint shapes —
 * rectangles for tasks, diamonds for milestones, elbow connectors for
 * dependencies. All shapes are tagged so we can read them back later.
 *
 * Pull: enumerates shapes on the active slide, finds the PowerPlanner
 * root group, and parses the embedded JSON document tag.
 *
 * PowerPoint slide coordinates are POINTS (1pt = 1/72"), not EMU. The
 * 16:9 slide canvas is 960 x 540 pt; 4:3 is 720 x 540 pt.
 */

import type { GanttDocument } from '../types/document';
import { validateDocument } from '../persistence/schema';
import { layoutDocument } from '../layout/engine';
import { addDays, diffDays, todayISO } from '../utils/dates';

// Minimal Office.js typing — we declare what we use to avoid pulling
// the full @types/office-js into the runtime path.
declare const Office: { context?: { host?: unknown } };
declare const PowerPoint: {
  run: <T>(cb: (ctx: PPContext) => Promise<T>) => Promise<T>;
  GeometricShapeType: { rectangle: unknown; diamond: unknown };
  ConnectorType: { elbowConnector: unknown };
  ArrowheadStyle: { none: unknown; triangle: unknown };
};

interface PPShapeFill { setSolidColor: (hex: string) => void }
interface PPShapeLine { color: string; weight: number; beginArrowheadStyle?: unknown; endArrowheadStyle?: unknown }
interface PPTextRange { text: string }
interface PPTextFrame { textRange: PPTextRange }
interface PPTagsCollection {
  add: (key: string, value: string) => void;
  items: Array<{ key: string; value: string }>;
}
interface PPShape {
  id: string;
  type: string;
  fill: PPShapeFill;
  lineFormat: PPShapeLine;
  textFrame: PPTextFrame;
  tags: PPTagsCollection;
}
interface PPShapeCollection {
  addGeometricShape: (kind: unknown, opts: { left: number; top: number; width: number; height: number }) => PPShape;
  addLine: (kind: unknown, opts: { left: number; top: number; width: number; height: number }) => PPShape;
  addTextBox: (text: string, opts: { left: number; top: number; width: number; height: number }) => PPShape;
  addGroup: (shapes: PPShape[]) => PPShape;
  items: PPShape[];
  load: (props: string) => void;
}
interface PPSlide { shapes: PPShapeCollection }
interface PPSlideCollection { getItemAt: (i: number) => PPSlide }
interface PPContext {
  presentation: { getSelectedSlides: () => PPSlideCollection };
  sync: () => Promise<void>;
}

// Slide canvas constants (16:9 default = 960 x 540 pt)
const SLIDE_W_PT = 960;
const MARGIN_PT = 36;
const CHART_W_PT = SLIDE_W_PT - MARGIN_PT * 2;
const ROW_GUTTER_PT = 100;
const ROW_HEIGHT_PT = 28;
const AXIS_H_PT = 22;
const BAR_INSET_PT = 5;

export function isOfficeAvailable(): boolean {
  return typeof Office !== 'undefined' && !!Office?.context?.host;
}

export interface InsertResult { shapeCount: number }

export async function insertGanttIntoSlide(doc: GanttDocument): Promise<InsertResult> {
  if (typeof PowerPoint === 'undefined') {
    throw new Error('PowerPoint API is unavailable. Open this pane inside PowerPoint.');
  }

  // Compute the projection: dates in the doc → slide coordinates in points.
  const dates = [
    ...doc.tasks.flatMap((t) => [t.start, t.end]),
    ...doc.milestones.map((m) => m.date),
  ];
  if (dates.length === 0 && doc.rows.length === 0) {
    throw new Error('Document is empty — nothing to insert.');
  }

  const minDate = dates.length > 0 ? dates.reduce((a, b) => (a < b ? a : b)) : todayISO();
  const maxDate = dates.length > 0 ? dates.reduce((a, b) => (a > b ? a : b)) : addDays(minDate, 30);
  const totalDays = Math.max(1, diffDays(minDate, maxDate) + 1);
  const padDays = Math.max(1, Math.ceil(totalDays * 0.05));
  const viewStart = addDays(minDate, -padDays);
  const chartContentW = CHART_W_PT - ROW_GUTTER_PT;
  const ptPerDay = chartContentW / (totalDays + padDays * 2);

  // Use existing layout engine, but in pt units. Scale viewport into points.
  const layout = layoutDocument({ doc, viewStart, pxPerDay: ptPerDay });

  const rowsToUse = layout.visibleRows;
  const chartTop = MARGIN_PT + AXIS_H_PT;
  const dateToPt = (iso: string) => MARGIN_PT + ROW_GUTTER_PT + diffDays(viewStart, iso) * ptPerDay;
  const rowTopPt = (i: number) => chartTop + i * ROW_HEIGHT_PT;

  let shapeCount = 0;

  await PowerPoint.run(async (ctx) => {
    const slide = ctx.presentation.getSelectedSlides().getItemAt(0);
    const shapes = slide.shapes;
    const emitted: PPShape[] = [];

    // Title
    const title = shapes.addTextBox(doc.title || 'Plan', {
      left: MARGIN_PT,
      top: MARGIN_PT - 18,
      width: CHART_W_PT,
      height: 18,
    });
    title.tags.add('PP_KIND', 'TITLE');
    emitted.push(title);
    shapeCount++;

    // Row labels (left gutter)
    rowsToUse.forEach((row, i) => {
      const label = shapes.addTextBox(row.label, {
        left: MARGIN_PT,
        top: rowTopPt(i) + 4,
        width: ROW_GUTTER_PT - 8,
        height: 18,
      });
      label.tags.add('PP_KIND', 'ROW_LABEL');
      label.tags.add('PP_ID', row.id);
      emitted.push(label);
      shapeCount++;
    });

    // Task bars
    const tasksByRow = new Map(rowsToUse.map((r, i) => [r.id, i] as const));
    for (const task of doc.tasks) {
      const rowIdx = tasksByRow.get(task.rowId);
      if (rowIdx === undefined) continue;
      const left = dateToPt(task.start);
      const right = dateToPt(task.end) + ptPerDay;
      const width = Math.max(2, right - left);
      const top = rowTopPt(rowIdx) + BAR_INSET_PT;
      const height = ROW_HEIGHT_PT - BAR_INSET_PT * 2;

      const bar = shapes.addGeometricShape(PowerPoint.GeometricShapeType.rectangle, {
        left, top, width, height,
      });
      bar.fill.setSolidColor(task.color || '#818CF8');
      bar.lineFormat.color = task.color || '#4338CA';
      bar.lineFormat.weight = 0.5;
      if (width > 60) bar.textFrame.textRange.text = task.label;
      bar.tags.add('PP_KIND', 'TASK');
      bar.tags.add('PP_ID', task.id);
      emitted.push(bar);
      shapeCount++;
    }

    // Milestones
    for (const ms of doc.milestones) {
      const rowIdx = tasksByRow.get(ms.rowId);
      if (rowIdx === undefined) continue;
      const cx = dateToPt(ms.date) + ptPerDay / 2;
      const cy = rowTopPt(rowIdx) + ROW_HEIGHT_PT / 2;
      const sz = 12;
      const diamond = shapes.addGeometricShape(PowerPoint.GeometricShapeType.diamond, {
        left: cx - sz / 2, top: cy - sz / 2, width: sz, height: sz,
      });
      diamond.fill.setSolidColor(ms.color || '#FBBF24');
      diamond.lineFormat.color = '#000000';
      diamond.lineFormat.weight = 0.5;
      diamond.tags.add('PP_KIND', 'MILESTONE');
      diamond.tags.add('PP_ID', ms.id);
      emitted.push(diamond);
      shapeCount++;
    }

    // Dependencies (elbow connectors)
    const taskById = new Map(doc.tasks.map((t) => [t.id, t] as const));
    for (const dep of doc.dependencies) {
      const from = taskById.get(dep.from);
      const to = taskById.get(dep.to);
      if (!from || !to) continue;
      const fromRowIdx = tasksByRow.get(from.rowId);
      const toRowIdx = tasksByRow.get(to.rowId);
      if (fromRowIdx === undefined || toRowIdx === undefined) continue;
      const fromX = dateToPt(from.end) + ptPerDay;
      const fromY = rowTopPt(fromRowIdx) + ROW_HEIGHT_PT / 2;
      const toX = dateToPt(to.start);
      const toY = rowTopPt(toRowIdx) + ROW_HEIGHT_PT / 2;
      const left = Math.min(fromX, toX);
      const top = Math.min(fromY, toY);
      const width = Math.max(2, Math.abs(toX - fromX));
      const height = Math.max(2, Math.abs(toY - fromY));
      const arrow = shapes.addLine(PowerPoint.ConnectorType.elbowConnector, { left, top, width, height });
      arrow.lineFormat.color = '#94A3B8';
      arrow.lineFormat.weight = 0.75;
      if ('endArrowheadStyle' in arrow.lineFormat) {
        arrow.lineFormat.endArrowheadStyle = PowerPoint.ArrowheadStyle.triangle;
      }
      arrow.tags.add('PP_KIND', 'DEP');
      arrow.tags.add('PP_ID', dep.id);
      emitted.push(arrow);
      shapeCount++;
    }

    await ctx.sync();

    // Group everything + attach round-trip blob to the group
    const group = shapes.addGroup(emitted);
    group.tags.add('PP_KIND', 'CHART_ROOT');
    group.tags.add('PP_VERSION', String(doc.schemaVersion));
    group.tags.add('PP_DOC', JSON.stringify(doc));
    await ctx.sync();
  });

  return { shapeCount };
}

export async function readGanttFromSlide(): Promise<GanttDocument | null> {
  if (typeof PowerPoint === 'undefined') {
    throw new Error('PowerPoint API is unavailable. Open this pane inside PowerPoint.');
  }

  return PowerPoint.run(async (ctx) => {
    const slide = ctx.presentation.getSelectedSlides().getItemAt(0);
    slide.shapes.load('items/type,items/id,items/tags/key,items/tags/value');
    await ctx.sync();

    const root = slide.shapes.items.find((s) =>
      s.tags.items.some((t) => t.key === 'PP_KIND' && t.value === 'CHART_ROOT'),
    );
    if (!root) return null;

    const docTag = root.tags.items.find((t) => t.key === 'PP_DOC');
    if (!docTag) return null;

    try {
      const parsed = JSON.parse(docTag.value);
      return validateDocument(parsed);
    } catch (err) {
      throw new Error(`Invalid PP_DOC tag on slide: ${(err as Error).message}`);
    }
  });
}
