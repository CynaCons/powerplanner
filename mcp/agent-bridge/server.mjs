#!/usr/bin/env node
/**
 * PowerPlanner Web Agent Bridge — MCP server.
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
import { writeFileSync, unlinkSync } from 'node:fs';
import { resolve } from 'node:path';
import process from 'node:process';

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
    description: 'Current viewport (start date, px/day), time scale, and selection.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'get_render',
    description:
      'The rendered chart as SVG markup, for showing the user what was built. Does not download a file.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
  },
  {
    name: 'apply_batch',
    description:
      'Apply an ordered list of document operations as ONE undo frame. All-or-nothing: if any operation is invalid, nothing is applied. This is the primary way to author a chart — prefer one batch over many single calls, so the user can revert your whole turn with a single Ctrl+Z.\n\n' +
      'Operations: set_title, set_scale, set_fiscal_year_start, set_theme, ' +
      'add_row/update_row/delete_row, add_task/update_task/delete_task, ' +
      'add_milestone/update_milestone/delete_milestone, ' +
      'add_bracket/update_bracket/delete_bracket, ' +
      'add_dependency/update_dependency/delete_dependency, ' +
      'add_marker/update_marker/delete_marker, capture_baseline, clear_baseline.\n\n' +
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
      'Move the visible time window. Never called automatically by edits — only move the view when the user would want it moved.',
    inputSchema: {
      type: 'object',
      properties: { startDate: { type: 'string' }, pxPerDay: { type: 'number' } },
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
  let buffer = Buffer.alloc(0);

  socket.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    let frame;
    while ((frame = decodeFrame(buffer))) {
      buffer = buffer.subarray(frame.consumed);
      if (frame.opcode === 0x8) {
        socket.end();
        return;
      }
      if (frame.opcode !== 0x1) continue;
      handleClientMessage(client, frame.payload.toString('utf8'), socket);
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
    throw bridgeFailure('NO_CLIENT', 'No PowerPlanner page is connected. Open the app and switch the Agent toggle on.');
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

/* --- frame codecs (server sends unmasked, receives masked) --------- */

function decodeFrame(buf) {
  if (buf.length < 2) return null;
  const opcode = buf[0] & 0x0f;
  const masked = (buf[1] & 0x80) !== 0;
  let length = buf[1] & 0x7f;
  let offset = 2;

  if (length === 126) {
    if (buf.length < offset + 2) return null;
    length = buf.readUInt16BE(offset);
    offset += 2;
  } else if (length === 127) {
    if (buf.length < offset + 8) return null;
    length = Number(buf.readBigUInt64BE(offset));
    offset += 8;
  }

  let mask = null;
  if (masked) {
    if (buf.length < offset + 4) return null;
    mask = buf.subarray(offset, offset + 4);
    offset += 4;
  }

  if (buf.length < offset + length) return null;
  const payload = Buffer.from(buf.subarray(offset, offset + length));
  if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];

  return { opcode, payload, consumed: offset + length };
}

function encodeFrame(text) {
  const payload = Buffer.from(text, 'utf8');
  const length = payload.length;
  let header;

  if (length < 126) {
    header = Buffer.from([0x81, length]);
  } else if (length < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x81;
    header[1] = 126;
    header.writeUInt16BE(length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x81;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(length), 2);
  }
  return Buffer.concat([header, payload]);
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

function mcpToolText(id, payload, isError = false) {
  mcpResult(id, {
    content: [{ type: 'text', text: typeof payload === 'string' ? payload : JSON.stringify(payload, null, 2) }],
    isError,
  });
}

async function handleToolCall(id, name, args) {
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

// Loopback only, never a wildcard address (SRS-ABR-002).
httpServer.listen(PORT, '127.0.0.1', () => {
  writeFileSync(TOKEN_FILE, JSON.stringify({ token: SESSION_TOKEN, port: PORT, pid: process.pid }, null, 2));
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
