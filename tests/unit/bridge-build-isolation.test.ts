import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * Build-output gate for SRS-ABR-050.
 *
 * The portable single-file HTML is distributed to third parties. The agent
 * bridge is a remote-control channel into a live document and must not travel
 * with it — being *hidden* in production is not sufficient, it must be *absent*.
 *
 * This asserts what Vite's dead-code elimination is supposed to achieve, so a
 * refactor that turns the DEV-gated dynamic import into a static one fails here
 * rather than silently shipping the channel.
 */

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
/**
 * `index.html`, not `PowerRoadmap.html`: the former is committed (the in-app
 * updater fetches it from raw.githubusercontent.com at the release tag,
 * DECISIONS.md D9) while the latter is an untracked copy the release workflow
 * makes. Pointing at the tracked file means this gate is never skipped for
 * want of a local build — which is what it did on a fresh CI checkout.
 */
const PORTABLE = resolve(REPO_ROOT, 'dist-template/index.html');

/**
 * Connection machinery — none of this may appear.
 *
 * Note what is deliberately NOT on this list: the status-indicator markup and
 * CSS (`agent-indicator`, "agent connected"). Those are a store-bound display
 * fed by `agentActive`, which nothing in a portable build can ever set. They
 * are not a channel, and keeping them unconditional avoids a second dev-gate
 * that could drift out of sync with this one.
 */
const FORBIDDEN = [
  'AgentBridge',
  '__agent-token',
  'powerplanner-bridge',
  'ws://127.0.0.1',
  'BRIDGE_PROTOCOL_VERSION',
  'REVISION_CONFLICT',
];

describe('agent bridge build isolation (SRS-ABR-050)', () => {
  it.runIf(existsSync(PORTABLE))('portable HTML contains no bridge machinery', () => {
    const html = readFileSync(PORTABLE, 'utf8');
    const found = FORBIDDEN.filter((symbol) => html.includes(symbol));
    expect(found, `bridge symbols leaked into the portable build: ${found.join(', ')}`).toEqual([]);
  });

  it('names the build that must exist for this gate to mean anything', () => {
    // Guards against the gate silently passing because nobody ran the build:
    // if the artifact is missing, this test says so out loud.
    if (!existsSync(PORTABLE)) {
      expect.soft(
        false,
        `dist-template/index.html missing — run "npm run build:template" before trusting SRS-ABR-050`,
      ).toBe(true);
    }
    expect(PORTABLE).toContain('dist-template');
  });
});
