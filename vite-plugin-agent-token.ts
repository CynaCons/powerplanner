import type { Plugin } from 'vite';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/**
 * Serves the agent bridge session token to the app, same-origin, during dev.
 *
 * The bridge server writes `.powerplanner-bridge.json` (gitignored) at startup;
 * this middleware hands the token to the page so the user never copy-pastes it,
 * and a foreign origin cannot read it (SRS-ABR-041/042).
 *
 * Dev-only by construction: `apply: 'serve'` means it never participates in a
 * build, so no token path exists in shipped output.
 */
/** True when a process with this pid exists. Signal 0 checks without signalling. */
function isAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

export function agentTokenPlugin(): Plugin {
  const tokenFile = resolve(process.cwd(), '.powerplanner-bridge.json');

  return {
    name: 'powerplanner-agent-token',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use('/__agent-token', (_req, res) => {
        res.setHeader('Content-Type', 'application/json');
        // Never cached: the token rotates every time the bridge server starts.
        res.setHeader('Cache-Control', 'no-store');

        if (!existsSync(tokenFile)) {
          res.statusCode = 503;
          res.end(
            JSON.stringify({
              error: 'bridge server not running',
              hint: 'the agent bridge MCP server writes .powerplanner-bridge.json at startup',
            }),
          );
          return;
        }

        try {
          const { token, port, pid } = JSON.parse(readFileSync(tokenFile, 'utf8')) as {
            token?: string;
            port?: number;
            pid?: number;
          };
          if (!token) {
            res.statusCode = 500;
            res.end(JSON.stringify({ error: 'token missing from bridge file' }));
            return;
          }

          // The file can outlive its server: on Windows a killed process does
          // not run its SIGTERM handler, so the on-exit unlink never happens.
          // Without this check the page would be handed a token for a dead
          // server and retry a dead port indefinitely. Signal 0 tests liveness
          // without delivering anything.
          if (pid !== undefined && !isAlive(pid)) {
            res.statusCode = 503;
            res.end(
              JSON.stringify({
                error: 'bridge server not running',
                hint: `stale .powerplanner-bridge.json for pid ${pid}; it will be replaced when the bridge restarts`,
              }),
            );
            return;
          }

          res.end(JSON.stringify({ token, port }));
        } catch (err) {
          res.statusCode = 500;
          res.end(JSON.stringify({ error: `unreadable bridge file: ${String(err)}` }));
        }
      });
    },
  };
}
