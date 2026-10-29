#!/usr/bin/env node
/**
 * PowerRoadmap Web Agent Bridge — MCP server.
 *
 * One process, two faces:
 *   - an MCP stdio server, spawned by the agent host via .mcp.json;
 *   - a WebSocket server bound to LOOPBACK ONLY, which the browser page
 *     connects out to.
 *
 * It is a relay with schemas. It deliberately holds NO document state: the
 * browser's Zustand store is the single source of truth, which is the property
 * that makes agent edits and user edits incapable of desyncing
 * (spec/srs-web/SRS-agent-bridge.md SRS-ABR-010).
 *
 * Scope: the web app only. The native add-in has its own agent surface.
 */

import { createServer } from 'node:http';
import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import { writeFileSync, unlinkSync, readFileSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

import { FrameReader, encodeFrame, OPCODE } from './frames.mjs';
const PROTOCOL_VERSION = 1;
const PORT = Number(process.env.POWERPLANNER_BRIDGE_PORT ?? 5199);
const CALL_TIMEOUT_MS = Number(process.env.POWERPLANNER_BRIDGE_TIMEOUT_MS ?? 5000);
const TOKEN_FILE = resolve(process.cwd(), '.powerplanner-bridge.json');

/**
 * Origins allowed to open a bridge socket.
 *
 * WebSocket handshakes are NOT subject to CORS, so without this any page the
 * user has open could connect and start editing their document
 * (SRS-ABR-040). Browsers set `Origin` themselves and pages cannot forge it —
 * which is exactly the attacker this defends against. It does not defend
 * against local code running as the user; that is out of scope by design.
 */
const ALLOWED_ORIGINS = new Set(
  (process.env.POWERPLANNER_BRIDGE_ORIGINS ?? 'http://localhost:5180,http://127.0.0.1:5180')
    .split(',')
    .map((o) => o.trim())
    .filter(Boolean),
);

const SESSION_TOKEN = randomBytes(32).toString('hex');

/* ------------------------------------------------------------------ */
/* Tool surface                                                        */
/* ------------------------------------------------------------------ */

const DOC_OP_SCHEMA = {
  type: 'object',
  description: 'A single document operation. See spec/srs-web/SRS-agent-bridge.md.',
  properties: { op: { type: 'string' } },
  required: ['op'],
  additionalProperties: true,
};

const TOOLS = [
  {
    name: 'get_status',
    description:
      'Bridge and document status: whether a browser client is attached, how many, the document title, current revision, and entity counts. Call this first — it distinguishes "nothing connected" from "connected to an empty document".',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'get_document',
    description:
      'The full GanttDocument as canonical JSON. Reason over this, not over screenshots.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'get_view',
    description:
      'What the person can actually SEE: the visible date window, px/day, time scale, the grid and weekend settings, the selection, ' +
      'the chart area in pixels, and a census of how many elements of each kind are on screen versus off it (above, below, before or after the view). ' +
      '`warnings` names anything a person would notice and you would not — every marker off screen, a plot taller than its window. ' +
      'The document saying a thing exists is not the same as the user being able to see it.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'get_render',
    description:
      'A PICTURE of the chart as it is on screen right now, returned as a PNG you can look at, plus the same view summary get_view gives. ' +
      'Call it after authoring to check your work the way the user will see it — colours, crowding, truncated names, things that do not fit. ' +
      'The raster carries the page background and the resolved theme colours, so it looks like the app and not like black text on nothing. ' +
      'Does not download a file. If rasterising is refused the reply says why in `pngError` instead of silently handing you markup.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'apply_batch',
    description:
      'Apply an ordered list of document operations as ONE undo frame. All-or-nothing: if any operation is invalid, nothing is applied. This is the primary way to author a chart — prefer one batch over many single calls, so the user can revert your whole turn with a single Ctrl+Z.\n\n' +
      'Operations: set_title, set_scale, set_fiscal_year_start, set_theme, ' +
      'set_grid_delimiter (none|day|week|month|quarter), set_weekend_display (off|shaded), ' +
      'set_allow_overlap, set_style_preset, ' +
      'add_row/update_row/delete_row, add_task/update_task/delete_task, ' +
      'add_milestone/update_milestone/delete_milestone, ' +
      'add_bracket/update_bracket/delete_bracket, ' +
      'add_dependency/update_dependency/delete_dependency, ' +
      'add_marker/update_marker/delete_marker, delete_items, capture_baseline, clear_baseline.\n\n' +
      'Use delete_items to clear a whole selection at once. A batch of single deletes CANNOT do it: ' +
      'delete_task already removes that task’s dependencies, so a dependency named later in the same ' +
      'batch aborts the whole batch and nothing is deleted.\n\n' +
      'Example: {"ops":[{"op":"add_row","row":{"id":"r1","label":"Design","groupId":null}},' +
      '{"op":"add_task","task":{"id":"t1","rowId":"r1","label":"Wireframes","start":"2026-03-02","end":"2026-03-16"}}]}\n\n' +
      'Ids must be supplied and unique. Dates are ISO (YYYY-MM-DD). Later ops may ' +
      'depend on entities created by earlier ops in the same batch.',
    inputSchema: {
      type: 'object',
      properties: {
        ops: { type: 'array', items: DOC_OP_SCHEMA, description: 'Ordered operations to apply.' },
        expectedRevision: {
          type: 'number',
          description:
            'Optional optimistic-concurrency check. Omit unless you need atomicity against concurrent user edits — supplying it makes the call fail if the user touched anything.',
        },
      },
      required: ['ops'],
      additionalProperties: false,
    },
  },
  {
    name: 'new_document',
    description: 'Replace the document with a blank plan. Use this before authoring from scratch — the app boots with sample data.',
    inputSchema: {
      type: 'object',
      properties: { title: { type: 'string' } },
      additionalProperties: false,
    },
  },
  {
    name: 'undo',
    description: 'Undo the last change.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'redo',
    description: 'Redo the last undone change.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'set_viewport',
    description:
      'Move the view: the left-hand date, the zoom in px/day, and how far down the plan is scrolled. ' +
      'scrollY is the one that answers the "N tasks are off screen (N below)" report from get_view — without it that report is a diagnosis with no remedy. ' +
      'Clamped to what is actually drawn. Changes no document state and makes no undo frame. Returns the view after the frame has painted.',
    inputSchema: {
      type: 'object',
      properties: {
        startDate: { type: 'string', description: 'ISO date for the left edge of the plot.' },
        pxPerDay: { type: 'number', description: 'Zoom. Clamped to the range the app allows.' },
        scrollY: { type: 'number', description: 'Pixels scrolled down the plan. 0 is the top.' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'reveal',
    description:
      'Scroll a row into view by id. Prefer this over set_viewport.scrollY after writing to a row: the app does the arithmetic, so it lands correctly whatever the height of the plan and of the window. Makes no undo frame.',
    inputSchema: {
      type: 'object',
      properties: { rowId: { type: 'string' } },
      required: ['rowId'],
      additionalProperties: false,
    },
  },
  {
    name: 'fit_to_tasks',
    description: 'Fit the viewport to the full date range of the document.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'select',
    description: 'Set the selection.',
    inputSchema: {
      type: 'object',
      properties: {
        items: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              kind: { type: 'string', enum: ['task', 'milestone', 'bracket', 'dependency', 'marker'] },
              id: { type: 'string' },
            },
            required: ['kind', 'id'],
          },
        },
      },
      required: ['items'],
      additionalProperties: false,
    },
  },
];

const MUTATING = new Set(['apply_batch', 'new_document', 'undo', 'redo']);

/* ------------------------------------------------------------------ */
/* WebSocket (RFC 6455, minimal server-side implementation)            */
/* ------------------------------------------------------------------ */

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

/** @type {Set<{socket: import('node:net').Socket, id: string, meta: object, lastSeen: number}>} */
const clients = new Set();
let nextCallId = 1;
/** @type {Map<number, {resolve: Function, timer: NodeJS.Timeout}>} */
const pending = new Map();

function tokenMatches(provided) {
  const a = createHash('sha256').update(String(provided ?? '')).digest();
  const b = createHash('sha256').update(SESSION_TOKEN).digest();
  return timingSafeEqual(a, b);
}

const httpServer = createServer((req, res) => {
  // The bridge speaks WebSocket only; no HTTP surface to probe.
  res.writeHead(404).end();
});

httpServer.on('upgrade', (req, socket) => {
  const origin = req.headers.origin;
  if (origin && !ALLOWED_ORIGINS.has(origin)) {
    socket.write('HTTP/1.1 403 Forbidden\r\n\r\n');
    socket.destroy();
    return;
  }

  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
    socket.destroy();
    return;
  }

  const accept = createHash('sha1').update(key + WS_GUID).digest('base64');
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
  );

  const client = { socket, id: null, meta: {}, lastSeen: Date.now() };

  // Fragmentation, masking and control frames are the codec's problem;
  // see mcp/agent-bridge/frames.mjs for why that is a module of its own.
  const reader = new FrameReader();

  socket.on('data', (chunk) => {
    for (const action of reader.push(chunk)) {
      if (action.type === 'close') {
        socket.end();
        return;
      }
      if (action.type === 'pong') {
        socket.write(encodeFrame(action.payload, OPCODE.pong));
        continue;
      }
      handleClientMessage(client, action.text, socket);
    }
  });

  const drop = () => {
    clients.delete(client);
  };
  socket.on('close', drop);
  socket.on('error', drop);
});

