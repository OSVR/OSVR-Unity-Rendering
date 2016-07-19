/** @file
@brief Header
@date 2015
@author
Sensics, Inc.
<http://sensics.com/osvr>
*/

// Copyright 2015 Sensics, Inc.
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
#pragma once

#include "PluginConfig.h"

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityInterface.h"
#include "osvr/RenderKit/RenderManagerC.h"
#include <osvr/RenderKit/RenderKitGraphicsTransforms.h>
#include <osvr/Util/ClientOpaqueTypesC.h>
#include <osvr/Util/ReturnCodesC.h>

typedef void(UNITY_INTERFACE_API *DebugFnPtr)(const char *);

extern "C" {

// No apparent UpdateDistortionMeshes symbol found?

/// @todo These are all the exported symbols, and they all are decorated to use
/// stdcall - yet somehow the managed code refers to some as cdecl. Either those
/// functions are never getting used, or something else is happening there.
UNITY_INTERFACE_EXPORT bool UNITY_INTERFACE_API 
IsDisplayOpened();

UNITY_INTERFACE_EXPORT bool UNITY_INTERFACE_API 
IsBufferConstructed();

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API 
SetClientContext(OSVR_ClientContext context);

UNITY_INTERFACE_EXPORT OSVR_ReturnCode UNITY_INTERFACE_API
ConstructRenderBuffers();

UNITY_INTERFACE_EXPORT OSVR_ReturnCode UNITY_INTERFACE_API
CreateRenderManagerFromUnity(OSVR_ClientContext context);

UNITY_INTERFACE_EXPORT OSVR_Pose3 UNITY_INTERFACE_API GetEyePose(int eye);

UNITY_INTERFACE_EXPORT OSVR_ProjectionMatrix
    UNITY_INTERFACE_API
    GetProjectionMatrix(int eye);

UNITY_INTERFACE_EXPORT UnityRenderingEvent UNITY_INTERFACE_API
GetRenderEventFunc();

UNITY_INTERFACE_EXPORT OSVR_ViewportDescription
    UNITY_INTERFACE_API
    GetViewport(int eye);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API LinkDebug(DebugFnPtr d);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API OnRenderEvent(int eventID);

/// @todo should return OSVR_ReturnCode
UNITY_INTERFACE_EXPORT int UNITY_INTERFACE_API
SetColorBufferFromUnity(void *texturePtr, int eye);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
SetFarClipDistance(double distance);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API SetIPD(double ipdMeters);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
SetNearClipDistance(double distance);
UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API ShutdownRenderManager();

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces *unityInterfaces);

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload();

// UpdateDistortionMesh no longer exported - buggy, not used.

} // extern "C"
