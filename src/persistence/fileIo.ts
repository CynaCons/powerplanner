import type { GanttDocument } from '../types/document';
import { validateDocument } from './schema';
import { toYaml, fromYaml } from './yaml';
import { buildEmbeddedHtml } from './embedded';

const FILE_PICKER_TYPES = [
  { description: 'PowerPlanner', accept: { 'text/html': ['.html'] as string[] } },
];

interface FilePickerOptions {
  suggestedName?: string;
  types?: Array<{ description: string; accept: Record<string, string[]> }>;
}

interface FileSystemWritableFileStreamLike {
  write: (data: BufferSource | string | Blob) => Promise<void>;
  close: () => Promise<void>;
}

interface FileSystemFileHandleLike {
  createWritable: () => Promise<FileSystemWritableFileStreamLike>;
  getFile: () => Promise<File>;
}

interface ShowSavePicker {
  (options: FilePickerOptions): Promise<FileSystemFileHandleLike>;
}

interface ShowOpenPicker {
  (options: { types?: Array<{ description: string; accept: Record<string, string[]> }>; multiple?: boolean }): Promise<FileSystemFileHandleLike[]>;
}

declare global {
  interface Window {
    showSaveFilePicker?: ShowSavePicker;
    showOpenFilePicker?: ShowOpenPicker;
  }
}

let currentHandle: FileSystemFileHandleLike | null = null;

export async function saveToFile(doc: GanttDocument, suggestedName = 'plan.html'): Promise<void> {
  const html = buildEmbeddedHtml(doc);
  const handle = currentHandle ?? (await pickSaveHandle(suggestedName));
  if (!handle) {
    downloadFile(suggestedName, html, 'text/html');
    return;
  }
  currentHandle = handle;
  const writable = await handle.createWritable();
  await writable.write(html);
  await writable.close();
}

export async function saveAs(doc: GanttDocument, suggestedName = 'plan.html'): Promise<void> {
  const html = buildEmbeddedHtml(doc);
  const handle = await pickSaveHandle(suggestedName);
  if (!handle) {
    downloadFile(suggestedName, html, 'text/html');
    return;
  }
  currentHandle = handle;
  const writable = await handle.createWritable();
  await writable.write(html);
  await writable.close();
}

async function pickSaveHandle(suggestedName: string): Promise<FileSystemFileHandleLike | null> {
  if (!window.showSaveFilePicker) return null;
  try {
    return await window.showSaveFilePicker({ suggestedName, types: FILE_PICKER_TYPES });
  } catch (e) {
    if ((e as { name?: string }).name === 'AbortError') return null;
    throw e;
  }
}

