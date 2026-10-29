/**
 * WebSocket frame codec for the agent bridge (RFC 6455, the parts we use).
 *
 * Its own module so it can be tested without starting a server: the bridge
 * binds a socket and reads stdin at import time, so a test importing
 * `server.mjs` would start a second bridge and fight the live one for the token
 * file.
 *
 * It lives here because of a bug that hid for as long as every message was
 * small. The read loop used to drop anything that was not a lone text frame,
 * which is correct until a payload gets big enough for Chrome to fragment it —
 * and the first payload big enough was a render with a raster in it. The first
 * fragment arrived as opcode 0x1 with FIN clear and was parsed as a whole
 * message (it is not valid JSON, so it was dropped), and the 0x0 continuations
 * carrying the rest were skipped. The caller waited out its timeout and was
 * told TIMEOUT: a transport error reported for a protocol gap.
 */

/** Text frames the browser sends are masked; frames the server sends are not. */
export function decodeFrame(buf) {
  if (buf.length < 2) return null;
  const fin = (buf[0] & 0x80) !== 0;
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

  return { fin, opcode, payload, consumed: offset + length };
}

/** `opcode` defaults to text; pass 0xa to answer a ping. */
export function encodeFrame(text, opcode = 0x1) {
  const payload = Buffer.from(text, 'utf8');
  const length = payload.length;
  const first = 0x80 | opcode;
  let header;

  if (length < 126) {
    header = Buffer.from([first, length]);
  } else if (length < 65536) {
    header = Buffer.alloc(4);
    header[0] = first;
    header[1] = 126;
    header.writeUInt16BE(length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = first;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(length), 2);
  }
  return Buffer.concat([header, payload]);
}

export const OPCODE = { continuation: 0x0, text: 0x1, binary: 0x2, close: 0x8, ping: 0x9, pong: 0xa };

/**
 * Accumulates bytes off a socket and yields whole messages.
 *
 * `push` returns a list of actions for the caller to perform, rather than
 * taking callbacks, so the ordering of close/pong/message is testable: a
 * control frame may be interleaved between two fragments of one message and
 * must be answered without disturbing the reassembly in progress.
 */
export class FrameReader {
  constructor() {
    this.buffer = Buffer.alloc(0);
    this.pendingOpcode = null;
    this.pendingParts = [];
  }

  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    const actions = [];
    let frame;
    while ((frame = decodeFrame(this.buffer))) {
      this.buffer = this.buffer.subarray(frame.consumed);

      if (frame.opcode === OPCODE.close) {
        actions.push({ type: 'close' });
        return actions;
      }
      if (frame.opcode === OPCODE.ping) {
        actions.push({ type: 'pong', payload: frame.payload.toString('utf8') });
        continue;
      }
      if (frame.opcode === OPCODE.pong) continue;

      if (frame.opcode === OPCODE.continuation) {
        // A continuation with nothing to continue is a protocol error. Dropped
        // rather than concatenated, so it cannot corrupt the next message.
        if (this.pendingOpcode === null) continue;
        this.pendingParts.push(frame.payload);
      } else {
        this.pendingOpcode = frame.opcode;
        this.pendingParts = [frame.payload];
      }

      if (!frame.fin) continue;

      const opcode = this.pendingOpcode;
      const payload =
        this.pendingParts.length === 1 ? this.pendingParts[0] : Buffer.concat(this.pendingParts);
      this.pendingOpcode = null;
      this.pendingParts = [];
      if (opcode !== OPCODE.text) continue;
      actions.push({ type: 'message', text: payload.toString('utf8') });
    }
    return actions;
  }
}
