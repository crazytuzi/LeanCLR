#pragma once

// Invariant (ordinal) collation primitives, shared by the two places that need
// them so the two cannot drift apart:
//
//   * icalls/system_globalization_compareinfo.cpp - the CompareInfo icalls used
//     by the Unity mscorlib product line.
//   * platform/rt_sys.cpp - the POSIX branch of the Win32 NLS surface
//     (FindNLSStringEx / FindStringOrdinal / CompareStringEx / LCMapStringEx)
//     that a Windows-flavour CoreCLR corlib P/Invokes into. leanclr's
//     architecture is "one Windows BCL + emulate Win32 on POSIX", and
//     GlobalizationNative_LoadICU() is deliberately 0, so corlib always picks
//     the NLS path - these entry points are load bearing on every platform.
//
// Precision: exact for ASCII, invariant-level beyond it. Case folding goes
// through towlower/towupper, which under bionic/musl in the "C" locale only
// fold ASCII; do not expect full Unicode simple case folding here. Linguistic
// options (IGNORESYMBOLS / IGNORENONSPACE / IGNOREKANATYPE / IGNOREWIDTH) are
// not modelled - callers get ordinal behaviour for those.
//
// Pure C++11, no exceptions, no ICU, no platform headers.

#include "core/rt_base.h"

#include <cstring>
#include <wctype.h>

