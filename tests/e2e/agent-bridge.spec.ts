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

  request(method: string, params: unknown): Promise<{ result?: { content?: Array<{ text: string }>; isError?: boolean } }> {
    const id = this.nextId++;
    return new Promise((resolvePromise) => {
      this.waiters.set(id, resolvePromise as (value: unknown) => void);
      this.child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
    });
  }

  /** Call a tool and return its parsed payload plus whether it reported an error. */
  async call(name: string, args: Record<string, unknown> = {}) {
    const response = await this.request('tools/call', { name, arguments: args });
    const text = response.result?.content?.[0]?.text ?? '{}';
    return { isError: response.result?.isError === true, payload: JSON.parse(text) as Record<string, unknown> };
  }

  dispose(): void {
    this.child.kill();
  }
}

async function connectBridge(page: Page): Promise<void> {
  await page.goto('/');
  await page.getByRole('button', { name: /agent/i }).click();
  await expect(page.getByRole('status')).toContainText('agent connected', { timeout: 10_000 });
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

  test('disconnecting from the UI ends the session immediately (SRS-ABR-061)', async ({ page }) => {
    await connectBridge(page);
    await page.getByRole('button', { name: /agent on/i }).click();
    await expect(page.getByRole('status')).toHaveCount(0);

    const { isError, payload } = await mcp.call('get_status');
    expect(isError).toBe(true);
    expect(payload.error).toBe('NO_CLIENT');
  });
});
