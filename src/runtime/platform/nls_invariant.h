#pragma once

#include "core/rt_base.h"
#include "platform/win32_error.h"
#include "utils/string_util.h"

#include <cstring>
#include <cwctype>

namespace leanclr
{
namespace platform
{
namespace nls
{

inline Utf16Char to_lower(Utf16Char c) noexcept
{
    return static_cast<Utf16Char>(std::towlower(static_cast<std::wint_t>(c)));
}

inline Utf16Char to_upper(Utf16Char c) noexcept
{
    return static_cast<Utf16Char>(std::towupper(static_cast<std::wint_t>(c)));
}

inline int32_t compare_char(Utf16Char c1, Utf16Char c2, bool ignore_case) noexcept
{
    const int32_t result =
        ignore_case ? static_cast<int32_t>(to_lower(c1)) - static_cast<int32_t>(to_lower(c2)) : static_cast<int32_t>(c1) - static_cast<int32_t>(c2);

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

inline int32_t compare_ordinal(const Utf16Char* str1, int32_t length1, const Utf16Char* str2, int32_t length2, bool ignore_case) noexcept
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

inline int32_t index_of(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length, bool ignore_case,
                        bool from_start) noexcept
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

inline bool starts_with(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length, bool ignore_case) noexcept
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

inline bool ends_with(const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length, bool ignore_case) noexcept
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

namespace win32
{

constexpr uint32_t kNormIgnoreCase = 0x00000001;
constexpr uint32_t kLinguisticIgnoreCase = 0x00000010;
constexpr uint32_t kIgnoreCaseMask = kNormIgnoreCase | kLinguisticIgnoreCase;
constexpr uint32_t kFindStartsWith = 0x00100000;
constexpr uint32_t kFindEndsWith = 0x00200000;
constexpr uint32_t kFindFromStart = 0x00400000;
constexpr uint32_t kFindFromEnd = 0x00800000;

constexpr int32_t kCstrLessThan = 1;
constexpr int32_t kCstrEqual = 2;
constexpr int32_t kCstrGreaterThan = 3;

constexpr uint32_t kLcMapLowerCase = 0x00000100;
constexpr uint32_t kLcMapUpperCase = 0x00000200;
constexpr uint32_t kLcMapTitleCase = 0x00000300;
constexpr uint32_t kLcMapSortKey = 0x00000400;

constexpr int32_t kErrorInvalidParameter = win32_error::kErrorInvalidParameter;
constexpr int32_t kErrorInsufficientBuffer = win32_error::kErrorInsufficientBuffer;
constexpr int32_t kErrorInvalidFlags = win32_error::kErrorInvalidFlags;

inline int32_t resolve_length(const Utf16Char* str, int32_t length) noexcept
{
    return length >= 0 ? length : (str != nullptr ? utils::StringUtil::get_utf16chars_length(str) : 0);
}

inline void set_error(int32_t* out_error, int32_t error) noexcept
{
    if (out_error != nullptr)
    {
        *out_error = error;
    }
}

inline int32_t find_impl(uint32_t find_flags, const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length, bool ignore_case,
                         int32_t* found_length, int32_t* out_error) noexcept
{
    if (source == nullptr || value == nullptr)
    {
        set_error(out_error, kErrorInvalidParameter);
        return -1;
    }
    if ((find_flags & kFindFromStart) != 0 && (find_flags & kFindFromEnd) != 0)
    {
        set_error(out_error, kErrorInvalidFlags);
        return -1;
    }

    const int32_t source_len = resolve_length(source, source_length);
    const int32_t value_len = resolve_length(value, value_length);

    int32_t hit;
    if ((find_flags & kFindStartsWith) != 0)
    {
        hit = starts_with(source, source_len, value, value_len, ignore_case) ? 0 : -1;
    }
    else if ((find_flags & kFindEndsWith) != 0)
    {
        hit = ends_with(source, source_len, value, value_len, ignore_case) ? source_len - value_len : -1;
    }
    else
    {
        hit = index_of(source, source_len, value, value_len, ignore_case, (find_flags & kFindFromEnd) == 0);
    }

    if (hit >= 0 && found_length != nullptr)
    {
        *found_length = value_len;
    }
    return hit;
}

inline int32_t find_nls_string(uint32_t find_flags, const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length,
                               int32_t* found_length, int32_t* out_error) noexcept
{
    const bool ignore_case = (find_flags & kIgnoreCaseMask) != 0;
    return find_impl(find_flags, source, source_length, value, value_length, ignore_case, found_length, out_error);
}

inline int32_t find_string_ordinal(uint32_t find_flags, const Utf16Char* source, int32_t source_length, const Utf16Char* value, int32_t value_length,
                                   bool ignore_case, int32_t* out_error) noexcept
{
    return find_impl(find_flags, source, source_length, value, value_length, ignore_case, nullptr, out_error);
}

inline int32_t compare_string(uint32_t compare_flags, const Utf16Char* string1, int32_t string1_length, const Utf16Char* string2, int32_t string2_length,
                              int32_t* out_error) noexcept
{
    if (string1 == nullptr || string2 == nullptr)
    {
        set_error(out_error, kErrorInvalidParameter);
        return 0;
    }

    const int32_t length1 = resolve_length(string1, string1_length);
    const int32_t length2 = resolve_length(string2, string2_length);
    const bool ignore_case = (compare_flags & kIgnoreCaseMask) != 0;

    const int32_t ord = compare_ordinal(string1, length1, string2, length2, ignore_case);
    if (ord < 0)
    {
        return kCstrLessThan;
    }
    if (ord > 0)
    {
        return kCstrGreaterThan;
    }
    return kCstrEqual;
}

inline int32_t lc_map_string(uint32_t map_flags, const Utf16Char* source, int32_t source_length, void* destination, int32_t destination_length,
                             int32_t* out_error) noexcept
{
    if (source == nullptr)
    {
        set_error(out_error, kErrorInvalidParameter);
        return 0;
    }

    const int32_t source_len = resolve_length(source, source_length);

    if ((map_flags & kLcMapSortKey) != 0)
    {
        const int32_t required_bytes = source_len * static_cast<int32_t>(sizeof(Utf16Char));
        if (destination_length == 0)
        {
            return required_bytes;
        }
        if (destination == nullptr || destination_length < required_bytes)
        {
            set_error(out_error, kErrorInsufficientBuffer);
            return 0;
        }

        const bool ignore_case = (map_flags & kIgnoreCaseMask) != 0;
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
        set_error(out_error, kErrorInsufficientBuffer);
        return 0;
    }

    Utf16Char* out_chars = static_cast<Utf16Char*>(destination);
    const uint32_t case_flags = map_flags & kLcMapTitleCase;
    if (case_flags == kLcMapTitleCase)
    {
        bool at_word_start = true;
        for (int32_t i = 0; i < source_len; ++i)
        {
            const Utf16Char c = source[i];
            out_chars[i] = at_word_start ? to_upper(c) : to_lower(c);
            at_word_start =
                c == static_cast<Utf16Char>(' ') || c == static_cast<Utf16Char>('\t') || c == static_cast<Utf16Char>('\n') || c == static_cast<Utf16Char>('\r');
        }
    }
    else if (case_flags == kLcMapUpperCase)
    {
        for (int32_t i = 0; i < source_len; ++i)
        {
            out_chars[i] = to_upper(source[i]);
        }
    }
    else if (case_flags == kLcMapLowerCase)
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
