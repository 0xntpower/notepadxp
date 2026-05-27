#pragma once

// WtlIncludes.hpp — Aggregated ATL/WTL includes.
//
// ATL/WTL headers are pulled in at warning level 3 (#pragma warning(push, 3)) so
// HRC's /W4 /WX applies cleanly to our own code without third-party header noise
// (HRC App. B documents the suppression). ATL (atlbase.h / atlwin.h) resolves
// from the MSVC environment (ATLMFC\include); WTL is vendored under
// extern/wtl/include. WTL injects its names into the global namespace
// automatically — an accepted, deliberate consequence.

#include "util/WinLean.hpp"

#pragma warning(push, 3)
#include <atlbase.h>
#include <atlapp.h>

#include <atlwin.h>
#include <atluser.h>
#include <atlgdi.h>
#include <atlmisc.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#pragma warning(pop)

// The single CAppModule instance, defined in Application.cpp. ATL/WTL module
// bookkeeping and the message-loop map live here.
extern CAppModule _Module;
