import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { APP_VERSION } from '../../src/version';

// One version source, every derived copy checked (DECISIONS.md D5). A release
// that ships a template built from a mismatched version is unattributable.
//
// Resolve from the Vitest root rather than import.meta.url: under Vitest's
// transform import.meta.url is not a file: URL, so new URL(...) throws.
describe('version', () => {
  it('matches the version in package.json', () => {
    const pkg = JSON.parse(readFileSync(resolve(process.cwd(), 'package.json'), 'utf8'));
    expect(pkg.version).toBe(APP_VERSION);
  });

  it('is a plain semver triple', () => {
    expect(APP_VERSION).toMatch(/^\d+\.\d+\.\d+$/);
  });
});
