// Force-included into FSR 2.2.1's runtime for the Switch 2 build (switch/CMakeLists.txt, pt_fsr2): the MSVC-only helpers its sources
// use, and volk, so the few Vulkan calls it makes directly (not through its device function table) go through volk's pointers.
#pragma once

#ifdef __cplusplus
#include <codecvt>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <locale>
#include <string>

#include <volk.h>

// newlib's libstdc++ has no std::wstring_convert; FSR 2 uses it only to widen its shaders' resource names, which are ASCII
namespace std {
template <typename Codecvt>
struct pt_ffx_wstring_convert {
    std::wstring from_bytes(const char* text) const {
        std::wstring wide;
        for (; text && *text; ++text) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*text)));
        return wide;
    }
};
}  // namespace std
#define wstring_convert pt_ffx_wstring_convert

#ifndef _countof
template <typename T, std::size_t N>
constexpr std::size_t pt_ffx_countof(T (&)[N]) {
    return N;
}
#define _countof(array) pt_ffx_countof(array)
#endif

// the bounded copy MSVC's wcscpy_s makes into a fixed array (FSR 2 copies resource names into wchar_t[64] fields)
template <std::size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < N; ++i) dst[i] = src[i];
    dst[i] = L'\0';
    return 0;
}
#endif