namespace leanclr
{
namespace platform
{
namespace nls
{

/// Length of a null-terminated UTF-16 string, in code units.
inline int32_t utf16_length(const Utf16Char* str) noexcept
{
    if (str == nullptr)
    {
        return 0;
    }

    int32_t length = 0;
    while (str[length] != 0)
    {
        ++length;
    }
    return length;
}

inline Utf16Char to_lower(Utf16Char c) noexcept
{
    return static_cast<Utf16Char>(towlower(static_cast<wint_t>(c)));
}

inline Utf16Char to_upper(Utf16Char c) noexcept
{
    return static_cast<Utf16Char>(towupper(static_cast<wint_t>(c)));
}

/// -1 / 0 / 1, following the sign of an ordinal code-unit comparison.
inline int32_t compare_char(Utf16Char c1, Utf16Char c2, bool ignore_case) noexcept
{
    const int32_t result = ignore_case ? static_cast<int32_t>(to_lower(c1)) - static_cast<int32_t>(to_lower(c2))
                                       : static_cast<int32_t>(c1) - static_cast<int32_t>(c2);

    if (result < 0)
    {
        return -1;
    }
    if (result > 0)
    {
        return 1;
    }
    return 0;
}

/// -1 / 0 / 1. Shorter string sorts first when one is a prefix of the other.
inline int32_t compare_ordinal(const Utf16Char* str1, int32_t length1, const Utf16Char* str2, int32_t length2,
                               bool ignore_case) noexcept
{
    const int32_t min_length = length1 < length2 ? length1 : length2;
    for (int32_t i = 0; i < min_length; ++i)
    {
        const int32_t ord = compare_char(str1[i], str2[i], ignore_case);
        if (ord != 0)
        {
            return ord;
        }
    }

    if (length1 < length2)
    {
        return -1;
    }
    if (length1 > length2)
    {
        return 1;
    }
    return 0;
}

/// True when the `length` code units at `source` equal those at `value`.
/// Callers must have range-checked `source`; `length` of 0 always matches.
inline bool equals_at(const Utf16Char* source, const Utf16Char* value, int32_t length, bool ignore_case) noexcept
{
    if (length <= 0)
    {
        return true;
    }

    if (!ignore_case)
    {
        return std::memcmp(source, value, static_cast<size_t>(length) * sizeof(Utf16Char)) == 0;
    }

    for (int32_t i = 0; i < length; ++i)
    {
        if (to_lower(source[i]) != to_lower(value[i]))
        {
            return false;
        }
    }
    return true;
}

/// Index of the first (`from_start`) or last occurrence of `value` in `source`,
/// or -1 when absent. An empty `value` matches at index 0 / `source_length`,
/// mirroring where a zero-length needle sits when scanning from either end.
inline int32_t index_of(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length,
                        bool ignore_case, bool from_start) noexcept
{
    if (value_length <= 0)
    {
        return from_start ? 0 : source_length;
    }
    if (source_length < value_length)
    {
        return -1;
    }

    if (from_start)
    {
        for (int32_t i = 0; i <= source_length - value_length; ++i)
        {
            if (equals_at(source + i, value, value_length, ignore_case))
            {
                return i;
            }
        }
    }
    else
    {
        for (int32_t i = source_length - value_length; i >= 0; --i)
        {
            if (equals_at(source + i, value, value_length, ignore_case))
            {
                return i;
            }
        }
    }
    return -1;
}

/// True when `source` starts with `value`.
inline bool starts_with(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length,
                        bool ignore_case) noexcept
{
    if (value_length <= 0)
    {
        return true;
    }
    if (source_length < value_length)
    {
        return false;
    }
    return equals_at(source, value, value_length, ignore_case);
}

/// True when `source` ends with `value`.
inline bool ends_with(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length,
                      bool ignore_case) noexcept
{
    if (value_length <= 0)
    {
        return true;
    }
    if (source_length < value_length)
    {
        return false;
    }
    return equals_at(source + (source_length - value_length), value, value_length, ignore_case);
}

// ---------------------------------------------------------------------------
// Win32 NLS emulation.
//
// These mirror FindNLSStringEx / FindStringOrdinal / CompareStringEx /
// LCMapStringEx closely enough that a Windows-flavour corlib gets correct
// answers out of them. They live here rather than inline in rt_sys.cpp's POSIX
// branch so that a host build can differential-test them against the real
// Win32 functions - the failure mode of getting a flag or a sentinel wrong is a
// silent wrong value, which is exactly what a test needs to catch.
//
// `out_error` (optional) receives a Win32 error code on failure only. A "not
// found" answer is not a failure: -1 *is* the answer FindNLSStringEx gives, and
// the managed callers never consult GetLastError on that path.
// ---------------------------------------------------------------------------
namespace win32
{

constexpr uint32_t NORM_IGNORECASE_VALUE = 0x00000001;
constexpr uint32_t NORM_IGNORENONSPACE_VALUE = 0x00000002;
constexpr uint32_t NORM_IGNORESYMBOLS_VALUE = 0x00000004;
constexpr uint32_t LINGUISTIC_IGNORECASE_VALUE = 0x00000010;
constexpr uint32_t FIND_STARTSWITH_VALUE = 0x00100000;
constexpr uint32_t FIND_ENDSWITH_VALUE = 0x00200000;
constexpr uint32_t FIND_FROMSTART_VALUE = 0x00400000;
constexpr uint32_t FIND_FROMEND_VALUE = 0x00800000;

/// CompareStringEx result codes. 0 means failure, and the managed side computes
/// `result - 2`, so returning 0 from a successful compare silently means
/// "less than". Never return 0 unless the call really failed.
constexpr int32_t CSTR_LESS_THAN_VALUE = 1;
constexpr int32_t CSTR_EQUAL_VALUE = 2;
constexpr int32_t CSTR_GREATER_THAN_VALUE = 3;

constexpr uint32_t LCMAP_LOWERCASE_VALUE = 0x00000100;
constexpr uint32_t LCMAP_UPPERCASE_VALUE = 0x00000200;
constexpr uint32_t LCMAP_TITLECASE_VALUE = 0x00000300; // == LOWERCASE|UPPERCASE, test it first
constexpr uint32_t LCMAP_SORTKEY_VALUE = 0x00000400;

constexpr int32_t ERROR_INVALID_PARAMETER_VALUE = 87;
constexpr int32_t ERROR_INSUFFICIENT_BUFFER_VALUE = 122;

/// A negative NLS length means "null terminated"; resolve it to a real count.
inline int32_t resolve_length(const Utf16Char* str, int32_t length) noexcept
{
    return length >= 0 ? length : utf16_length(str);
}

inline void set_error(int32_t* out_error, int32_t error) noexcept
{
    if (out_error != nullptr)
    {
        *out_error = error;
    }
}

/// FindNLSStringEx: index of the match relative to `source`, or -1.
inline int32_t find_nls_string(uint32_t find_flags, const Utf16Char* source, int32_t source_length,
                               const Utf16Char* value, int32_t value_length, int32_t* found_length,
                               int32_t* out_error) noexcept
{
    if (source == nullptr || value == nullptr)
    {
        set_error(out_error, ERROR_INVALID_PARAMETER_VALUE);
        return -1;
    }

    const int32_t source_len = resolve_length(source, source_length);
    const int32_t value_len = resolve_length(value, value_length);
    const bool ignore_case = (find_flags & (LINGUISTIC_IGNORECASE_VALUE | NORM_IGNORECASE_VALUE)) != 0;

    int32_t hit;
    if ((find_flags & FIND_STARTSWITH_VALUE) != 0)
    {
        hit = starts_with(source, source_len, value, value_len, ignore_case) ? 0 : -1;
    }
    else if ((find_flags & FIND_ENDSWITH_VALUE) != 0)
    {
        hit = ends_with(source, source_len, value, value_len, ignore_case) ? source_len - value_len : -1;
    }
    else
    {
        // FIND_FROMSTART_VALUE is also the behaviour when no FIND_* flag is set.
        hit = index_of(source, source_len, value, value_len, ignore_case, (find_flags & FIND_FROMEND_VALUE) == 0);
    }

    if (hit >= 0 && found_length != nullptr)
    {
        *found_length = value_len;
    }
    return hit;
}

/// FindStringOrdinal: ordinal by definition, so this one is exact.
inline int32_t find_string_ordinal(uint32_t find_flags, const Utf16Char* source, int32_t source_length,
                                   const Utf16Char* value, int32_t value_length, bool ignore_case,
                                   int32_t* out_error) noexcept
{
    if (source == nullptr || value == nullptr)
    {
        set_error(out_error, ERROR_INVALID_PARAMETER_VALUE);
        return -1;
    }

    const int32_t source_len = resolve_length(source, source_length);
    const int32_t value_len = resolve_length(value, value_length);

    if ((find_flags & FIND_STARTSWITH_VALUE) != 0)
    {
        return starts_with(source, source_len, value, value_len, ignore_case) ? 0 : -1;
    }
    if ((find_flags & FIND_ENDSWITH_VALUE) != 0)
    {
        return ends_with(source, source_len, value, value_len, ignore_case) ? source_len - value_len : -1;
    }
    return index_of(source, source_len, value, value_len, ignore_case, (find_flags & FIND_FROMEND_VALUE) == 0);
}

/// CompareStringEx: CSTR_LESS_THAN_VALUE / CSTR_EQUAL_VALUE / CSTR_GREATER_THAN_VALUE, 0 on failure.
inline int32_t compare_string(uint32_t compare_flags, const Utf16Char* string1, int32_t string1_length,
                              const Utf16Char* string2, int32_t string2_length, int32_t* out_error) noexcept
{
    if (string1 == nullptr || string2 == nullptr)
    {
        set_error(out_error, ERROR_INVALID_PARAMETER_VALUE);
        return 0;
    }

    const int32_t length1 = resolve_length(string1, string1_length);
    const int32_t length2 = resolve_length(string2, string2_length);
    const bool ignore_case = (compare_flags & (LINGUISTIC_IGNORECASE_VALUE | NORM_IGNORECASE_VALUE)) != 0;

    const int32_t ord = compare_ordinal(string1, length1, string2, length2, ignore_case);
    if (ord < 0)
    {
        return CSTR_LESS_THAN_VALUE;
    }
    if (ord > 0)
    {
        return CSTR_GREATER_THAN_VALUE;
    }
    return CSTR_EQUAL_VALUE;
}

/// LCMapStringEx: chars written (bytes for LCMAP_SORTKEY_VALUE), the required size
/// when `destination_length` is 0, or 0 on failure.
///
/// Map flags we cannot model (BYTEREV, HASH, the IGNORE* normalisations)
/// degrade to a plain copy rather than to a failure, because 0 means "failed".
inline int32_t lc_map_string(uint32_t map_flags, const Utf16Char* source, int32_t source_length, void* destination,
                             int32_t destination_length, int32_t* out_error) noexcept
{
    if (source == nullptr)
    {
        set_error(out_error, ERROR_INVALID_PARAMETER_VALUE);
        return 0;
    }

    const int32_t source_len = resolve_length(source, source_length);

    if ((map_flags & LCMAP_SORTKEY_VALUE) != 0)
    {
        // Sort keys are opaque byte strings that the managed side compares with
        // memcmp and hashes. Big-endian code units make memcmp order match
        // compare_string's order, and folding when asked keeps
        // "case-insensitively equal => equal key" true.
        const int32_t required_bytes = source_len * static_cast<int32_t>(sizeof(Utf16Char));
        if (destination_length == 0)
        {
            return required_bytes;
        }
        if (destination == nullptr || destination_length < required_bytes)
        {
            set_error(out_error, ERROR_INSUFFICIENT_BUFFER_VALUE);
            return 0;
        }

        const bool ignore_case = (map_flags & NORM_IGNORECASE_VALUE) != 0;
        uint8_t* out_bytes = static_cast<uint8_t*>(destination);
        for (int32_t i = 0; i < source_len; ++i)
        {
            const Utf16Char c = ignore_case ? to_lower(source[i]) : source[i];
            out_bytes[i * 2] = static_cast<uint8_t>((c >> 8) & 0xFF);
            out_bytes[i * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        return required_bytes;
    }

    if (destination_length == 0)
    {
        return source_len;
    }
    if (destination == nullptr || destination_length < source_len)
    {
        set_error(out_error, ERROR_INSUFFICIENT_BUFFER_VALUE);
        return 0;
    }

    Utf16Char* out_chars = static_cast<Utf16Char*>(destination);
    const uint32_t case_flags = map_flags & LCMAP_TITLECASE_VALUE;
    if (case_flags == LCMAP_TITLECASE_VALUE)
    {
        bool at_word_start = true;
        for (int32_t i = 0; i < source_len; ++i)
        {
            const Utf16Char c = source[i];
            out_chars[i] = at_word_start ? to_upper(c) : to_lower(c);
            at_word_start = c == static_cast<Utf16Char>(' ') || c == static_cast<Utf16Char>('\t') ||
                            c == static_cast<Utf16Char>('\n') || c == static_cast<Utf16Char>('\r');
        }
    }
    else if (case_flags == LCMAP_UPPERCASE_VALUE)
    {
        for (int32_t i = 0; i < source_len; ++i)
        {
            out_chars[i] = to_upper(source[i]);
        }
    }
    else if (case_flags == LCMAP_LOWERCASE_VALUE)
    {
        for (int32_t i = 0; i < source_len; ++i)
        {
            out_chars[i] = to_lower(source[i]);
        }
    }
    else if (source_len > 0)
    {
        std::memcpy(out_chars, source, static_cast<size_t>(source_len) * sizeof(Utf16Char));
    }

    return source_len;
}

} // namespace win32

} // namespace nls
} // namespace platform
} // namespace leanclr
