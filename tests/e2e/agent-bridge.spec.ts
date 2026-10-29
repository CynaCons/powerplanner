import { test, expect, type Page } from '@playwright/test';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * End-to-end proof for the web agent bridge: a real browser, a real bridge
 * server, real MCP tool calls over stdio.
 *
 * These assertions cover the properties that pure unit tests cannot observe —
 * that a batch authored by an agent actually renders, that one Ctrl+Z reverts
 * the whole turn, and that a concurrent user edit is reported rather than
 * silently overwritten (SRS-ABR-012/013/021/022).
 *
 * Only runs on the desktop project: the bridge is a desktop dev tool and the
 * mobile/tablet projects would spawn competing servers on the same port.
 */

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const BRIDGE_PORT = 5219; // clear of the default so a dev bridge can stay running

/** Minimal MCP stdio client — the same surface an agent host speaks. */
class McpClient {
  private child: ChildProcessWithoutNullStreams;
  private nextId = 1;
  private buffer = '';
  private waiters = new Map<number, (value: unknown) => void>();

  constructor() {
    this.child = spawn('node', ['mcp/agent-bridge/server.mjs'], {
      cwd: REPO_ROOT,
      env: {
        ...process.env,
        POWERPLANNER_BRIDGE_PORT: String(BRIDGE_PORT),
        POWERPLANNER_BRIDGE_ORIGINS: 'http://localhost:5180,http://127.0.0.1:5180',
      },
    }) as ChildProcessWithoutNullStreams;

    this.child.stdout.setEncoding('utf8');
    this.child.stdout.on('data', (chunk: string) => {
      this.buffer += chunk;
      let index: number;
      while ((index = this.buffer.indexOf('\n')) !== -1) {
        const line = this.buffer.slice(0, index).trim();
        this.buffer = this.buffer.slice(index + 1);
        if (!line) continue;
        const message = JSON.parse(line) as { id: number };
        this.waiters.get(message.id)?.(message);
        this.waiters.delete(message.id);
      }
    });
  }

  async ready(): Promise<void> {
    await this.request('initialize', {});
    // Give the WebSocket listener a moment to bind before the page connects.
    await new Promise((r) => setTimeout(r, 300));
  }

  request(method: string, params: unknown): Promise<{
    result?: { content?: Array<{ type?: string; text?: string; data?: string; mimeType?: string }>; isError?: boolean };
  }> {
    const id = this.nextId++;
    return new Promise((resolvePromise) => {
      this.waiters.set(id, resolvePromise as (value: unknown) => void);
      this.child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
    });
  }

  /**
   * Call a tool and return its parsed payload plus whether it reported an error.
   *
   * The text block is found by type rather than taken from index 0: a render
   * puts an image block first (SRS-ABR-034), and indexing blindly would read
   * the picture's base64 as if it were the JSON.
   */
  async call(name: string, args: Record<string, unknown> = {}) {
    const response = await this.request('tools/call', { name, arguments: args });
    const content = response.result?.content ?? [];
    const text = content.find((c) => c.type === 'text' || typeof c.text === 'string')?.text ?? '{}';
    return {
      isError: response.result?.isError === true,
      payload: JSON.parse(text) as Record<string, unknown>,
      image: content.find((c) => c.type === 'image'),
      blocks: content.map((c) => c.type ?? 'text'),
    };
  }

  dispose(): void {
    this.child.kill();
  }
}

async function connectBridge(page: Page): Promise<void> {
  await page.goto('/');
  await page.getByRole('button', { name: /agent/i }).click();
  // Narrowed to the agent chip: the status bar now also carries the one
  // live region that reports file and edit outcomes (SRS-PERS-025).
  await expect(page.locator('.app-status .agent-indicator')).toContainText('agent connected', { timeout: 10_000 });
}

function plan(rows: number, tasksPerRow: number) {
  const ops: Array<Record<string, unknown>> = [];
  for (let r = 0; r < rows; r++) {
    ops.push({ op: 'add_row', row: { id: `r${r}`, label: `Phase ${r + 1}`, groupId: null } });
    for (let t = 0; t < tasksPerRow; t++) {
      const day = 3 + r * 7 + t * 3;
      ops.push({
        op: 'add_task',
        task: {
          id: `r${r}t${t}`,
          rowId: `r${r}`,
          label: `Task ${r + 1}.${t + 1}`,
          start: `2026-03-${String(day).padStart(2, '0')}`,
          end: `2026-03-${String(day + 2).padStart(2, '0')}`,
        },
      });
    }
  }
  return ops;
}

