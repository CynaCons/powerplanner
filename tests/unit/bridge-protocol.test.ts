import { describe, it, expect, afterEach } from 'vitest';
import { spawn, type ChildProcess } from 'node:child_process';
import { connect, type Socket } from 'node:net';
import { createHash, randomBytes } from 'node:crypto';
import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * Protocol and security tests for the bridge server, driven over a raw socket.
 *
 * These cover the properties that the browser-side e2e cannot: what happens to
 * a connection from the WRONG origin, or with the WRONG token. A real browser
 * will not send a forged Origin header, so a real browser cannot test the
 * defence that exists precisely to stop forged connections (SRS-ABR-040/041).
 */

const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const SERVER = resolve(REPO_ROOT, 'mcp/agent-bridge/server.mjs');
const TOKEN_FILE = resolve(REPO_ROOT, '.powerplanner-bridge.json');
const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const ALLOWED_ORIGIN = 'http://localhost:5180';

let server: ChildProcess | null = null;

/** Start a bridge server on its own port and wait until it is listening. */
async function startServer(port: number): Promise<string> {
  server = spawn('node', [SERVER], {
    cwd: REPO_ROOT,
    env: { ...process.env, POWERPLANNER_BRIDGE_PORT: String(port), POWERPLANNER_BRIDGE_ORIGINS: ALLOWED_ORIGIN },
    stdio: ['pipe', 'pipe', 'pipe'],
  });

  await new Promise<void>((resolvePromise, reject) => {
    const timer = setTimeout(() => reject(new Error('bridge server did not start')), 10_000);
    server!.stderr!.on('data', (chunk: Buffer) => {
      if (chunk.toString().includes('listening')) {
        clearTimeout(timer);
        resolvePromise();
      }
    });
  });

  return (JSON.parse(readFileSync(TOKEN_FILE, 'utf8')) as { token: string }).token;
}

/** Open a raw WebSocket handshake and return the server's HTTP response line. */
function handshake(port: number, origin: string | null): Promise<{ status: string; socket: Socket }> {
  return new Promise((resolvePromise, reject) => {
    const socket = connect(port, '127.0.0.1', () => {
      const key = randomBytes(16).toString('base64');
      const lines = [
        'GET /agent HTTP/1.1',
        `Host: 127.0.0.1:${port}`,
        'Upgrade: websocket',
        'Connection: Upgrade',
        `Sec-WebSocket-Key: ${key}`,
        'Sec-WebSocket-Version: 13',
      ];
      if (origin) lines.push(`Origin: ${origin}`);
      socket.write(lines.join('\r\n') + '\r\n\r\n');
    });

    socket.once('data', (chunk: Buffer) => {
      resolvePromise({ status: chunk.toString().split('\r\n')[0], socket });
    });
    socket.on('error', reject);
    setTimeout(() => reject(new Error('handshake timed out')), 5000);
  });
}

/** Mask and frame a text payload the way a browser client would. */
function clientFrame(text: string): Buffer {
  const payload = Buffer.from(text, 'utf8');
  const mask = randomBytes(4);
  const masked = Buffer.from(payload);
  for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i % 4];

  const header =
    payload.length < 126
      ? Buffer.from([0x81, 0x80 | payload.length])
      : Buffer.concat([Buffer.from([0x81, 0x80 | 126]), (() => {
          const b = Buffer.alloc(2);
          b.writeUInt16BE(payload.length);
          return b;
        })()]);

  return Buffer.concat([header, mask, masked]);
}

afterEach(() => {
  server?.kill();
  server = null;
});

describe('bridge server handshake security', () => {
  it('accepts an allowed origin (SRS-ABR-040)', async () => {
    const port = 5231;
    await startServer(port);
    const { status, socket } = await handshake(port, ALLOWED_ORIGIN);
    socket.destroy();
    expect(status).toContain('101');
  });

  it('rejects a foreign origin — WebSocket handshakes are not CORS-protected (SRS-ABR-040)', async () => {
    const port = 5232;
    await startServer(port);
    const { status, socket } = await handshake(port, 'https://evil.example.com');
    socket.destroy();
    expect(status).toContain('403');
  });

  it('closes a connection presenting a bad token (SRS-ABR-041)', async () => {
    const port = 5233;
    await startServer(port);
    const { socket } = await handshake(port, ALLOWED_ORIGIN);

    const closed = new Promise<string>((resolvePromise) => {
      let seen = '';
      socket.on('data', (chunk: Buffer) => {
        seen += chunk.toString('utf8');
      });
      socket.on('close', () => resolvePromise(seen));
    });

    socket.write(
      clientFrame(
        JSON.stringify({
          type: 'hello',
          protocolVersion: 1,
          clientId: 'impostor',
          token: 'not-the-token',
          href: ALLOWED_ORIGIN,
          title: 'x',
          revision: 0,
        }),
      ),
    );

    expect(await closed).toContain('bad token');
  });

  it('closes a connection with a mismatched protocol version', async () => {
    const port = 5234;
    const token = await startServer(port);
    const { socket } = await handshake(port, ALLOWED_ORIGIN);

    const closed = new Promise<string>((resolvePromise) => {
      let seen = '';
      socket.on('data', (chunk: Buffer) => {
        seen += chunk.toString('utf8');
      });
      socket.on('close', () => resolvePromise(seen));
    });

    socket.write(
      clientFrame(
        JSON.stringify({
          type: 'hello',
          protocolVersion: 99,
          clientId: 'from-the-future',
          token,
          href: ALLOWED_ORIGIN,
          title: 'x',
          revision: 0,
        }),
      ),
    );

    expect(await closed).toContain('protocol version mismatch');
  });

  it('binds loopback only, never a wildcard address (SRS-ABR-002)', () => {
    // Asserted against the source: a regression here would expose a
    // document-editing channel to the network, and is not observable from a
    // loopback-only test.
    const source = readFileSync(SERVER, 'utf8');
    expect(source).toContain("httpServer.listen(PORT, '127.0.0.1'");
    expect(source).not.toMatch(/listen\([^)]*'0\.0\.0\.0'/);
  });

  it('writes a session token file that is gitignored (SRS-ABR-042)', async () => {
    await startServer(5235);
    expect(existsSync(TOKEN_FILE)).toBe(true);
    const gitignore = readFileSync(resolve(REPO_ROOT, '.gitignore'), 'utf8');
    expect(gitignore).toContain('.powerplanner-bridge.json');
  });

  it('computes the RFC 6455 accept key correctly', async () => {
    const port = 5236;
    await startServer(port);
    const key = randomBytes(16).toString('base64');
    const expected = createHash('sha1').update(key + WS_GUID).digest('base64');

    const response = await new Promise<string>((resolvePromise, reject) => {
      const socket = connect(port, '127.0.0.1', () => {
        socket.write(
          [
            'GET /agent HTTP/1.1',
            `Host: 127.0.0.1:${port}`,
            'Upgrade: websocket',
            'Connection: Upgrade',
            `Sec-WebSocket-Key: ${key}`,
            'Sec-WebSocket-Version: 13',
            `Origin: ${ALLOWED_ORIGIN}`,
          ].join('\r\n') + '\r\n\r\n',
        );
      });
      socket.once('data', (chunk: Buffer) => {
        resolvePromise(chunk.toString());
        socket.destroy();
      });
      socket.on('error', reject);
    });

    expect(response).toContain(expected);
  });
});
