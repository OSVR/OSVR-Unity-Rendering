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

#ifndef INCLUDED_PluginConfig_h_GUID_BE647102_8843_4C9E_8180_2CA916069021
#define INCLUDED_PluginConfig_h_GUID_BE647102_8843_4C9E_8180_2CA916069021

// Which platform we are on?
#ifdef _WIN32
#define UNITY_WIN 1
#elif defined(__APPLE__)
#define UNITY_OSX 1
#elif defined(__linux__)
#define UNITY_LINUX 1
#else
#error "Unknown platform!"
#endif

// Which graphics device APIs we possibly support?
#if UNITY_WIN
#define SUPPORT_D3D11 1
#define SUPPORT_OPENGL 1
#elif UNITY_OSX || UNITY_LINUX
#define SUPPORT_OPENGL 1
#endif

#endif // INCLUDED_PluginConfig_h_GUID_BE647102_8843_4C9E_8180_2CA916069021
