/**
 * Agent bridge client — the browser end of the MCP bridge.
 *
 * Connects OUT to the bridge server's loopback WebSocket, receives method
 * calls, applies them to the existing stores, and answers with results.
 *
 * Two invariants this module exists to protect:
 *
 *  - Every document mutation goes through `applyBatch`, so agent edits get the
 *    same validation, cascade, and single-history-frame semantics as any other
 *    edit (SRS-ABR-010/012/013). There is no second write path.
 *  - Nothing here is reachable unless the user turned the bridge on
 *    (SRS-ABR-001); the module is dynamically imported behind a DEV guard so it
 *    is absent from production output entirely (SRS-ABR-050).
 */

import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore, type Selectable } from '../stores/selectionStore';
import { validateDocument } from '../persistence/schema';
import { blankDocument } from '../embed/index';
import type { DocOp } from '../stores/docOps';
import { DocOpError } from '../stores/docOps';
import {
  BRIDGE_PROTOCOL_VERSION,
  DEFAULT_BRIDGE_PORT,
  type ApplyBatchParams,
  type BridgeErrorCode,
  type CallMessage,
  type ResultMessage,
} from './protocol';

/** Bounded replay cache for idempotent retries (SRS-ABR-023). */
const OP_CACHE_LIMIT = 128;

class BridgeError extends Error {
  constructor(
    readonly code: BridgeErrorCode,
    message: string,
  ) {
    super(message);
  }
}

export interface AgentBridgeOptions {
  port?: number;
  onStatusChange?: (connected: boolean) => void;
}

export class AgentBridge {
  private socket: WebSocket | null = null;
  private closedByUser = false;
  private reconnectDelay = 500;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly clientId = `client-${Math.random().toString(36).slice(2, 10)}`;
  private readonly applied = new Map<string, ResultMessage>();
  /** userRevision as of the caller's previous call, for `userEditedSince`. */
  private lastSeenUserRevision = useDocumentStore.getState().userRevision;

  constructor(private readonly options: AgentBridgeOptions = {}) {}

  async start(): Promise<void> {
    this.closedByUser = false;
    await this.connect();
  }

  stop(): void {
    // Withdrawal of consent takes effect immediately and locally (SRS-ABR-061).
    this.closedByUser = true;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
    if (this.socket && this.socket.readyState === WebSocket.OPEN) {
      this.send({ type: 'event', kind: 'bye' });
    }
    this.socket?.close();
    this.socket = null;
    useDocumentStore.getState().setAgentActive(false);
    this.options.onStatusChange?.(false);
  }

  private async connect(): Promise<void> {
    // The bridge server publishes BOTH the token and the port it actually bound
    // to, so an explicitly-configured port (tests, a second bridge) is picked up
    // without the page having to be told separately.
    const session = await fetchSession();
    const port = this.options.port ?? session.port ?? DEFAULT_BRIDGE_PORT;

    const socket = new WebSocket(`ws://127.0.0.1:${port}/agent`);
    this.socket = socket;

    socket.onopen = () => {
      this.reconnectDelay = 500;
      const state = useDocumentStore.getState();
      this.send({
        type: 'hello',
        protocolVersion: BRIDGE_PROTOCOL_VERSION,
        clientId: this.clientId,
        token: session.token,
        href: location.href,
        title: state.doc.title,
        revision: state.revision,
      });
      state.setAgentActive(true);
      this.options.onStatusChange?.(true);
    };

    socket.onmessage = (event) => {
      let call: CallMessage;
      try {
        call = JSON.parse(String(event.data)) as CallMessage;
      } catch {
        return;
      }
      if (call?.type !== 'call') return;
      this.send(this.handleCall(call));
    };

    socket.onclose = () => {
      useDocumentStore.getState().setAgentActive(false);
      this.options.onStatusChange?.(false);
      if (!this.closedByUser) this.scheduleReconnect();
    };

    socket.onerror = () => socket.close();
  }

  private scheduleReconnect(): void {
    // Restarting the agent host is routine and must not disturb the document
    // or require user action (SRS-ABR-003).
    this.reconnectTimer = setTimeout(() => {
      this.reconnectDelay = Math.min(this.reconnectDelay * 2, 8000);
      // connect() can reject before a socket exists — the token endpoint
      // returns 503 while the bridge server is down, which is the NORMAL state
      // during a restart. Without this catch that rejection is unhandled and
      // the retry loop stops silently. Found by the live gate; the e2e never
      // exercises a server that goes away mid-session.
      this.connect().catch(() => {
        if (!this.closedByUser) this.scheduleReconnect();
      });
    }, this.reconnectDelay);
  }