function handleClientMessage(client, raw, socket) {
  let message;
  try {
    message = JSON.parse(raw);
  } catch {
    return;
  }

  if (message.type === 'hello') {
    if (!tokenMatches(message.token)) {
      socket.write(encodeFrame(JSON.stringify({ type: 'event', kind: 'bye', reason: 'bad token' })));
      socket.end();
      return;
    }
    if (message.protocolVersion !== PROTOCOL_VERSION) {
      socket.write(
        encodeFrame(JSON.stringify({ type: 'event', kind: 'bye', reason: 'protocol version mismatch' })),
      );
      socket.end();
      return;
    }
    client.id = message.clientId;
    client.meta = { href: message.href, title: message.title, revision: message.revision };
    client.lastSeen = Date.now();
    clients.add(client);
    return;
  }

  if (message.type === 'result') {
    client.lastSeen = Date.now();
    const waiter = pending.get(message.id);
    if (!waiter) return;
    clearTimeout(waiter.timer);
    pending.delete(message.id);
    waiter.resolve(message);
    return;
  }

  if (message.type === 'event' && message.kind === 'bye') {
    clients.delete(client);
    socket.end();
  }
}

/** Resolve which client a call targets (SRS-ABR-004/005). */
function pickClient(requestedId) {
  const list = [...clients];
  if (list.length === 0) {
    throw bridgeFailure('NO_CLIENT', 'No PowerRoadmap page is connected. Open the app and switch the Agent toggle on.');
  }
  if (requestedId) {
    const found = list.find((c) => c.id === requestedId);
    if (!found) throw bridgeFailure('UNKNOWN_ID', `No connected client with id '${requestedId}'.`);
    return found;
  }
  if (list.length > 1) {
    // Silently editing the wrong open document is unrecoverable from the
    // user's point of view, so refuse to guess.
    throw bridgeFailure(
      'UNKNOWN_ID',
      `${list.length} clients are connected (${list.map((c) => c.id).join(', ')}); pass clientId to choose one.`,
    );
  }
  return list.sort((a, b) => b.lastSeen - a.lastSeen)[0];
}