test.describe('web agent bridge', () => {
  // Serial: each test spawns its own bridge server, and they would otherwise
  // contend for the same loopback port.
  test.describe.configure({ mode: 'serial' });

  let mcp: McpClient;

  test.beforeEach(async ({ page: _page }, testInfo) => {
    test.skip(testInfo.project.name !== 'desktop', 'bridge is a desktop dev tool');
    mcp = new McpClient();
    await mcp.ready();
  });

  test.afterEach(() => {
    mcp?.dispose();
  });

  test('reports NO_CLIENT before any page connects (SRS-ABR-004)', async () => {
    const { isError, payload } = await mcp.call('get_status');
    expect(isError).toBe(true);
    expect(payload.error).toBe('NO_CLIENT');
  });

  test('authors a 12-task plan that actually renders (SRS-ABR-010)', async ({ page }) => {
    await connectBridge(page);

    const cleared = await mcp.call('new_document', { title: 'Agent plan' });
    expect(cleared.isError).toBe(false);

    const ops = plan(4, 3);
    const applied = await mcp.call('apply_batch', { ops });
    expect(applied.isError).toBe(false);
    expect(applied.payload.applied).toBe(ops.length);

    // The document is not merely accepted — it is on screen.
    await expect(page.locator('.chart-area svg .task-bar-body')).toHaveCount(12);
    await expect(page.locator('.app-status')).toContainText('12 tasks');
  });

  test('one Ctrl+Z reverts the entire agent batch (SRS-ABR-013)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('new_document', { title: 'Undo test' });
    await mcp.call('apply_batch', { ops: plan(4, 3) });
    await expect(page.locator('.chart-area svg .task-bar-body')).toHaveCount(12);

    await page.keyboard.press('Control+z');

    // Not 11 — the whole turn.
    await expect(page.locator('.chart-area svg .task-bar-body')).toHaveCount(0);
  });

  test('an invalid batch applies nothing (SRS-ABR-012)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('new_document', { title: 'Atomicity' });

    const { isError, payload } = await mcp.call('apply_batch', {
      ops: [
        { op: 'add_row', row: { id: 'ok', label: 'Fine', groupId: null } },
        { op: 'add_task', task: { id: 't1', rowId: 'ghost', label: 'Bad', start: '2026-03-01', end: '2026-03-05' } },
      ],
    });

    expect(isError).toBe(true);
    expect(String(payload.message)).toContain('ghost');

    const status = await mcp.call('get_status');
    expect((status.payload.counts as Record<string, number>).rows).toBe(1); // blankDocument's row only
    expect((status.payload.counts as Record<string, number>).tasks).toBe(0);
  });

  test('a concurrent user edit is reported, not overwritten (SRS-ABR-021/022)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('new_document', { title: 'Conflict' });

    const before = await mcp.call('get_status');
    const staleRevision = before.payload.revision as number;

    // The user renames the chart while the agent is mid-thought.
    await page.getByLabel('Chart title').fill('Mine, actually');

    const { isError, payload } = await mcp.call('apply_batch', {
      ops: [{ op: 'add_row', row: { id: 'late', label: 'Late', groupId: null } }],
      expectedRevision: staleRevision,
    });

    expect(isError).toBe(true);
    expect(payload.error).toBe('REVISION_CONFLICT');
    // The user's edit stands.
    await expect(page.getByLabel('Chart title')).toHaveValue('Mine, actually');
  });

  test('retrying a timed-out mutation with the same opId does not double-apply (SRS-ABR-023)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('new_document', { title: 'Idempotency' });

    const opId = 'op-fixed-for-test';
    const ops = [{ op: 'add_row', row: { id: 'once', label: 'Once', groupId: null } }];

    const first = await mcp.call('apply_batch', { ops, opId });
    expect(first.isError).toBe(false);

    const retry = await mcp.call('apply_batch', { ops, opId });
    expect(retry.isError).toBe(false);
    expect(retry.payload.replayed).toBe(true);

    // Applied once, despite two calls — and no duplicate-id failure either.
    const status = await mcp.call('get_status');
    expect((status.payload.counts as Record<string, number>).rows).toBe(2); // blank row + 'once'
  });

  test('survives the bridge server vanishing mid-session (SRS-ABR-003)', async ({ page }) => {
    // Regression gate for the two defects the live run found and the rest of
    // this suite could not: the suite otherwise never enters the state where a
    // session outlives its server.
    await connectBridge(page);
    await mcp.call('new_document', { title: 'Reconnect' });
    await mcp.call('apply_batch', { ops: plan(1, 2) });
    await expect(page.locator('.chart-area svg .task-bar-body')).toHaveCount(2);

    // Server dies. On Windows this kill bypasses the SIGTERM cleanup handler,
    // which is precisely how the stale-token-file defect arose.
    mcp.dispose();
    await expect(page.locator('.app-status .agent-indicator')).toHaveCount(0, { timeout: 15_000 });

    // Stay down long enough that several backoff attempts (500/1000/2000/4000ms)
    // run against a DEAD server. This window is the whole point of the test: it
    // is where the token endpoint 503s and connect() rejects before a socket
    // exists. Without the gap the new server comes back so fast that the
    // rejecting path is never entered and the test passes even when broken —
    // verified by removing the fix and watching a gapless version still pass.
    await page.waitForTimeout(6000);

    // A fresh server issues a NEW token; the page must pick it up unaided.
    mcp = new McpClient();
    await mcp.ready();

    await expect(page.locator('.app-status .agent-indicator')).toHaveCount(1, { timeout: 30_000 });

    const status = await mcp.call('get_status');
    expect(status.isError).toBe(false);
    // The document survived the outage untouched.
    expect((status.payload.counts as Record<string, number>).tasks).toBe(2);
  });

  test('the four settings that had no operation are now reachable (SRS-ABR-025)', async ({ page }) => {
    await connectBridge(page);

    const before = await mcp.call('get_view');
    expect(before.payload.gridDelimiter).toBe('week');
    expect(before.payload.weekendDisplay).toBe('shaded');

    // One batch, which is the point: before this they could only be reached by
    // replacing the whole document, so they could not travel with other ops.
    const applied = await mcp.call('apply_batch', {
      ops: [
        { op: 'set_grid_delimiter', delimiter: 'quarter' },
        { op: 'set_weekend_display', display: 'off' },
        { op: 'set_allow_overlap', allow: true },
        { op: 'set_style_preset', preset: 'mono' },
      ],
    });
    expect(applied.isError).toBe(false);
    expect(applied.payload.applied).toBe(4);

    const view = applied.payload.view as Record<string, unknown>;
    expect(view.gridDelimiter).toBe('quarter');
    expect(view.weekendDisplay).toBe('off');

    // It reached the chart, not just the model.
    expect(await page.locator('.grid-weekend').count()).toBe(0);

    const doc = (await mcp.call('get_document')).payload as {
      calendar: Record<string, unknown>;
      style: Record<string, unknown>;
    };
    expect(doc.calendar.gridDelimiter).toBe('quarter');
    expect(doc.style.allowOverlap).toBe(true);
    expect(doc.style.preset).toBe('mono');

    // And it is one frame, like any other batch (SRS-ABR-013).
    await mcp.call('undo');
    expect((await mcp.call('get_view')).payload.weekendDisplay).toBe('shaded');
  });

  test('a batch with one bad setting changes nothing (SRS-ABR-012)', async ({ page }) => {
    await connectBridge(page);
    const bad = await mcp.call('apply_batch', {
      ops: [
        { op: 'set_grid_delimiter', delimiter: 'quarter' },
        { op: 'set_weekend_display', display: 'hatched' },
      ],
    });
    expect(bad.isError).toBe(true);
    expect((await mcp.call('get_view')).payload.gridDelimiter).toBe('week');
  });

  test('the render comes back as a picture, not markup to read (SRS-ABR-034)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('apply_batch', { ops: plan(6, 3) });

    const render = await mcp.call('get_render');
    expect(render.isError).toBe(false);

    // An image block, first, so an agent can look at the chart.
    expect(render.blocks).toContain('image');
    expect(render.image?.mimeType).toBe('image/png');
    const png = render.image?.data ?? '';
    // A real raster of a real chart, not a 1x1 placeholder. PNG base64 always
    // begins iVBORw0KGgo.
    expect(png.startsWith('iVBORw0KGgo')).toBe(true);
    expect(png.length).toBeGreaterThan(5_000);
    expect(render.payload.pngError ?? null).toBeNull();

    // This payload is larger than one WebSocket frame, so it also proves the
    // reassembly: before the fix the call returned TIMEOUT because the first
    // fragment was parsed as a whole message and the continuations dropped.
    expect(render.payload.svgBytes as number).toBeGreaterThan(5_000);
    expect(render.payload.width as number).toBeGreaterThan(0);
  });

  test('the agent is told what is off screen, not just what exists (SRS-ABR-035)', async ({ page }) => {
    await connectBridge(page);
    // Far more rows than fit: the document is healthy and the view is not.
    await mcp.call('apply_batch', { ops: plan(30, 1) });

    const view = (await mcp.call('get_view')).payload as {
      hasLayout: boolean;
      onScreen: Record<string, { visible: number; clipped: number; below: number }>;
      warnings: string[];
      viewportPx: { height: number; contentBelow: number; contentAbove: number };
    };

    expect(view.hasLayout).toBe(true);
    expect(view.onScreen.task.visible).toBeGreaterThan(0);
    // The counts have to disagree with the document, or they are telling an
    // agent nothing it could not read from get_document.
    expect(view.onScreen.task.below).toBeGreaterThan(0);
    expect(view.warnings.join(' ')).toMatch(/off screen/);
    // Measured from what is drawn, not from the SVG node: the chart scrolls by
    // translating its contents inside a window-sized svg, so the node's height
    // is always the window's and would make this assertion unfalsifiable.
    expect(view.viewportPx.contentBelow).toBeGreaterThan(0);

    // Scrolling changes the answer, which is what makes it a view and not a
    // property of the document.
    const box = (await page.locator('.chart-area').boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    for (let i = 0; i < 8; i++) await page.mouse.wheel(0, 200);
    await page.waitForTimeout(200);

    const after = (await mcp.call('get_view')).payload as {
      onScreen: Record<string, { below: number; above: number }>;
    };
    expect(after.onScreen.task.above).toBeGreaterThan(0);
    expect(after.onScreen.task.below).toBeLessThan(view.onScreen.task.below);
  });

  test('an agent can act on its own off-screen report (SRS-ABR-026)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('apply_batch', { ops: plan(30, 1) });

    const before = (await mcp.call('get_view')).payload as {
      onScreen: Record<string, { visible: number; below: number }>;
      viewportPx: { scrollY: number; contentBelow: number };
    };
    expect(before.viewportPx.scrollY).toBe(0);
    expect(before.onScreen.row.below).toBeGreaterThan(0);

    // The last row the agent authored is off screen. Reveal is the remedy, and
    // the app does the arithmetic.
    const revealed = await mcp.call('reveal', { rowId: 'r29' });
    expect(revealed.isError).toBe(false);
    const after = revealed.payload.view as {
      onScreen: Record<string, { visible: number }>;
      viewportPx: { scrollY: number };
    };
    expect(after.viewportPx.scrollY).toBeGreaterThan(0);

    // The row is now actually drawn inside the chart area, measured in the page.
    const onScreen = await page.evaluate(() => {
      const el = document.querySelector('[data-pp-kind="row"][data-pp-id="r29"]');
      const area = document.querySelector('.chart-area');
      if (!el || !area) return false;
      const r = el.getBoundingClientRect();
      const a = area.getBoundingClientRect();
      return r.top < a.bottom && r.bottom > a.top;
    });
    expect(onScreen).toBe(true);

    // scrollY is readable and settable, and 0 puts it back at the top.
    const home = await mcp.call('set_viewport', { scrollY: 0 });
    expect((home.payload.view as { viewportPx: { scrollY: number } }).viewportPx.scrollY).toBe(0);
  });

  test('reveal refuses a row that does not exist', async ({ page }) => {
    await connectBridge(page);
    const { isError, payload } = await mcp.call('reveal', { rowId: 'no-such-row' });
    expect(isError).toBe(true);
    expect(payload.error).toBe('UNKNOWN_ID');
  });

  test('a new document releases the file the old one was bound to (SRS-ABR-028)', async ({ page }) => {
    await connectBridge(page);
    await mcp.call('apply_batch', { ops: plan(2, 1) });
    await page.locator('[data-pp-kind="task"]').first().click();
    await expect(page.locator('.app-status')).toContainText('1 selected');

    const { isError } = await mcp.call('new_document', { title: 'Fresh' });
    expect(isError).toBe(false);

    // The selection named ids that no longer exist; it must be gone.
    await expect(page.locator('.app-status')).not.toContainText('1 selected');
    // And the plan starts clean, not dirty and undoable: the title chip carries
    // the dirty mark, so a bullet here would mean the blank document arrived
    // already needing a save.
    await expect(page.locator('.app-header')).not.toContainText('•');

    const status = await mcp.call('get_status');
    expect(status.payload.title).toBe('Fresh');
    expect((status.payload.counts as Record<string, number>).tasks).toBe(0);
  });

  test('disconnecting from the UI ends the session immediately (SRS-ABR-061)', async ({ page }) => {
    await connectBridge(page);
    await page.getByRole('button', { name: /agent on/i }).click();
    await expect(page.locator('.app-status .agent-indicator')).toHaveCount(0);

    const { isError, payload } = await mcp.call('get_status');
    expect(isError).toBe(true);
    expect(payload.error).toBe('NO_CLIENT');
  });
});
