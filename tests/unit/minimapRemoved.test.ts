import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { useViewStore } from '../../src/stores/viewStore';

/**
 * SRS-ELEM-060 — the minimap is gone.
 *
 * It shipped on, pinned 8px from the foot of the chart — the strip the
 * floating command bar occupies — and painted behind it, so the only part ever
 * visible was the overhang past the pill's ends. No label, no title, no
 * tooltip, and its one reachable behaviour was a click on that exposed end,
 * which panned the view by half a viewport with nothing to undo it. Owner
 * report 2026-10-29: "There's like a permanent bar at the bottom that's like
 * underneath the toolbar. I don't know actually what it is."
 *
 * Decided as removal rather than redesign, so this test guards the absence:
 * the module is gone, the chart does not reference it, no view flag survives to
 * switch it back on, and the palette offers no command for it.
 */

describe('nothing paints a viewport rail (SRS-ELEM-060)', () => {
  it('has no module left to render', () => {
    expect(existsSync('src/renderer/Minimap.tsx')).toBe(false);
  });

  it('is not imported or rendered by the chart', () => {
    expect(readFileSync('src/app/ChartArea.tsx', 'utf8')).not.toContain('Minimap');
  });

  it('leaves no view-store flag to turn it back on', () => {
    const view = useViewStore.getState() as unknown as Record<string, unknown>;
    expect('showMinimap' in view).toBe(false);
    expect('setShowMinimap' in view).toBe(false);
    // The toggles that remain are the two that have something to show.
    expect('showCriticalPath' in view).toBe(true);
    expect('showBaseline' in view).toBe(true);
  });

  it('offers no command for it', () => {
    const palette = readFileSync('src/app/CommandPalette.tsx', 'utf8');
    expect(palette.toLowerCase()).not.toContain('minimap');
  });
});
