import { describe, it, expect } from 'vitest';
import { fileBindingLabel, windowTitle } from '../../src/app/StatusBar';

/**
 * SRS-PERS-029 — which file, and whether it holds what is on screen.
 *
 * The binding is a module-level handle inside `fileIo` that nothing could read
 * reactively, and the dirty flag was consulted only by the New and Open
 * guards: the app knew both facts and stated neither. Owner report 2026-10-29:
 * "I am missing the information about which file I am connected to and whether
 * I am connected to a file or not."
 *
 * Observed red on 79ccbdb: the status bar carried stats, scale, selection
 * count and the version, and no filename anywhere; `document.title` was the
 * build-time string for every document the app ever opened.
 */

describe('the footer states the binding (SRS-PERS-029)', () => {
  it('names the file it is bound to', () => {
    const b = fileBindingLabel('roadmap-q3.html', false);
    expect(b.text).toBe('roadmap-q3.html');
    expect(b.title).toContain('Saved to roadmap-q3.html');
    expect(b.unsaved).toBe(false);
  });

  it('says so when that file is behind what is on screen', () => {
    const b = fileBindingLabel('roadmap-q3.html', true);
    expect(b.text).toContain('roadmap-q3.html');
    expect(b.text).toContain('unsaved');
    expect(b.title).toContain('Ctrl+S');
    expect(b.unsaved).toBe(true);
  });

  it('distinguishes "no file" from "unsaved", which used to be one silence', () => {
    expect(fileBindingLabel(null, false).text).toBe('No file');
    expect(fileBindingLabel(null, true).text).toContain('unsaved');
    expect(fileBindingLabel(null, false).title).toContain('Ctrl+S');
  });
});

describe('the window title carries the same two facts (SRS-PERS-029)', () => {
  it('leads with the file when there is one', () => {
    expect(windowTitle('roadmap-q3.html', 'Q3 plan', false)).toBe('roadmap-q3.html — PowerRoadmap');
  });

  it('marks unsaved work where a taskbar entry shows it', () => {
    expect(windowTitle('roadmap-q3.html', 'Q3 plan', true)).toBe('• roadmap-q3.html — PowerRoadmap');
  });

  it('falls back to the plan\'s own name, then to a plain one', () => {
    expect(windowTitle(null, 'Q3 plan', false)).toBe('Q3 plan — PowerRoadmap');
    expect(windowTitle(null, '   ', false)).toBe('Untitled roadmap — PowerRoadmap');
  });
});
