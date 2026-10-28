import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { useKeyboardShortcuts } from '../../src/app/useKeyboardShortcuts';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useViewStore } from '../../src/stores/viewStore';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-EDIT-040 / SRS-EDIT-041 — one declared keymap, matched as a full chord.
 *
 * The behaviour tests below talk to the shipped hook only through `window` and
 * the stores, so they were runnable — and red — before `keymap.ts` existed.
 *
 * Observed red on bf3f4ae (the if-chain over bare `e.key`):
 *   Shift+M  inserted a milestone and left the scale on 'day'; the documented
 *            ⇧M → Month was unreachable, shadowed by the bare-'m' branch.
 *   Ctrl+N   inserted a task and suppressed the browser's "new".
 *   Cmd+M    inserted a milestone.
 *   Alt+B    inserted a bracket (`altKey` was read nowhere in src/).
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

let root: Root;
let host: HTMLDivElement;

function Harness() {
  useKeyboardShortcuts();
  return null;
}

function press(init: KeyboardEventInit) {
  act(() => {
    window.dispatchEvent(new KeyboardEvent('keydown', { bubbles: true, cancelable: true, ...init }));
  });
}

function doc() {
  return useDocumentStore.getState().doc;
}

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  useViewStore.setState({ detailsOpen: false, newRoadmapOpen: false });
  const fresh = sampleDocument();
  fresh.calendar.scale = 'day';
  act(() => {
    useDocumentStore.getState().setDocument(fresh);
  });
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
  act(() => {
    root.render(createElement(Harness));
  });
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
});

describe('chords are matched whole (SRS-EDIT-040)', () => {
  it('Shift+M reaches the month scale instead of inserting a milestone', () => {
    const before = doc().milestones.length;
    press({ key: 'M', shiftKey: true });
    expect(doc().calendar.scale).toBe('month');
    expect(doc().milestones.length).toBe(before);
  });

  it('Ctrl+N does not insert a task', () => {
    const before = doc().tasks.length;
    press({ key: 'n', ctrlKey: true });
    expect(doc().tasks.length).toBe(before);
  });

  it('Cmd+M does not insert a milestone', () => {
    const before = doc().milestones.length;
    press({ key: 'm', metaKey: true });
    expect(doc().milestones.length).toBe(before);
  });

  it('Alt+B does not insert a bracket', () => {
    const before = doc().brackets.length;
    press({ key: 'b', altKey: true });
    expect(doc().brackets.length).toBe(before);
  });

  it('the bare keys still do their job', () => {
    const milestones = doc().milestones.length;
    press({ key: 'm' });
    expect(doc().milestones.length).toBe(milestones + 1);

    const tasks = doc().tasks.length;
    press({ key: 'n' });
    expect(doc().tasks.length).toBe(tasks + 1);

    const brackets = doc().brackets.length;
    press({ key: 'b' });
    expect(doc().brackets.length).toBe(brackets + 1);
  });

  it('an unclaimed chord is left to the browser', () => {
    const e = new KeyboardEvent('keydown', { key: 'n', ctrlKey: true, bubbles: true, cancelable: true });
    act(() => {
      window.dispatchEvent(e);
    });
    expect(e.defaultPrevented).toBe(false);
  });

  it('a teaching sheet holds the keyboard', () => {
    useViewStore.setState({ shortcutsOpen: true });
    const before = doc().milestones.length;
    press({ key: 'm' });
    expect(doc().milestones.length).toBe(before);
    useViewStore.setState({ shortcutsOpen: false });
  });
});
