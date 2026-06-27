// Precompiled header: ATL + Office/PowerPoint type libraries.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define _ATL_APARTMENT_THREADED
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define STRICT

#include <windows.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>

// Type libraries resolved by LIBID from the registry (Office is installed).
// raw_interfaces_only keeps the import lean and avoids name-collision wrappers.
// We intentionally OMIT named_guids: Office and PowerPoint both declare an
// `Adjustments` interface, so named_guids would emit IID_Adjustments twice in
// one object (LNK1179 duplicate COMDAT). Instead we use __uuidof on the
// imported interfaces (each carries a uuid attribute) and define LIBIDs by hand.

// Add-in Designer -> IDTExtensibility2 (namespace AddInDesignerObjects)
#import "libid:AC0714F2-3D04-11D1-AE7D-00A0C90F26F4" raw_interfaces_only

// Microsoft Office Object Library (MSO) -> IRibbonExtensibility / IRibbonControl / IRibbonUI
#import "libid:2DF8D04C-5BFA-101B-BDE5-00AA0044DE52" raw_interfaces_only \
    rename_namespace("Office") rename("RGB", "OfficeRGB") rename("DocumentProperties", "OfficeDocumentProperties")

// Microsoft PowerPoint Object Library
#import "libid:91493440-5A91-11CF-8700-00AA0060263B" raw_interfaces_only \
    rename_namespace("PowerPoint") rename("RGB", "PpRGB")
