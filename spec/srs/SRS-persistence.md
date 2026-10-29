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
| SRS-PERS-022 `[web]` | Replacing the document — a new roadmap, a template — shall release the file the app is bound to, so the next save asks where to write. | The binding is module state that outlives the document. Left in place, the next Ctrl+S wrote the new, empty roadmap over the file the previous one came from, with no picker, no prompt and no undo. | Test (`detachFile`) | fileIo `detachFile`; documentActions `startDocument` |
| SRS-PERS-023 `[web]` | A save shall report whether the bytes reached the file the user chose, and the document shall be marked saved only then. | A cancelled picker falls back to a download, which is not the file they asked for. Marking that clean silenced the guards on New and Open. | Test | fileIo `SaveResult`; documentActions `saveCurrent` |
| SRS-PERS-024 `[web]` | A document restored from the autosave snapshot shall be marked as having unsaved changes. | It is the one whole document that by definition is in no file; marking it clean made the guards stay silent over the very work they exist to protect. | Test | App `onRestore` → `replaceDocument` |
| SRS-PERS-025 `[web]` | Every file outcome shall be reported in one polite `role="status"` region naming what happened and to which file: a save that reached the chosen file, a save that fell back to a download because there is no picker or the picker was cancelled, a save that failed, an open rejected as not a roadmap, and each export. | Saved, saved-somewhere-else and failed were one silence. `saveToFile` reported honestly to a caller that used the answer only to gate `markSaved`; six throwing paths reached callers that neither awaited nor caught, so a failed save was an unhandled console event and nothing more; and the legacy open path raised an `alert` while the File System Access path rethrew, so Firefox told the user the file was not a roadmap and Chrome — the only browser where saving works at all — said nothing. | Test: `tests/e2e/announcements.spec.ts` | `stores/noticeStore`; `app/Notices`; `documentActions`; `fileIo` `SaveOutcome`/`OpenResult`/`ExportResult` |
| SRS-PERS-026 `[web]` | An update that attempts to rewrite the open file and fails shall say so, and shall not describe the file as untouched. | The failed write fell through to the download branch and returned the same value as "there was no file to write to", so the dialog printed "the one you have now is untouched" over a file it had just tried to overwrite — with a backup already in the downloads folder, leaving two files and one explanation. | Test: `tests/unit/updateChecker.test.ts` | `updateChecker` `PerformUpdateResult.inPlaceFailed`; `SettingsDialog` |
| SRS-PERS-027 `[web]` | A failure to write the autosave snapshot shall be reported, once however often it recurs. | The autosave path swallowed every write error while firing every 800ms, so a full local storage meant crash safety was gone with nothing on screen saying so — and an undeduplicated report would pin the region and bury every other message. | Test: `tests/unit/autosave.test.ts` | `autosave` `saveAutosave`; `noticeStore` collapse; `App` |
| SRS-PERS-028 `[web]` | Closing the window on a document that is in no file shall prompt before it is lost. | It was the one unrecoverable action left with nothing in front of it: the restore snapshot comes back on the next visit, but only until the next document replaces it. | Review (browser-native dialog; it cannot be asserted from a test) | `App` `beforeunload` |
| SRS-PERS-029 `[web]` | The status bar shall state which file the plan is bound to and whether it holds unsaved changes, and the window title shall carry the same two facts. | The binding is a module-level handle inside `fileIo` that nothing could read reactively, so the app never said whether the next Ctrl+S would write a file or produce a download, nor whether what was on screen had reached disk — the dirty flag existed and only the New and Open guards ever consulted it. Owner report 2026-10-29: “I am missing the information about which file I am connected to and whether I am connected to a file or not.” The window title carries it too, because a tab or a taskbar entry is where a filename is looked for, and PowerScroll — the product this one is modelled on — states it in both places. | Test: `tests/unit/fileBindingLabel.test.ts` | `app/StatusBar` `fileBindingLabel`; `persistence/fileIo` `getCurrentHandle`; `stores/documentStore` `useIsDirty`; SRS-PERS-021, SRS-PERS-023 |
| SRS-PERS-030 `[web]` | The autosave shall not overwrite a snapshot the restore banner is still offering, until there is newer work to keep. | SRS-PERS-020 offers the snapshot rather than applying it, and the debounced write ran unconditionally 800ms after every open — with whatever had loaded, normally the sample. So opening the app rewrote the backup with the sample before the planner had answered the banner. The offer survived the reload that made it, because it is already held in state; the second reload had nothing left to offer. A day's work could be lost by opening the app twice and not answering a banner. | Test: `tests/unit/autosaveOffer.test.ts`, `tests/e2e/axis-zoom.spec.ts` | `app/App` autosave effect; `persistence/autosave` |

## Open items

- HTML embed round-trip is partly Demo; promote to Test with a headless
  embed/parse fixture.
