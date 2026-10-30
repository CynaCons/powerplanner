import { test, expect } from '@playwright/test';
import { existsSync, mkdtempSync, readdirSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

/**
 * SRS-PERS-031/032/033/034 — the file you save is the file you reopen.
 *
 * The primary verb of a single-file app, and until now it had no gate at all:
 * `injectDocumentInto` was unit-tested against hand-written strings, so
 * nothing ever saved a real build and opened it. The real build is where it
 * failed. Every save spliced the plan into the middle of the inlined bundle,
 * because the first `</body>` in a built PowerRoadmap is inside this app's own
 * minified source, 137 KB before the real one. The reopened file looked almost
 * right — `outerHTML` had captured the rendered chart — but nothing in it ran.
 *
 * So this test saves through the app's own Ctrl+S, with the picker removed so
 * the bytes come out as a download, and opens the result as a local file the
 * way an owner does. The three things that were wrong are the three things it
 * asserts: the app runs, the plan is the saved one, and no script died.
 */

const TEMPLATE = resolve('dist-template/index.html');

/**
 * The built template is what the dev server's Save writes, so a stale one
 * means this gate reopens yesterday's app and says the save works. It bit
 * once already: a template built before the fix reopened cleanly, because the
 * defect only appears when the file has no data tag yet.
 */
function templateStaleness(): string | null {
  if (!existsSync(TEMPLATE)) {
    return 'dist-template/index.html is missing';
  }
  const built = statSync(TEMPLATE).mtimeMs;
  let newest = 0;
  let newestFile = '';
  const walk = (dir: string) => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) walk(path);
      else if (/\.(ts|tsx|css|html)$/.test(entry.name)) {
        const at = statSync(path).mtimeMs;
        if (at > newest) {
          newest = at;
          newestFile = path;
        }
      }
    }
  };
  walk(resolve('src'));
  const indexHtml = resolve('index.html');
  if (statSync(indexHtml).mtimeMs > newest) {
    newest = statSync(indexHtml).mtimeMs;
    newestFile = indexHtml;
  }
  return newest > built ? `dist-template/index.html is older than ${newestFile}` : null;
}

test.describe('a saved roadmap reopens', () => {
  test.skip(
    ({ browserName }) => browserName !== 'chromium',
    'saving in place is a Chromium capability; elsewhere the app downloads',
  );

  test('as a working app holding the plan that was saved', async ({ page, context }, testInfo) => {
    expect(
      templateStaleness(),
      'run `npm run build:template` — the dev server saves that built file, not this page',
    ).toBeNull();

    // No picker, so Save falls back to a download Playwright can catch. The
    // bytes are identical either way — `finishSave` builds them once. On the
    // context, not the page: the reopened file saves again below.
    await context.addInitScript(() => {
      delete (window as unknown as { showSaveFilePicker?: unknown }).showSaveFilePicker;
    });

    const errors: string[] = [];
    page.on('pageerror', (e) => errors.push(e.message));
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await expect(page.locator('.app-status')).toContainText('33 tasks');

    // An edit, so the reopened file can be told apart from the template it was
    // built from. Without one, a file holding the sample proves nothing.
    await page.locator('.cmdbar').getByTitle('Add task (T)').click();
    await expect(page.locator('.app-status')).toContainText('34 tasks');

    const [download] = await Promise.all([
      page.waitForEvent('download'),
      page.keyboard.press('Control+s'),
    ]);
    const saved = join(mkdtempSync(join(tmpdir(), 'pp-roundtrip-')), 'plan.html');
    await download.saveAs(saved);

    const reopened = await context.newPage();
    const reopenErrors: string[] = [];
    reopened.on('pageerror', (e) => reopenErrors.push(e.message));
    await reopened.goto(pathToFileURL(saved).href);

    // The app RUNS. A file whose bundle was cut in half still paints the
    // chart that `outerHTML` captured, so "a chart is visible" is not enough
    // on its own — but with no script errors and a live status bar it is.
    await reopened.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await expect(reopened.locator('.app-status')).toContainText('34 tasks');
    expect(reopenErrors, 'the reopened file threw while starting').toEqual([]);
    expect(errors, 'the app threw while saving').toEqual([]);

    // And it is editable, which a snapshot is not.
    await reopened.locator('.cmdbar').getByTitle('Add task (T)').click();
    await expect(reopened.locator('.app-status')).toContainText('35 tasks');

    // Save AGAIN, from the reopened file. A different branch, and the one
    // that ships: the dev server builds from the fetched template, a released
    // file builds from itself — and this time the data tag already exists, so
    // it is replaced rather than created. Appending instead would double the
    // document on every save.
    const [second] = await Promise.all([
      reopened.waitForEvent('download'),
      reopened.keyboard.press('Control+s'),
    ]);
    const savedTwice = join(mkdtempSync(join(tmpdir(), 'pp-roundtrip2-')), 'plan.html');
    await second.saveAs(savedTwice);

    const again = await context.newPage();
    const againErrors: string[] = [];
    again.on('pageerror', (e) => againErrors.push(e.message));
    await again.goto(pathToFileURL(savedTwice).href);
    await again.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await expect(again.locator('.app-status')).toContainText('35 tasks');
    expect(againErrors, 'the twice-saved file threw while starting').toEqual([]);

    const { readFileSync } = await import('node:fs');
    const twice = readFileSync(savedTwice, 'utf8');
    expect(
      (twice.match(/id="powerplanner-data"/g) ?? []).length,
      'the plan was appended rather than replaced',
    ).toBe(1);
    // It does not grow by a document per save.
    expect(twice.length).toBeLessThan(readFileSync(saved, 'utf8').length * 1.1);

    await testInfo.attach('saved-plan', { path: savedTwice, contentType: 'text/html' });
  });

  test('without carrying a copy of the rendered chart', async ({ page }) => {
    expect(templateStaleness(), 'run `npm run build:template`').toBeNull();
    await page.addInitScript(() => {
      delete (window as unknown as { showSaveFilePicker?: unknown }).showSaveFilePicker;
    });
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const [download] = await Promise.all([
      page.waitForEvent('download'),
      page.keyboard.press('Control+s'),
    ]);
    const saved = join(mkdtempSync(join(tmpdir(), 'pp-roundtrip-')), 'plan.html');
    await download.saveAs(saved);

    const { readFileSync } = await import('node:fs');
    const html = readFileSync(saved, 'utf8');
    // The chart root ships empty: a saved file carries data, not a picture.
    expect(html).toContain('<div id="root"></div>');
    // And it stays near the size of the build it came from. The snapshot used
    // to add ~100 KB of dead DOM on every save.
    const template = readFileSync(TEMPLATE, 'utf8');
    expect(html.length).toBeLessThan(template.length * 1.25);
  });
});
