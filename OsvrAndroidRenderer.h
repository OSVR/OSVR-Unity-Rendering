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

#include "OsvrUnityRenderer.h"

#include <osvr/ClientKit/ContextC.h>
#include <osvr/ClientKit/DisplayC.h>
#include <osvr/ClientKit/ImagingC.h>
#include <osvr/ClientKit/InterfaceC.h>
#include <osvr/ClientKit/InterfaceCallbackC.h>
#include <osvr/ClientKit/InterfaceStateC.h>
#include <osvr/ClientKit/ServerAutoStartC.h>
#include <chrono>

#include <iostream>
#include <osvr/RenderKit/RenderManagerOpenGLC.h>
#include <sstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <osvr/RenderKit/GraphicsLibraryOpenGL.h>
#include "OsvrRenderingPlugin.h"
class OsvrAndroidRenderer : public OsvrUnityRenderer{
public:
	OsvrAndroidRenderer();
	~OsvrAndroidRenderer();
	virtual OSVR_ReturnCode ConstructRenderBuffers();
	virtual OSVR_ReturnCode CreateRenderManager(OSVR_ClientContext context);
	virtual OSVR_Pose3 GetEyePose(std::uint8_t eye);
	virtual OSVR_ProjectionMatrix GetProjectionMatrix(std::uint8_t eye);
	virtual OSVR_ViewportDescription GetViewport(std::uint8_t eye);
	virtual void OnRenderEvent();
	virtual void OnInitializeGraphicsDeviceEvent();
	virtual void SetFarClipDistance(double distance);
	virtual void SetIPD(double ipdMeters);
	virtual void SetNearClipDistance(double distance);
	virtual void ShutdownRenderManager();
	virtual void UpdateRenderInfo();
	virtual void SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer);



	//@todo keep refactoring into generic base class
protected:
	bool setupOSVR();
	bool setupRenderManager();
	bool setupGraphics(int width, int height);
	bool setupRenderTextures(OSVR_RenderManager renderManager);
	OSVR_ClientContext gClientContext = NULL;
	OSVR_RenderManager gRenderManager = nullptr;
	OSVR_RenderManagerOpenGL gRenderManagerOGL = nullptr;

};