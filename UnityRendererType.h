/** @file
    @brief Header

    @date 2016

    @author
    Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2016 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_UnityRendererType_h_GUID_0140E981_E0C3_4590_6CCD_A74EACAF81D1
#define INCLUDED_UnityRendererType_h_GUID_0140E981_E0C3_4590_6CCD_A74EACAF81D1

// Internal Includes
#include "PluginConfig.h"

// Library/third-party includes
#include <boost/assert.hpp>

// Standard includes
// - none

/// Wrapper around UnityGfxRenderer that knows about our support capabilities.
class UnityRendererType {
  public:
    explicit operator bool() const { return supported_; }
    UnityGfxRenderer getDeviceTypeEnum() const {
        BOOST_ASSERT_MSG(supported_, "Cannot get an unsupported renderer!");
        return renderer_;
    }
    UnityGfxRenderer getDeviceTypeEnumUnconditionally() const {
        return renderer_;
    }
    UnityRendererType &operator=(UnityGfxRenderer gfxRenderer) {
        BOOST_ASSERT_MSG(renderer_ == kUnityGfxRendererNull,
                         "Expect to only set renderer when it's null!");
        switch (gfxRenderer) {
#if SUPPORT_OPENGL
        case kUnityGfxRendererOpenGL:
            renderer_ = gfxRenderer;
            supported_ = true;
            break;
#endif
#if SUPPORT_D3D11
        case kUnityGfxRendererD3D11:
            renderer_ = gfxRenderer;
            supported_ = true;
            break;
#endif
        case kUnityGfxRendererD3D9:
        case kUnityGfxRendererGCM:
        case kUnityGfxRendererNull:
        case kUnityGfxRendererXenon:
        case kUnityGfxRendererOpenGLES20:
        case kUnityGfxRendererOpenGLES30:
        case kUnityGfxRendererGXM:
        case kUnityGfxRendererPS4:
        case kUnityGfxRendererXboxOne:
        case kUnityGfxRendererMetal:
        case kUnityGfxRendererD3D12:
        default:
            renderer_ = kUnityGfxRendererNull;
            supported_ = false;
            break;
        }
        return *this;
    }

    void reset() {
        renderer_ = kUnityGfxRendererNull;
        supported_ = false;
    }

  private:
    UnityGfxRenderer renderer_ = kUnityGfxRendererNull;
    bool supported_ = false;
};

#endif // INCLUDED_UnityRendererType_h_GUID_0140E981_E0C3_4590_6CCD_A74EACAF81D1