function bridgeFailure(code, message) {
  const err = new Error(message);
  err.bridgeCode = code;
  return err;
}

function callClient(method, params, opId, clientId) {
  const client = pickClient(clientId);
  const id = nextCallId++;
  const payload = { type: 'call', id, method, params };
  if (opId) payload.opId = opId;

  return new Promise((resolvePromise, rejectPromise) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      // Retry is safe only with the same opId; the page replays rather than
      // re-applying (SRS-ABR-023/024).
      rejectPromise(
        bridgeFailure(
          'TIMEOUT',
          `No response within ${CALL_TIMEOUT_MS}ms. Safe to retry with the same opId ('${opId ?? 'none'}').`,
        ),
      );
    }, CALL_TIMEOUT_MS);

    pending.set(id, { resolve: resolvePromise, timer });
    try {
      client.socket.write(encodeFrame(JSON.stringify(payload)));
    } catch (err) {
      clearTimeout(timer);
      pending.delete(id);
      rejectPromise(err);
    }
  });
}

/* ------------------------------------------------------------------ */
/* MCP stdio                                                           */
/* ------------------------------------------------------------------ */

function writeMcp(message) {
  process.stdout.write(JSON.stringify(message) + '\n');
}

function mcpResult(id, result) {
  writeMcp({ jsonrpc: '2.0', id, result });
}

/**
 * Send a tool result.
 *
 * A `png` field is lifted out into an MCP image block rather than printed as
 * base64 (SRS-ABR-034). Everything used to go back as one text block, which
 * meant an agent asking to see the chart received SVG source to read — it could
 * reason about the markup but it could not look at the picture, and the two are
 * not the same thing when the question is "does this chart read well".
 *
 * The `svg` field is dropped from the text alongside it: it is tens of
 * kilobytes of markup that nothing downstream parses, and sending both the
 * image and its source doubles the cost of every render for no gain. It stays
 * available through the bridge for a caller that wants the markup itself.
 */
function mcpToolText(id, payload, isError = false) {
  const content = [];
  let rest = payload;

  if (payload && typeof payload === 'object' && typeof payload.png === 'string') {
    content.push({ type: 'image', data: payload.png, mimeType: 'image/png' });
    const { png, svg, ...others } = payload;
    rest = { ...others, svgBytes: typeof svg === 'string' ? svg.length : 0 };
  } else if (payload && typeof payload === 'object' && typeof payload.svg === 'string') {
    // Rasterising was refused; say so where the agent is looking, so a missing
    // picture cannot be mistaken for a chart that renders as nothing.
    const { svg, ...others } = payload;
    rest = { ...others, svgBytes: svg.length };
  }

  content.push({
    type: 'text',
    text: typeof rest === 'string' ? rest : JSON.stringify(rest, null, 2),
  });
  mcpResult(id, { content, isError });
}

