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
