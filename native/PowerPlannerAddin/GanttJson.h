// N3: canonical JSON <-> PpDocument. Used to embed the document on the slide
// (PP_DOC tag) and read it back (round-trip), and to load a document instead of
// the built-in sample. Validates against spec/schema/document.schema.json.
#pragma once

#include "GanttModel.h"
#include <string>

// Serialize a document to canonical JSON (schema-valid; calendar/style/markers
// are emitted with sensible defaults for fields the native model does not yet hold).
std::string DocumentToJson(const PpDocument& doc);

// Parse a document JSON string (the document object, not the fixture wrapper).
PpDocument DocumentFromJson(const std::string& json);

// SR-SMO-02 fast path: when `after` is a non-structural delta of `before`, patch
// `cachedJson` in place (parse → mutate → dump) instead of rebuilding JSON from
// C++ structs. Returns empty when a full DocumentToJson(after) is required.
std::string TryPatchDocJson(const PpDocument& before, const PpDocument& after,
	const std::string& cachedJson);

// Parsed PP_DOC cache for the scene-diff fast path — avoids re-parsing the tag
// JSON on every nudge/color op when CommitSceneCache has already parsed it once.
void GanttJson_InvalidateParsedCache();
void GanttJson_CommitParsedCache(const std::string& docJson);
std::string GanttJson_TryPatchFast(const PpDocument& before, const PpDocument& after);