  private send(message: unknown): void {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(message));
    }
  }

  private handleCall(call: CallMessage): ResultMessage {
    // Replay rather than re-apply, so a retry after TIMEOUT is safe.
    if (call.opId) {
      const cached = this.applied.get(call.opId);
      if (cached) return { ...cached, id: call.id, replayed: true };
    }

    const before = useDocumentStore.getState();
    const userEditedSince = before.userRevision > this.lastSeenUserRevision;

    let result: ResultMessage;
    try {
      const data = this.dispatch(call);
      const after = useDocumentStore.getState();
      result = {
        type: 'result',
        id: call.id,
        ok: true,
        data,
        revision: after.revision,
        userEditedSince,
        replayed: false,
      };
    } catch (err) {
      const code: BridgeErrorCode =
        err instanceof BridgeError
          ? err.code
          : err instanceof DocOpError
            ? 'VALIDATION_FAILED'
            : 'VALIDATION_FAILED';
      result = {
        type: 'result',
        id: call.id,
        ok: false,
        error: { code, message: err instanceof Error ? err.message : String(err) },
        revision: useDocumentStore.getState().revision,
        userEditedSince,
        replayed: false,
      };
    }

    this.lastSeenUserRevision = useDocumentStore.getState().userRevision;

    if (call.opId && result.ok) {
      this.applied.set(call.opId, result);
      if (this.applied.size > OP_CACHE_LIMIT) {
        this.applied.delete(this.applied.keys().next().value as string);
      }
    }
    return result;
  }

  private dispatch(call: CallMessage): unknown {
    const params = (call.params ?? {}) as Record<string, unknown>;
    const store = useDocumentStore.getState();

    switch (call.method) {
      case 'get_status': {
        const s = useDocumentStore.getState();
        return {
          connected: true,
          clientId: this.clientId,
          title: s.doc.title,
          revision: s.revision,
          userRevision: s.userRevision,
          counts: {
            rows: s.doc.rows.length,
            tasks: s.doc.tasks.length,
            milestones: s.doc.milestones.length,
            dependencies: s.doc.dependencies.length,
            markers: s.doc.markers.length,
            brackets: s.doc.brackets.length,
          },
        };
      }

      case 'get_document':
        return store.doc;

      case 'get_view': {
        const vp = useViewportStore.getState();
        return {
          startDate: vp.startDate,
          pxPerDay: vp.pxPerDay,
          scale: store.doc.calendar.scale,
          selection: useSelectionStore.getState().items,
        };
      }

      case 'get_render':
        // Evidence to show the user; must not trigger a download (SRS-ABR-031).
        return { svg: serializeChartSvg() };

      case 'apply_batch': {
        const { ops, expectedRevision } = params as unknown as ApplyBatchParams;
        if (!Array.isArray(ops)) throw new BridgeError('VALIDATION_FAILED', 'params.ops must be an array');
        this.assertRevision(expectedRevision);
        store.applyBatch(ops as DocOp[], 'agent');
        return { applied: ops.length, revision: useDocumentStore.getState().revision };
      }

      case 'new_document': {
        this.assertRevision(params.expectedRevision as number | undefined);
        const doc = blankDocument(typeof params.title === 'string' ? params.title : undefined);
        // Same validation path as loading a file — an agent is untrusted input.
        store.replaceDocument(validateDocument(doc));
        return { title: doc.title };
      }

      case 'undo':
        store.undo('agent');
        return { revision: useDocumentStore.getState().revision };

      case 'redo':
        store.redo('agent');
        return { revision: useDocumentStore.getState().revision };

      case 'set_viewport': {
        const vp = useViewportStore.getState();
        const { startDate, pxPerDay } = params as Record<string, string | number | undefined>;
        if (typeof startDate === 'string') vp.setStart(startDate);
        if (typeof pxPerDay === 'number') vp.setPxPerDay(pxPerDay);
        const after = useViewportStore.getState();
        return { startDate: after.startDate, pxPerDay: after.pxPerDay };
      }

      case 'fit_to_tasks': {
        const doc = store.doc;
        const dates = [
          ...doc.tasks.flatMap((t) => [t.start, t.end]),
          ...doc.milestones.map((m) => m.date),
        ];
        if (dates.length === 0) throw new BridgeError('VALIDATION_FAILED', 'document has no dated entities');
        const min = dates.reduce((a, b) => (a < b ? a : b));
        const max = dates.reduce((a, b) => (a > b ? a : b));
        const width = document.querySelector('.chart-area')?.clientWidth ?? 800;
        useViewportStore.getState().fit(min, max, Math.max(100, width - 200));
        return { startDate: min, endDate: max };
      }

      case 'select': {
        const items = params.items;
        if (!Array.isArray(items)) throw new BridgeError('VALIDATION_FAILED', 'params.items must be an array');
        useSelectionStore.getState().setSelection(items as Selectable[]);
        return { selected: items.length };
      }

      default:
        throw new BridgeError('UNKNOWN_METHOD', `unknown method: ${call.method}`);
    }
  }

  /**
   * Opt-in optimistic concurrency (SRS-ABR-021). Absent means "do not check" —
   * mandatory whole-document checking would reject unrelated concurrent user
   * edits and make live sessions unusable (design review F1).
   */
  private assertRevision(expected: number | undefined): void {
    if (expected === undefined) return;
    const actual = useDocumentStore.getState().revision;
    if (expected !== actual) {
      throw new BridgeError(
        'REVISION_CONFLICT',
        `expected revision ${expected} but document is at ${actual}; the user's document stands`,
      );
    }
  }
}

/**
 * Serialize the live chart SVG to markup.
 *
 * `exportSvg()` in src/persistence/fileIo.ts triggers a download and cannot be
 * reused here (design review F7).
 */
function serializeChartSvg(): string {
  const svg = document.querySelector('.chart-area svg') ?? document.querySelector('svg');
  if (!svg) throw new BridgeError('VALIDATION_FAILED', 'no chart SVG present in the document');
  return new XMLSerializer().serializeToString(svg);
}

/**
 * Fetch the session token and bound port from the dev server, same-origin
 * (SRS-ABR-042). A foreign origin cannot read it, which is what makes it useful
 * behind the server's origin allowlist.
 */
async function fetchSession(): Promise<{ token: string; port?: number }> {
  const response = await fetch('/__agent-token');
  if (!response.ok) {
    const detail = await response.text().catch(() => '');
    throw new Error(`agent bridge unavailable (${response.status}) ${detail}`.trim());
  }
  const body = (await response.json()) as { token?: string; port?: number };
  if (!body.token) throw new Error('agent token missing from response');
  return { token: body.token, port: body.port };
}
