// =============================================================================
// WestEngine - Platform (Win32)
// Isolated Windows.h inclusion point — prevents header pollution
// =============================================================================
#pragma once

// Minimize Windows headers
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <windows.h>
