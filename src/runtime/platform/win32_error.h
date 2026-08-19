#pragma once

#include <cstdint>

namespace leanclr
{
namespace platform
{
namespace win32_error
{

static constexpr int32_t kErrorSuccess = 0;
static constexpr int32_t kErrorInvalidHandle = 6;
static constexpr int32_t kErrorInvalidParameter = 87;
static constexpr int32_t kErrorCallNotImplemented = 120;
static constexpr int32_t kErrorInsufficientBuffer = 122;
static constexpr int32_t kErrorModNotFound = 126;
static constexpr int32_t kErrorProcNotFound = 127;
static constexpr int32_t kErrorEnvvarNotFound = 203;
static constexpr int32_t kErrorInvalidFlags = 1004;
static constexpr int32_t kErrorTimeout = 1460;

} // namespace win32_error
} // namespace platform
} // namespace leanclr
