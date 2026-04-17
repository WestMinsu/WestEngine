// =============================================================================
// WestEngine - Core
// Fundamental type aliases and utility macros
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace west
{

// ── Fixed-width type aliases ───────────────────────────────────────────────
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

using float32 = float;
using float64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;

} // namespace west

// ── Enum Flags Macro ───────────────────────────────────────────────────────
// Enables bitwise operators (|, &, ^, ~) for enum class types.
// Usage: WEST_ENUM_FLAGS(MyEnumClass);

#define WEST_ENUM_FLAGS(EnumType)                                                                                      \
    inline constexpr EnumType operator|(EnumType a, EnumType b)                                                        \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) |                                \
                                     static_cast<std::underlying_type_t<EnumType>>(b));                                \
    }                                                                                                                  \
    inline constexpr EnumType operator&(EnumType a, EnumType b)                                                        \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) &                                \
                                     static_cast<std::underlying_type_t<EnumType>>(b));                                \
    }                                                                                                                  \
    inline constexpr EnumType operator^(EnumType a, EnumType b)                                                        \
    {                                                                                                                  \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) ^                                \
                                     static_cast<std::underlying_type_t<EnumType>>(b));                                \
    }                                                                                                                  \
    inline constexpr EnumType operator~(EnumType a)                                                                    \
    {                                                                                                                  \
        return static_cast<EnumType>(~static_cast<std::underlying_type_t<EnumType>>(a));                               \
    }                                                                                                                  \
    inline constexpr EnumType& operator|=(EnumType& a, EnumType b)                                                     \
    {                                                                                                                  \
        return a = a | b;                                                                                              \
    }                                                                                                                  \
    inline constexpr EnumType& operator&=(EnumType& a, EnumType b)                                                     \
    {                                                                                                                  \
        return a = a & b;                                                                                              \
    }                                                                                                                  \
    inline constexpr EnumType& operator^=(EnumType& a, EnumType b)                                                     \
    {                                                                                                                  \
        return a = a ^ b;                                                                                              \
    }                                                                                                                  \
    inline constexpr bool HasFlag(EnumType value, EnumType flag)                                                       \
    {                                                                                                                  \
        return (value & flag) == flag;                                                                                 \
    }
