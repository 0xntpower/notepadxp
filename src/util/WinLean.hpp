#pragma once

// WinLean.hpp — Single, lean include of <Windows.h> with the HRC-mandated guards.
//
// Every project header that needs the Win32 API includes this instead of
// <Windows.h> directly, so the WIN32_LEAN_AND_MEAN / NOMINMAX / STRICT guards
// (HRC §10.1) are defined in exactly one place and headers stay self-contained.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef STRICT
#define STRICT
#endif

#include <Windows.h>
