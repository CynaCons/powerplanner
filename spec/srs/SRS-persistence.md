# SRS — Persistence

Requirements for saving, loading, and round-tripping documents across formats.
Feature tag: `PERS`.

Traces up to: `../data-model.md`, `../schema/document.schema.json`.
Reference impl: `src/persistence/` (`schema.ts`, `yaml.ts`, `embedded.ts`,
`fileIo.ts`, `autosave.ts`).

## Validation

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PERS-001 | On load, the system shall validate the document against the canonical schema and reject it with a clear error when required structure is invalid. Unknown extra properties are tolerated (lenient forward-compat), not rejected. | Never operate on a structurally-malformed document, while staying forward-compatible with newer optional fields. | Test: `tests/unit/persistence`, `spec-conformance` | schema.ts; document.schema.json (`additionalProperties: true`) |
| SRS-PERS-002 | The document shall carry `schemaVersion` (currently 1); the system shall reject any version it does not support (no silent migration). | The version is explicit and gates loading; migration, if ever needed, is deliberate. | Test | schema.ts (SCHEMA_VERSION) |

## Formats & round-trip

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PERS-010 | The system shall serialize/deserialize the document to/from canonical JSON without loss. | JSON is the interchange format. | Test (JSON round-trip equals) | persistence; document.schema.json |
| SRS-PERS-011 | The system shall serialize/deserialize a human-readable YAML form, round-trip-stable with JSON. | Diff-friendly, hand-editable storage. | Test (YAML round-trip) | yaml.ts |
| SRS-PERS-012 | The system shall embed the document JSON in a portable single-file HTML and re-hydrate from it on open. | Email/archive/offline distribution. | Test/Demo | embedded.ts; build:template |
| SRS-PERS-013 | Round-trip through any supported format (JSON, YAML, HTML) shall preserve the document exactly (schema-equal). | Lossless persistence is the core promise. | Test | persistence round-trip tests |

## Autosave & file access

| ID | Requirement | Rationale | Verification | Trace |
|----|-------------|-----------|--------------|-------|
| SRS-PERS-020 | The system shall autosave a debounced snapshot to local storage while editing and offer to restore it on next launch. | Crash/refresh safety without explicit saves. | Demo | autosave.ts |
| SRS-PERS-021 `[web]` | The system shall save via the File System Access API where available and fall back to a download otherwise. | Native save on modern browsers; works everywhere. | Demo | fileIo.ts |

## Open items

- HTML embed round-trip is partly Demo; promote to Test with a headless
  embed/parse fixture.
