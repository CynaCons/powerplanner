import { describe, it, expect } from 'vitest';
// @ts-expect-error - plain ESM with no build step, imported for its behaviour
import { FrameReader, decodeFrame, encodeFrame, OPCODE } from '../../mcp/agent-bridge/frames.mjs';

/**
 * SRS-ABR-034 — a render large enough to fragment still arrives.
 *
 * Observed red on d101e0b: `get_render` over the live bridge returned
 * `{"error":"TIMEOUT","message":"No response within 5000ms"}` while the same
 * work took 32ms inside the page. The read loop did
 * `if (frame.opcode !== 0x1) continue;` and handed the first fragment straight
 * to `JSON.parse`, so a message Chrome split into fragments was lost: the first
 * fragment was unparseable JSON and the 0x0 continuations were skipped. Every
 * payload the bridge had ever carried was small enough to arrive in one frame,
 * which is why five iterations of bridge work never saw it. Adding a raster to
 * the render is simply the first payload big enough to cross the line.
 *
 * These cases build frames by hand, masked, the way a browser sends them.
 */

/** One masked frame, as a browser would put it on the wire. */
function clientFrame(text: string, { fin = true, opcode = OPCODE.text } = {}): Buffer {
  const payload = Buffer.from(text, 'utf8');
  const mask = Buffer.from([0x37, 0xfa, 0x21, 0x3d]);
  const masked = Buffer.from(payload);
  for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i % 4];

  const first = (fin ? 0x80 : 0x00) | opcode;
  let header: Buffer;
  if (payload.length < 126) {
    header = Buffer.from([first, 0x80 | payload.length]);
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4);
    header[0] = first;
    header[1] = 0x80 | 126;
    header.writeUInt16BE(payload.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = first;
    header[1] = 0x80 | 127;
    header.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  return Buffer.concat([header, mask, masked]);
}

const messages = (actions: Array<{ type: string; text?: string }>) =>
  actions.filter((a) => a.type === 'message').map((a) => a.text);

describe('the bridge reassembles a fragmented message (SRS-ABR-034)', () => {
  it('reads a whole message that arrived in one frame', () => {
    const reader = new FrameReader();
    expect(messages(reader.push(clientFrame('{"type":"result","id":1}')))).toEqual([
      '{"type":"result","id":1}',
    ]);
  });

  it('joins a message split across three fragments', () => {
    // This is the case that was silently dropped.
    const reader = new FrameReader();
    const head = clientFrame('{"type":"res', { fin: false, opcode: OPCODE.text });
    const mid = clientFrame('ult","id":', { fin: false, opcode: OPCODE.continuation });
    const tail = clientFrame('7}', { fin: true, opcode: OPCODE.continuation });

    expect(messages(reader.push(head))).toEqual([]);
    expect(messages(reader.push(mid))).toEqual([]);
    expect(messages(reader.push(tail))).toEqual(['{"type":"result","id":7}']);
  });

  it('carries a payload the size of a real render', () => {
    // ~116KB of base64 plus ~64KB of markup is what a render now weighs, and
    // both the 16-bit and 64-bit length forms have to work.
    const reader = new FrameReader();
    const png = 'A'.repeat(116_000);
    const svg = 'B'.repeat(64_000);
    const text = JSON.stringify({ type: 'result', id: 1, data: { png, svg } });
    const half = Math.floor(text.length / 2);
    reader.push(clientFrame(text.slice(0, half), { fin: false, opcode: OPCODE.text }));
    const out = messages(reader.push(clientFrame(text.slice(half), { fin: true, opcode: OPCODE.continuation })));
    expect(out).toHaveLength(1);
    expect(JSON.parse(out[0]!).data.png).toHaveLength(116_000);
  });

  it('survives a message arriving in arbitrary TCP chunks', () => {
    // A frame is not a packet: the reader must hold a partial header too.
    const reader = new FrameReader();
    const frame = clientFrame(JSON.stringify({ type: 'result', id: 2, data: 'x'.repeat(5_000) }));
    const out: Array<{ type: string; text?: string }> = [];
    for (let i = 0; i < frame.length; i += 97) out.push(...reader.push(frame.subarray(i, i + 97)));
    expect(messages(out)).toHaveLength(1);
  });

  it('answers a ping interleaved between two fragments without losing the message', () => {
    // A control frame may legally sit between fragments. Treating it as a
    // continuation would corrupt the payload; treating it as a new message
    // would discard the fragments already held.
    const reader = new FrameReader();
    reader.push(clientFrame('{"a":', { fin: false, opcode: OPCODE.text }));
    const mid = reader.push(clientFrame('hb', { fin: true, opcode: OPCODE.ping }));
    expect(mid.map((a: { type: string }) => a.type)).toEqual(['pong']);
    expect(messages(reader.push(clientFrame('1}', { fin: true, opcode: OPCODE.continuation })))).toEqual([
      '{"a":1}',
    ]);
  });

  it('reports a close and stops reading', () => {
    const reader = new FrameReader();
    const actions = reader.push(
      Buffer.concat([clientFrame('', { opcode: OPCODE.close }), clientFrame('{"ignored":1}')]),
    );
    expect(actions.map((a: { type: string }) => a.type)).toEqual(['close']);
  });

  it('drops a stray continuation instead of corrupting the next message', () => {
    const reader = new FrameReader();
    reader.push(clientFrame('junk', { fin: true, opcode: OPCODE.continuation }));
    expect(messages(reader.push(clientFrame('{"clean":true}')))).toEqual(['{"clean":true}']);
  });

  it('ignores a binary message rather than parsing it as text', () => {
    const reader = new FrameReader();
    expect(messages(reader.push(clientFrame('\u0000\u0001', { opcode: OPCODE.binary })))).toEqual([]);
  });

  it('round-trips both length forms, and sets FIN on what the server sends', () => {
    for (const size of [10, 200, 70_000]) {
      const frame = encodeFrame('z'.repeat(size));
      const decoded = decodeFrame(frame);
      expect(decoded.payload.toString('utf8')).toHaveLength(size);
      expect(decoded.fin).toBe(true);
      expect(decoded.opcode).toBe(OPCODE.text);
    }
  });

  it('can encode a pong, which the old encoder hardcoded to text', () => {
    expect(decodeFrame(encodeFrame('hb', OPCODE.pong)).opcode).toBe(OPCODE.pong);
  });
});