/**
 * Re-assert ownership of the token file.
 *
 * Several bridges can exist over a machine's lifetime — an e2e run spawns its
 * own, and on Windows a killed process never runs its exit cleanup, so a dead
 * bridge's token can outlive it and shadow a live one. Rather than trying to
 * make cleanup reliable, the live server simply re-writes the file whenever it
 * is asked to do work. Self-healing beats coordinated shutdown.
 */
function ensureTokenFileOwned() {
  try {
    if (existsSync(TOKEN_FILE)) {
      const current = JSON.parse(readFileSync(TOKEN_FILE, 'utf8'));
      if (current.pid === process.pid && current.token === SESSION_TOKEN) return;
    }
    writeTokenFile();
  } catch {
    writeTokenFile();
  }
}

function writeTokenFile() {
  try {
    writeFileSync(TOKEN_FILE, JSON.stringify({ token: SESSION_TOKEN, port: PORT, pid: process.pid }, null, 2));
  } catch {
    /* non-fatal: the page will report the token endpoint failure */
  }
}

async function handleToolCall(id, name, args) {
  ensureTokenFileOwned();
  const known = TOOLS.some((t) => t.name === name);
  if (!known) {
    mcpToolText(id, `Unknown tool: ${name}`, true);
    return;
  }

  const { clientId, opId: callerOpId, ...params } = args ?? {};
  // Mutating calls always carry an idempotency key so a timeout is recoverable.
  const opId = MUTATING.has(name) ? (callerOpId ?? `op-${randomBytes(8).toString('hex')}`) : undefined;

  try {
    const response = await callClient(name, params, opId, clientId);
    if (!response.ok) {
      mcpToolText(
        id,
        {
          error: response.error?.code ?? 'VALIDATION_FAILED',
          message: response.error?.message ?? 'unknown error',
          revision: response.revision,
        },
        true,
      );
      return;
    }
    mcpToolText(id, {
      ...(typeof response.data === 'object' && response.data !== null ? response.data : { result: response.data }),
      revision: response.revision,
      userEditedSince: response.userEditedSince,
      ...(response.replayed ? { replayed: true } : {}),
      ...(opId ? { opId } : {}),
    });
  } catch (err) {
    mcpToolText(id, { error: err.bridgeCode ?? 'VALIDATION_FAILED', message: err.message }, true);
  }
}

let stdinBuffer = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => {
  stdinBuffer += chunk;
  let index;
  while ((index = stdinBuffer.indexOf('\n')) !== -1) {
    const line = stdinBuffer.slice(0, index).trim();
    stdinBuffer = stdinBuffer.slice(index + 1);
    if (!line) continue;

    let request;
    try {
      request = JSON.parse(line);
    } catch {
      continue;
    }

    switch (request.method) {
      case 'initialize':
        mcpResult(request.id, {
          protocolVersion: '2024-11-05',
          capabilities: { tools: {} },
          serverInfo: { name: 'powerplanner-web-bridge', version: '1.0.0' },
        });
        break;
      case 'tools/list':
        mcpResult(request.id, { tools: TOOLS });
        break;
      case 'tools/call':
        void handleToolCall(request.id, request.params?.name, request.params?.arguments);
        break;
      case 'notifications/initialized':
        break;
      default:
        if (request.id !== undefined) {
          writeMcp({
            jsonrpc: '2.0',
            id: request.id,
            error: { code: -32601, message: `Method not found: ${request.method}` },
          });
        }
    }
  }
});

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

function cleanup() {
  try {
    unlinkSync(TOKEN_FILE);
  } catch {
    /* already gone */
  }
}

/**
 * Say why, and stop.
 *
 * Without this an EADDRINUSE is an unhandled 'error' event: the process dies on
 * a stack trace, and anything waiting on `initialize` simply never hears back.
 * A caller cannot tell that from a bridge that is merely slow, which is how a
 * port collision between two test workers was read as a failure of the feature
 * the test happened to be asserting.
 */
httpServer.on('error', (err) => {
  const why =
    err.code === 'EADDRINUSE'
      ? `port ${PORT} is already in use — another bridge is running, or two of them started at once`
      : err.message;
  process.stderr.write(`[powerplanner-bridge] cannot listen: ${why}\n`);
  process.exit(1);
});

// Loopback only, never a wildcard address (SRS-ABR-002).
httpServer.listen(PORT, '127.0.0.1', () => {
  writeTokenFile();
  process.stderr.write(`[powerplanner-bridge] listening on 127.0.0.1:${PORT}\n`);
});

process.on('exit', cleanup);
process.on('SIGINT', () => {
  cleanup();
  process.exit(0);
});
process.on('SIGTERM', () => {
  cleanup();
  process.exit(0);
});
