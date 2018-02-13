/** @file
@brief Header
@date 2017
@author
Sensics, Inc.
<http://sensics.com/osvr>
*/

// Copyright 2017 Sensics, Inc.
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
// Internal includes
#include "OsvrRenderingPlugin.h"
#include "Unity/IUnityGraphics.h"
#include "UnityRendererType.h"

// Library includes
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include <osvr/Util/Finally.h>
#include <osvr/Util/MatrixConventionsC.h>

#include "PluginConfig.h"

#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityInterface.h"
#include <osvr/RenderKit/RenderKitGraphicsTransforms.h>
#include "osvr/RenderKit/RenderManagerC.h"
#include <osvr/Util/ClientOpaqueTypesC.h>
#include <osvr/Util/ReturnCodesC.h>
#include <cstdint>

//Abstract class for Unity Rendering plugin
//Constains virtual function declarations for render-type-specific sublclasses
class OsvrUnityRenderer {
public:
	virtual OSVR_ReturnCode CreateRenderBuffers(){ return OSVR_RETURN_FAILURE; }
	virtual OSVR_ReturnCode CreateRenderManager(OSVR_ClientContext context){ return OSVR_RETURN_FAILURE; }
	virtual OSVR_Pose3 GetEyePose(std::uint8_t eye)
	{
		OSVR_Pose3 pose;
		return pose;
	}
	virtual OSVR_ProjectionMatrix GetProjectionMatrix(std::uint8_t eye)
	{
		OSVR_ProjectionMatrix pm;
		return pm;
	}
	virtual OSVR_ViewportDescription GetViewport(std::uint8_t eye)
	{
		OSVR_ViewportDescription viewportDescription;
		return viewportDescription;
	}
	virtual void OnRenderEvent(){}
	virtual void OnInitializeGraphicsDeviceEvent(){}
	virtual void SetFarClipDistance(double distance){}
	virtual void SetIPD(double ipdMeters){}
	virtual void SetNearClipDistance(double distance){}
	virtual void ShutdownRenderManager(){}
	virtual void UpdateRenderInfo(){}
	virtual void SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer){}
	void SetDebugLog(DebugFnPtr d)
	{
		s_debugLog = d;

	}

protected:
	// logging
	DebugFnPtr s_debugLog = nullptr;
	OsvrUnityRenderer()
	{

	}

	~OsvrUnityRenderer()
	{

	}
	void DebugLog(const char *str)
	{
		if (s_debugLog != nullptr) {
			s_debugLog(str);
		}

	}
	IUnityInterfaces *s_UnityInterfaces = nullptr;
	IUnityGraphics *s_Graphics = nullptr;
	UnityRendererType s_deviceType = {};
	/// @todo is this redundant? (given renderParams)
	double s_nearClipDistance = 0.1;
	/// @todo is this redundant? (given renderParams)
	double s_farClipDistance = 1000.0;
	/// @todo is this redundant? (given renderParams)
	double s_ipd = 0.063;
	// cached viewport values
	std::uint32_t viewportWidth = 0;
	std::uint32_t viewportHeight = 0;

	//"double-buffered" swap chain
	//alternate buffers every frame
	int numBuffers = 2;
	int iterations = 0;

	// RenderEvents
	// Called from Unity with GL.IssuePluginEvent
	enum RenderEvents {
		kOsvrEventID_Render = 0,
		kOsvrEventID_Shutdown = 1,
		kOsvrEventID_Update = 2,
		kOsvrEventID_ConstructBuffers = 3,
		kOsvrEventID_ClearRoomToWorldTransform = 4
	};

};

