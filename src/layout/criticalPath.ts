import type { GanttDocument, Dependency, DependencyType } from '../types/document';
import { diffDays } from '../utils/dates';

/**
 * Critical Path Method (CPM) over a Gantt DAG.
 *
 * Each task's actual `start` is anchored as its Earliest Start (ES) in day-offsets
 * from the project epoch. A forward pass propagates ES along dependency edges by
 * type (FS/SS/FF/SF). A backward pass seeds each weakly-connected component's
 * sinks with LF = component max-EF and relaxes LS upstream. Float = LS - ES;
 * tasks with float <= 0 lie on the critical path. Per-component scoping means
 * truly isolated tasks (no in/out deps) are critical on their own path. Cycles
 * are detected via Kahn's algorithm and short-circuit to an empty result.
 * Overall complexity is O(V + E).
 */
export interface CriticalPathResult {
  criticalTaskIds: Set<string>;
  taskFloat: Map<string, number>;
  projectDurationDays: number;
  hasCycle: boolean;
}

interface Edge {
  from: string;
  to: string;
  type: DependencyType;
}

const EPSILON = 1e-9;

export function computeCriticalPath(doc: GanttDocument): CriticalPathResult {
  const taskIds: string[] = doc.tasks.map((t) => t.id);
  const taskById = new Map(doc.tasks.map((t) => [t.id, t] as const));

  const emptyResult = (hasCycle: boolean): CriticalPathResult => {
    const taskFloat = new Map<string, number>();
    for (const id of taskIds) taskFloat.set(id, Infinity);
    return {
      criticalTaskIds: new Set<string>(),
      taskFloat,
      projectDurationDays: 0,
      hasCycle,
    };
  };

  if (taskIds.length === 0) {
    return {
      criticalTaskIds: new Set<string>(),
      taskFloat: new Map<string, number>(),
      projectDurationDays: 0,
      hasCycle: false,
    };
  }

  // Find project epoch (earliest task start) so all offsets are non-negative.
  let epoch = doc.tasks[0].start;
  for (const t of doc.tasks) {
    if (diffDays(epoch, t.start) < 0) epoch = t.start;
  }

  // Baseline ES from each task's actual start; duration from its actual span.
  const baselineES = new Map<string, number>();
  const duration = new Map<string, number>();
  for (const t of doc.tasks) {
    const d = diffDays(t.start, t.end);
    duration.set(t.id, d >= 0 ? d : 0);
    baselineES.set(t.id, diffDays(epoch, t.start));
  }

  // Build adjacency.
  const outEdges = new Map<string, Edge[]>();
  const inEdges = new Map<string, Edge[]>();
  for (const id of taskIds) {
    outEdges.set(id, []);
    inEdges.set(id, []);
  }
  for (const dep of doc.dependencies as Dependency[]) {
    if (!taskById.has(dep.from) || !taskById.has(dep.to)) continue;
    if (dep.from === dep.to) return emptyResult(true);
    const e: Edge = { from: dep.from, to: dep.to, type: dep.type };
    outEdges.get(dep.from)!.push(e);
    inEdges.get(dep.to)!.push(e);
  }

  // Kahn's algorithm for topological order + cycle detection.
  const inDegree = new Map<string, number>();
  for (const id of taskIds) inDegree.set(id, inEdges.get(id)!.length);
  const queue: string[] = [];
  for (const id of taskIds) if (inDegree.get(id) === 0) queue.push(id);
  const topo: string[] = [];
  while (queue.length > 0) {
    const id = queue.shift()!;
    topo.push(id);
    for (const e of outEdges.get(id)!) {
      const next = (inDegree.get(e.to) ?? 0) - 1;
      inDegree.set(e.to, next);
      if (next === 0) queue.push(e.to);
    }
  }
  if (topo.length < taskIds.length) return emptyResult(true);

  // Forward pass: ES respects baseline AND every incoming dependency constraint.
  const es = new Map<string, number>();
  const ef = new Map<string, number>();
  for (const id of topo) {
    const dur = duration.get(id)!;
    let earliest = baselineES.get(id)!;
    for (const e of inEdges.get(id)!) {
      const predES = es.get(e.from)!;
      const predEF = ef.get(e.from)!;
      let candidate: number;
      switch (e.type) {
        case 'finish-to-start':
          candidate = predEF;
          break;
        case 'start-to-start':
          candidate = predES;
          break;
        case 'finish-to-finish':
          candidate = predEF - dur;
          break;
        case 'start-to-finish':
          candidate = predES - dur;
          break;
      }
      if (candidate > earliest) earliest = candidate;
    }
    es.set(id, earliest);
    ef.set(id, earliest + dur);
  }

  // Weakly-connected components so isolated tasks are critical on their own.
  const component = new Map<string, number>();
  let nextComp = 0;
  for (const id of taskIds) {
    if (component.has(id)) continue;
    const stack = [id];
    component.set(id, nextComp);
    while (stack.length > 0) {
      const cur = stack.pop()!;
      for (const e of outEdges.get(cur)!) {
        if (!component.has(e.to)) {
          component.set(e.to, nextComp);
          stack.push(e.to);
        }
      }
      for (const e of inEdges.get(cur)!) {
        if (!component.has(e.from)) {
          component.set(e.from, nextComp);
          stack.push(e.from);
        }
      }
    }
    nextComp++;
  }

  // Per-component project duration = max EF of any task in that component.
  const compMaxEF = new Map<number, number>();
  for (const id of taskIds) {
    const c = component.get(id)!;
    const v = ef.get(id)!;
    if (!compMaxEF.has(c) || v > compMaxEF.get(c)!) compMaxEF.set(c, v);
  }

  // Backward pass: seed every task's LF with its component's max EF.
  const lf = new Map<string, number>();
  const ls = new Map<string, number>();
  for (const id of taskIds) lf.set(id, compMaxEF.get(component.get(id)!)!);
  for (let i = topo.length - 1; i >= 0; i--) {
    const id = topo[i];
    const dur = duration.get(id)!;
    let latestFinish = lf.get(id)!;
    for (const e of outEdges.get(id)!) {
      const succLS = ls.get(e.to)!;
      const succLF = lf.get(e.to)!;
      let candidate: number;
      switch (e.type) {
        case 'finish-to-start':
          candidate = succLS;
          break;
        case 'start-to-start':
          candidate = succLS + dur;
          break;
        case 'finish-to-finish':
          candidate = succLF;
          break;
        case 'start-to-finish':
          candidate = succLF + dur;
          break;
      }
      if (candidate < latestFinish) latestFinish = candidate;
    }
    lf.set(id, latestFinish);
    ls.set(id, latestFinish - dur);
  }

  // Float and critical set.
  const taskFloat = new Map<string, number>();
  const criticalTaskIds = new Set<string>();
  for (const id of taskIds) {
    const slack = ls.get(id)! - es.get(id)!;
    taskFloat.set(id, slack);
    if (slack <= EPSILON) criticalTaskIds.add(id);
  }

  let projectDurationDays = 0;
  for (const v of ef.values()) if (v > projectDurationDays) projectDurationDays = v;

  return { criticalTaskIds, taskFloat, projectDurationDays, hasCycle: false };
}