export function downloadFile(name: string, content: string | Blob, mime = 'text/plain'): void {
  const blob = typeof content === 'string' ? new Blob([content], { type: mime }) : content;
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

export async function openFile(): Promise<GanttDocument | null> {
  if (window.showOpenFilePicker) {
    try {
      const [handle] = await window.showOpenFilePicker({
        types: [
          { description: 'PowerPlanner', accept: { 'text/html': ['.html'], 'application/json': ['.json'], 'text/yaml': ['.yaml', '.yml'] } as Record<string, string[]> },
        ],
      });
      const file = await handle.getFile();
      currentHandle = handle;
      return parseFileContents(file.name, await file.text());
    } catch (e) {
      if ((e as { name?: string }).name === 'AbortError') return null;
      throw e;
    }
  }
  return new Promise<GanttDocument | null>((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.html,.json,.yaml,.yml';
    input.onchange = async () => {
      const file = input.files?.[0];
      if (!file) return resolve(null);
      try {
        resolve(parseFileContents(file.name, await file.text()));
      } catch (e) {
        alert(`Failed to open file: ${(e as Error).message}`);
        resolve(null);
      }
    };
    input.click();
  });
}

export function parseFileContents(name: string, text: string): GanttDocument {
  if (/\.html?$/i.test(name)) {
    const match = text.match(/<script[^>]*id=["']powerplanner-data["'][^>]*>([\s\S]*?)<\/script>/i);
    if (!match) throw new Error('No embedded PowerPlanner data found in this HTML file.');
    return validateDocument(JSON.parse(match[1]));
  }
  if (/\.json$/i.test(name)) {
    return validateDocument(JSON.parse(text));
  }
  if (/\.ya?ml$/i.test(name)) {
    return validateDocument(fromYaml(text));
  }
  // Try JSON first, then YAML
  try {
    return validateDocument(JSON.parse(text));
  } catch {
    return validateDocument(fromYaml(text));
  }
}

export function exportJson(doc: GanttDocument): void {
  downloadFile('plan.json', JSON.stringify(doc, null, 2), 'application/json');
}

export function exportYaml(doc: GanttDocument): void {
  downloadFile('plan.yaml', toYaml(doc), 'text/yaml');
}

export function exportSvg(): void {
  const svg = document.querySelector('.chart-area svg');
  if (!svg) return;
  const cloned = svg.cloneNode(true) as SVGSVGElement;
  inlineComputedStyles(svg as SVGSVGElement, cloned);
  const serialized = new XMLSerializer().serializeToString(cloned);
  const xml = `<?xml version="1.0" encoding="UTF-8"?>\n${serialized}`;
  downloadFile('plan.svg', xml, 'image/svg+xml');
}

export async function exportPng(scale = 2): Promise<void> {
  const svg = document.querySelector('.chart-area svg');
  if (!svg) return;
  const cloned = svg.cloneNode(true) as SVGSVGElement;
  inlineComputedStyles(svg as SVGSVGElement, cloned);
  const w = Number(cloned.getAttribute('width') ?? 800);
  const h = Number(cloned.getAttribute('height') ?? 600);
  const serialized = new XMLSerializer().serializeToString(cloned);
  const svgBlob = new Blob([serialized], { type: 'image/svg+xml;charset=utf-8' });
  const url = URL.createObjectURL(svgBlob);
  try {
    const img = new Image();
    await new Promise<void>((resolve, reject) => {
      img.onload = () => resolve();
      img.onerror = reject;
      img.src = url;
    });
    const canvas = document.createElement('canvas');
    canvas.width = w * scale;
    canvas.height = h * scale;
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new Error('Canvas 2D not supported');
    ctx.fillStyle = getComputedStyle(document.body).getPropertyValue('--color-bg') || '#fff';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.scale(scale, scale);
    ctx.drawImage(img, 0, 0);
    canvas.toBlob((blob) => {
      if (blob) downloadFile('plan.png', blob, 'image/png');
    }, 'image/png');
  } finally {
    URL.revokeObjectURL(url);
  }
}

// Inline computed styles so SVG renders correctly when extracted from the page
function inlineComputedStyles(src: SVGSVGElement, dst: SVGSVGElement): void {
  const srcEls = src.querySelectorAll('*');
  const dstEls = dst.querySelectorAll('*');
  for (let i = 0; i < srcEls.length; i++) {
    const s = srcEls[i];
    const d = dstEls[i] as SVGElement | HTMLElement;
    const cs = getComputedStyle(s);
    const props = ['fill', 'stroke', 'stroke-width', 'opacity', 'font-size', 'font-family', 'font-weight', 'text-anchor'];
    for (const p of props) {
      const v = cs.getPropertyValue(p);
      if (v && v !== 'normal' && v !== 'none' && v !== '') {
        d.style.setProperty(p, v);
      }
    }
  }
  // Apply background fill explicitly on first rect (already exists in our renderer)
  const rootBg = getComputedStyle(document.body).getPropertyValue('--color-bg') || '#fff';
  const bgRect = dst.querySelector('rect');
  if (bgRect && bgRect.getAttribute('fill') === 'var(--color-bg)') {
    bgRect.setAttribute('fill', rootBg);
  }
}
