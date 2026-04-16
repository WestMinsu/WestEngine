// =============================================================================
// WestEngine - Core
// Memory alignment utilities
// =============================================================================
#pragma once

#include "core/Types.h"

namespace west
{

/// Aligns a value up to the next multiple of alignment.
/// @pre alignment must be a power of two.
inline constexpr usize AlignUp(usize value, usize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Aligns a value down to the previous multiple of alignment.
/// @pre alignment must be a power of two.
inline constexpr usize AlignDown(usize value, usize alignment)
{
    return value & ~(alignment - 1);
}

/// Checks if a value is aligned to the given alignment.
inline constexpr bool IsAligned(usize value, usize alignment)
{
    return (value & (alignment - 1)) == 0;
}

/// Default alignment for engine allocations (16 bytes — SSE/NEON friendly)
constexpr usize kDefaultAlignment = 16;

} // namespace west
