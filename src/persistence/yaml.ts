// Minimal YAML serializer for GanttDocument-shaped data.
// Outputs human-readable, deterministic YAML. Not a general YAML library.

type Json = null | string | number | boolean | Json[] | { [key: string]: Json };

function indent(level: number): string {
  return '  '.repeat(level);
}

function isScalar(v: Json): v is string | number | boolean | null {
  return v === null || typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean';
}

function scalarToYaml(v: string | number | boolean | null): string {
  if (v === null) return 'null';
  if (typeof v === 'boolean') return v ? 'true' : 'false';
  if (typeof v === 'number') return String(v);
  // String: quote if needed (contains special chars, starts with whitespace/quote, or empty)
  if (v === '' || /[:\-?&*!|>'"%@`#\n\t]|^\s|\s$/.test(v) || /^(true|false|null|~|yes|no)$/i.test(v) || /^\d/.test(v)) {
    return JSON.stringify(v);
  }
  return v;
}

function emit(v: Json, level: number): string {
  if (isScalar(v)) return scalarToYaml(v);
  if (Array.isArray(v)) {
    if (v.length === 0) return '[]';
    return v
      .map((item) => {
        if (isScalar(item)) return `${indent(level)}- ${scalarToYaml(item)}`;
        if (Array.isArray(item)) return `${indent(level)}-\n${emit(item, level + 1)}`;
        // object
        const keys = Object.keys(item);
        if (keys.length === 0) return `${indent(level)}- {}`;
        const firstKey = keys[0];
        const firstVal = (item as { [k: string]: Json })[firstKey];
        const firstLine = `${indent(level)}- ${firstKey}: ${
          isScalar(firstVal) ? scalarToYaml(firstVal) : '\n' + emit(firstVal, level + 2)
        }`;
        const rest = keys
          .slice(1)
          .map((k) => `${indent(level + 1)}${k}: ${
            isScalar((item as { [k: string]: Json })[k])
              ? scalarToYaml((item as { [k: string]: Json })[k] as string | number | boolean | null)
              : '\n' + emit((item as { [k: string]: Json })[k], level + 2)
          }`);
        return [firstLine, ...rest].join('\n');
      })
      .join('\n');
  }
  // object
  const keys = Object.keys(v as { [k: string]: Json });
  if (keys.length === 0) return '{}';
  return keys
    .map((k) => {
      const val = (v as { [k: string]: Json })[k];
      if (isScalar(val)) return `${indent(level)}${k}: ${scalarToYaml(val)}`;
      if (Array.isArray(val) && val.length === 0) return `${indent(level)}${k}: []`;
      if (!Array.isArray(val) && Object.keys(val as { [k: string]: Json }).length === 0) return `${indent(level)}${k}: {}`;
      return `${indent(level)}${k}:\n${emit(val, level + 1)}`;
    })
    .join('\n');
}

export function toYaml(doc: unknown): string {
  return emit(doc as Json, 0) + '\n';
}

// Minimal YAML parser: supports the subset our serializer outputs.
// Designed for documents we wrote ourselves; not a general YAML loader.
export function fromYaml(yaml: string): unknown {
  const lines = yaml.split('\n').filter((l) => l.length > 0 && !/^\s*#/.test(l));
  let i = 0;

  function peekIndent(): number {
    if (i >= lines.length) return -1;
    const line = lines[i];
    return line.search(/\S/);
  }

  function parseScalar(s: string): Json {
    const t = s.trim();
    if (t === 'null' || t === '~') return null;
    if (t === 'true') return true;
    if (t === 'false') return false;
    if (t === '[]') return [];
    if (t === '{}') return {};
    if (/^-?\d+(\.\d+)?$/.test(t)) return Number(t);
    if ((t.startsWith('"') && t.endsWith('"')) || (t.startsWith("'") && t.endsWith("'"))) {
      try { return JSON.parse(t.replace(/^'/, '"').replace(/'$/, '"')); } catch { return t.slice(1, -1); }
    }
    return t;
  }

  function parseValue(myIndent: number): Json {
    if (i >= lines.length) return null;
    const lineIndent = peekIndent();
    if (lineIndent < myIndent) return null;
    const line = lines[i];
    const trimmed = line.trimStart();
    if (trimmed.startsWith('- ')) {
      return parseArray(lineIndent);
    }
    return parseObject(lineIndent);
  }

  function parseArray(myIndent: number): Json[] {
    const arr: Json[] = [];
    const innerIndent = myIndent + 2; // continuation keys of object items align under `- `
    while (i < lines.length) {
      const lineIndent = peekIndent();
      if (lineIndent < myIndent) break;
      const line = lines[i];
      if (lineIndent !== myIndent || !line.trimStart().startsWith('- ')) break;
      const content = line.trimStart().slice(2);
      i++;
      if (/^[\w-]+:\s*/.test(content)) {
        const colonIdx = content.indexOf(':');
        const k = content.slice(0, colonIdx).trim();
        const rawV = content.slice(colonIdx + 1).trim();
        const obj: { [k: string]: Json } = {};
        if (rawV === '') {
          obj[k] = parseValue(innerIndent + 2);
        } else {
          obj[k] = parseScalar(rawV);
        }
        while (i < lines.length) {
          const li = peekIndent();
          if (li !== innerIndent) break;
          const l = lines[i].trimStart();
          const ci = l.indexOf(':');
          if (ci < 0) break;
          const kk = l.slice(0, ci).trim();
          const vv = l.slice(ci + 1).trim();
          i++;
          if (vv === '') {
            obj[kk] = parseValue(innerIndent + 2);
          } else {
            obj[kk] = parseScalar(vv);
          }
        }
        arr.push(obj);
      } else {
        arr.push(parseScalar(content));
      }
    }
    return arr;
  }

  function parseObject(myIndent: number): { [k: string]: Json } {
    const obj: { [k: string]: Json } = {};
    while (i < lines.length) {
      const lineIndent = peekIndent();
      if (lineIndent < myIndent) break;
      if (lineIndent > myIndent) break;
      const line = lines[i].trimStart();
      if (line.startsWith('- ')) break;
      const colonIdx = line.indexOf(':');
      if (colonIdx < 0) break;
      const k = line.slice(0, colonIdx).trim();
      const rawV = line.slice(colonIdx + 1).trim();
      i++;
      if (rawV === '') {
        obj[k] = parseValue(myIndent + 1);
      } else {
        obj[k] = parseScalar(rawV);
      }
    }
    return obj;
  }

  return parseValue(0);
}
