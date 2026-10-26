import { describe, it, expect, vi } from 'vitest';
import { compareVersions, performUpdate } from '../../src/utils/updateChecker';
import type { FileSystemFileHandleLike } from '../../src/persistence/fileIo';
import { sampleDocument } from '../../src/utils/sample';

const doc = sampleDocument();
const TEMPLATE = '<html><body><div id="root"></div></body></html>';

describe('compareVersions', () => {
  it('orders dotted versions numerically, not lexically', () => {
    expect(compareVersions('2.16.0', '2.9.0')).toBeGreaterThan(0);
    expect(compareVersions('2.9.0', '2.16.0')).toBeLessThan(0);
    expect(compareVersions('2.16.0', '2.16.0')).toBe(0);
  });

  it('treats a missing segment as zero', () => {
    expect(compareVersions('2.16', '2.16.0')).toBe(0);
    expect(compareVersions('2.16.1', '2.16')).toBeGreaterThan(0);
  });
});

describe('performUpdate', () => {
  const base = () => ({
    fetchTemplate: vi.fn(async (_url?: string, _tag?: string) => TEMPLATE as string | null),
    writeHandle: vi.fn(async (_h: FileSystemFileHandleLike, _html: string) => true),
    verifyWrite: vi.fn(async (_h: FileSystemFileHandleLike) => true),
    reload: vi.fn(),
    download: vi.fn((_name: string, _content: string, _mime?: string) => {}),
    buildBackupHtml: () => '<html>backup</html>',
  });

  it('rewrites the open file and reloads when a writable handle exists', async () => {
    const deps = { ...base(), getHandle: () => ({}) as unknown as FileSystemFileHandleLike };
    const result = await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);

    expect(result).toEqual({ ok: true, mode: 'live-swap' });
    expect(deps.writeHandle).toHaveBeenCalledOnce();
    expect(deps.reload).toHaveBeenCalledOnce();
    // The plan must travel into the new build, or updating loses the document.
    expect(deps.writeHandle.mock.calls[0][1]).toContain('powerplanner-data');
    expect(deps.writeHandle.mock.calls[0][1]).toContain(doc.title);
  });

  it('settles write permission BEFORE fetching, while the click is still live', async () => {
    const order: string[] = [];
    const deps = {
      ...base(),
      getHandle: () => ({}) as unknown as FileSystemFileHandleLike,
      verifyWrite: vi.fn(async (_h: FileSystemFileHandleLike) => { order.push('verify'); return true; }),
      fetchTemplate: vi.fn(async (_url?: string, _tag?: string) => { order.push('fetch'); return TEMPLATE as string | null; }),
    };
    await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(order).toEqual(['verify', 'fetch']);
  });

  it('backs the file up before overwriting it', async () => {
    const deps = { ...base(), getHandle: () => ({}) as unknown as FileSystemFileHandleLike };
    await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(deps.download).toHaveBeenCalledOnce();
    expect(deps.download.mock.calls[0][0]).toContain('backup');
  });

  it('downloads instead when there is no handle, and does NOT also download a backup', async () => {
    const deps = { ...base(), getHandle: () => null };
    const result = await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);

    expect(result).toEqual({ ok: true, mode: 'download' });
    // Exactly one: browsers block a second programmatic download from one
    // gesture, and the file on disk is untouched, so it IS the backup.
    expect(deps.download).toHaveBeenCalledOnce();
    expect(deps.download.mock.calls[0][0]).toContain('v2.17.0');
    expect(deps.reload).not.toHaveBeenCalled();
  });

  it('downloads when the handle is there but permission is refused', async () => {
    const deps = { ...base(), getHandle: () => ({}) as unknown as FileSystemFileHandleLike, verifyWrite: vi.fn(async (_h: FileSystemFileHandleLike) => false) };
    const result = await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(result).toEqual({ ok: true, mode: 'download' });
    expect(deps.writeHandle).not.toHaveBeenCalled();
  });

  it('falls back to downloading when the in-place write fails', async () => {
    const deps = {
      ...base(),
      getHandle: () => ({}) as unknown as FileSystemFileHandleLike,
      writeHandle: vi.fn(async (_h: FileSystemFileHandleLike, _html: string) => false),
    };
    const result = await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(result).toEqual({ ok: true, mode: 'download' });
    expect(deps.reload).not.toHaveBeenCalled();
  });

  it('reports failure, and writes nothing, when the build cannot be fetched', async () => {
    const deps = { ...base(), getHandle: () => ({}) as unknown as FileSystemFileHandleLike, fetchTemplate: vi.fn(async (_url?: string, _tag?: string) => null as string | null) };
    const result = await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(result).toEqual({ ok: false });
    expect(deps.writeHandle).not.toHaveBeenCalled();
    expect(deps.download).not.toHaveBeenCalled();
  });

  it('pins the fetch to the release tag, never to main', async () => {
    const deps = { ...base(), getHandle: () => null };
    await performUpdate(doc, '2.16.0', '2.17.0', 'v2.17.0', undefined, deps);
    expect(deps.fetchTemplate).toHaveBeenCalledWith(undefined, 'v2.17.0');
  });
});
