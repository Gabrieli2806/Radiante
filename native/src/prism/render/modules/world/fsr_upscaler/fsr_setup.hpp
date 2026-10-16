// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <volk.h>

#include <cstdint>

#ifdef PRISM_ENABLE_FFX_UPSCALER
#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_api_types.h>
#endif

// What the FidelityFX API needs from the renderer, shared by the FSR upscaler and FSR frame generation.
namespace prism::fsr {

#ifdef PRISM_ENABLE_FFX_UPSCALER
// The FidelityFX surface format with the texel layout of a Vulkan format (depth formats map to the matching color
// format), or FFX_API_SURFACE_FORMAT_UNKNOWN.
FfxApiSurfaceFormat vkToFfxFormat(VkFormat format);

// The function loader FidelityFX is created with. It asks for instance functions through it too, which
// vkGetDeviceProcAddr does not return, so those come from the instance volk loaded.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL customVkGetDeviceProcAddr(VkDevice device, const char *pName);

// FidelityFX's errors and warnings, into the error log.
void messageCallback(uint32_t type, const wchar_t *message);
#endif

} // namespace prism::fsr
