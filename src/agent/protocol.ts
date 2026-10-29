/**
 * Agent bridge wire protocol — browser side.
 *
 * The authoritative description lives in `docs/web-agent-bridge-plan.md` §3 and
 * the requirements in `spec/srs-web/SRS-agent-bridge.md`. The Node server keeps
 * its own copy of these shapes (it is plain JS with no build step); this file
 * and `mcp/agent-bridge/protocol.mjs` must be changed together.
 *
 * Deliberately small: the bridge is a relay, not a second document model.
 */

import type { DocOp } from '../stores/docOps';

export const BRIDGE_PROTOCOL_VERSION = 1;
export const DEFAULT_BRIDGE_PORT = 5199;

export type BridgeErrorCode =
  | 'NO_CLIENT'
  | 'REVISION_CONFLICT'
  | 'VALIDATION_FAILED'
  | 'UNKNOWN_ID'
  | 'TIMEOUT'
  | 'BRIDGE_DISABLED'
  | 'UNKNOWN_METHOD';

/** Methods that change the document. Each requires an `opId` (SRS-ABR-023). */
export const MUTATING_METHODS = [
  'apply_batch',
  'new_document',
  'undo',
  'redo',
] as const;

export type MutatingMethod = (typeof MUTATING_METHODS)[number];

export type BridgeMethod =
  | MutatingMethod
  | 'get_status'
  | 'get_document'
  | 'get_view'
  | 'get_render'
  | 'set_viewport'
  | 'select'
  | 'fit_to_tasks'
  /** Scroll a row into view (SRS-ABR-026). Not a document change. */
  | 'reveal';

export interface HelloMessage {
  type: 'hello';
  protocolVersion: number;
  clientId: string;
  token: string;
  href: string;
  title: string;
  revision: number;
}

export interface CallMessage {
  type: 'call';
  id: number;
  /** Idempotency key; present on mutating methods so retry cannot double-apply. */
  opId?: string;
  method: BridgeMethod;
  params?: Record<string, unknown>;
}

export interface ResultMessage {
  type: 'result';
  id: number;
  ok: boolean;
  data?: unknown;
  error?: { code: BridgeErrorCode; message: string };
  revision: number;
  /** True when the user has edited since the caller's previous call. */
  userEditedSince: boolean;
  /** True when this result was replayed from the opId cache rather than applied. */
  replayed: boolean;
}

export interface EventMessage {
  type: 'event';
  kind: 'doc-changed' | 'bye';
  revision?: number;
  source?: 'user' | 'agent';
}

export type ClientMessage = HelloMessage | ResultMessage | EventMessage;
export type ServerMessage = CallMessage;

/** Params for `apply_batch`, the workhorse method. */
export interface ApplyBatchParams {
  ops: DocOp[];
  /** Optional optimistic-concurrency check; absent means "do not check". */
  expectedRevision?: number;
}
