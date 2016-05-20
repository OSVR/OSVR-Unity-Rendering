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

#ifndef INCLUDED_OSVRDXGIUtils_h_GUID_B5C791C8_5F61_4A99_8B1E_6D9EFA9CE3D2
#define INCLUDED_OSVRDXGIUtils_h_GUID_B5C791C8_5F61_4A99_8B1E_6D9EFA9CE3D2

// Internal Includes
// - none

// Library/third-party includes
#include <d3d11.h>
#include <dxgi.h>

// Standard includes
#include <cstring>
#include <functional>
#include <sstream>
#include <utility>

/// Gets an IDXGIAdapter pointer from a D3D11Device - closer to the metal.
inline IDXGIAdapter *getAdapterFromD3DDevice(ID3D11Device *d3dDevice) {
    IDXGIDevice *dxgiDevice = nullptr;
    auto hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice),
                                        reinterpret_cast<void **>(&dxgiDevice));
    if (FAILED(hr)) {
        return nullptr;
    }
    IDXGIAdapter *dxgiAdapter = nullptr;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        return nullptr;
    }
    return dxgiAdapter;
}

/// Gets a description struct - something we can actually compare - for the
/// adapter underlying a given D3D11 Device.
inline DXGI_ADAPTER_DESC getAdapterDesc(ID3D11Device *d3dDevice) {
    DXGI_ADAPTER_DESC ret = {};
    if (!d3dDevice) {
        return ret;
    }
    auto dxgiAdapter = getAdapterFromD3DDevice(d3dDevice);
    if (!dxgiAdapter) {
        return ret;
    }
    auto hr = dxgiAdapter->GetDesc(&ret);
    if (FAILED(hr)) {
        return DXGI_ADAPTER_DESC{};
    }
    return ret;
}

/// Simple byte-wise comparison of the DXGI_ADAPTER_DESC struct for equality
inline bool adaptersEqual(DXGI_ADAPTER_DESC const &a,
                          DXGI_ADAPTER_DESC const &b) {
    return (0 == std::memcmp(&a, &b, sizeof(a)));
}

using ShareTextureReturnType = std::pair<ID3D11Texture2D *, const char *>;

inline ShareTextureReturnType shareTexture(ID3D11Texture2D &input,
                                           ID3D11Device &dev) {

    ShareTextureReturnType ret = {nullptr, nullptr};
    auto setError = [&](const char *errMsg) {
        ret.first = nullptr;
        ret.second = errMsg;
    };

    // Have to give the "good" adapter/device access to this texture.
    IDXGIResource *dxgiResource = nullptr;
    auto hr = input.QueryInterface(__uuidof(IDXGIResource),
                                   reinterpret_cast<void **>(&dxgiResource));
    if (FAILED(hr)) {
        setError("Could not get IDXGIResource interface from the "
                 "texture!");
        return ret;
    }

    HANDLE sharedHandle;
    hr = dxgiResource->GetSharedHandle(&sharedHandle);

    if (FAILED(hr)) {
        setError("Could not get shared handle from IDXGIResource "
                 "interface of the texture!");
        return ret;
    }

    hr = dev.OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void **>(&ret.first));
    if (FAILED(hr)) {
        setError("OpenSharedResource failed.");
        return ret;
    }
    if (ret.first == nullptr) {
        /// more of a warning - not sure if this happens or not...
        setError("Output was nullptr, reset to input pointer.");
        ret.first = &input;
    }
    return ret;
}

#endif // INCLUDED_OSVRDXGIUtils_h_GUID_B5C791C8_5F61_4A99_8B1E_6D9EFA9CE3D2
