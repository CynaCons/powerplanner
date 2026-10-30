import { describe, it, expect } from 'vitest';
import { mayAutosave } from '../../src/persistence/fileHandleStore';

/**
 * SRS-PERS-037 — the app keeps the bound file level with the screen, and only
 * where it is already allowed to.
 *
 * Each `false` here is a way to lose a file quietly. No binding: nothing to
 * write, and guessing one would pick the last plan the planner had open. A
 * binding for another file: that is SRS-PERS-036, a copy writing its original.
 * `prompt`: the ordinary state of a handle recalled from a previous session —
 * writing anyway is not possible, and ASKING is not possible either, because
 * the dialog needs a user gesture this timer does not have. It would be
 * refused with nothing on screen, which is why the answer is no and the
 * footer says `needs-permission` until the next Ctrl+S.
 */

const served = { protocol: 'http:', pathname: '/' };
const here = { name: 'plan.html' };

describe('mayAutosave', () => {
  it('writes when the file is bound and already permitted', () => {
    expect(mayAutosave(here, 'granted', served)).toBe(true);
  });

  it('does not write when nothing is bound', () => {
    expect(mayAutosave(null, 'granted', served)).toBe(false);
  });

  it('does not write while permission is merely promptable', () => {
    // The state every restored binding starts in.
    expect(mayAutosave(here, 'prompt', served)).toBe(false);
  });

  it('does not write when permission was denied', () => {
    expect(mayAutosave(here, 'denied', served)).toBe(false);
  });

  it('does not write when the permission could not be read at all', () => {
    // An older browser, or a handle whose query threw: unknown is not yes.
    expect(mayAutosave(here, 'unknown', served)).toBe(false);
  });

  it('does not write a copy over the file it was copied from', () => {
    const copy = { protocol: 'file:', pathname: '/C:/plans/plan%20(copy).html' };
    expect(mayAutosave(here, 'granted', copy)).toBe(false);
    expect(mayAutosave({ name: 'plan (copy).html' }, 'granted', copy)).toBe(true);
  });
});
