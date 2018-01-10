/** @file
@brief Implementation
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

/// Both of these need to be enabled to force-enable logging to files.
#define ENABLE_LOGGING 0
#define ENABLE_LOGFILE 0

// Internal includes
#include "OsvrRenderingPlugin.h"
#include "Unity/IUnityGraphics.h"
#include "UnityRendererType.h"
#include "OsvrUnityRenderer.h"
#include "OsvrD3DRenderer.h"
#include "OsvrAndroidRenderer.h"

// Library includes
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include <osvr/Util/Finally.h>
#include <osvr/Util/MatrixConventionsC.h>


#if UNITY_WIN
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
// logging on windows
#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#include <fstream>
#include <iostream>
#endif // end define ENABLE_LOGGING
#include <memory>
#endif


// VARIABLES
static IUnityInterfaces *s_UnityInterfaces = nullptr;
static IUnityGraphics *s_Graphics = nullptr;
static UnityRendererType s_deviceType = {};

/// @todo is this redundant? (given renderParams)
static double s_nearClipDistance = 0.1;
/// @todo is this redundant? (given renderParams)
static double s_farClipDistance = 1000.0;
/// @todo is this redundant? (given renderParams)
static double s_ipd = 0.063;

// cached viewport values
static std::uint32_t viewportWidth = 0;
static std::uint32_t viewportHeight = 0;

static OsvrUnityRenderer* osvrUnityRenderer = nullptr;



// logging
#if UNITY_WIN
#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
static std::ofstream s_debugLogFile;
static std::streambuf *s_oldCout = nullptr;
static std::streambuf *s_oldCerr = nullptr;
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#endif

static int numBuffers = 2;
static int iterations = 0;

// OpenGL vars
#if SUPPORT_OPENGL
GLuint s_frameBuffer;
#endif // SUPPORT_OPENGL

// RenderEvents
// Called from Unity with GL.IssuePluginEvent
enum RenderEvents {
	kOsvrEventID_Render = 0,
	kOsvrEventID_Shutdown = 1,
	kOsvrEventID_Update = 2,
	kOsvrEventID_ConstructBuffers = 3,
	kOsvrEventID_ClearRoomToWorldTransform = 4
};

// --------------------------------------------------------------------------
// Helper utilities

// Allow writing to the Unity debug console from inside DLL land.
static DebugFnPtr s_debugLog = nullptr;
void UNITY_INTERFACE_API LinkDebug(DebugFnPtr d) {
	s_debugLog = d;
	osvrUnityRenderer->SetDebugLog(d);
}

// Only for debugging purposes, as this causes some errors at shutdown
inline void DebugLog(const char *str) {
#if UNITY_ANDROID
	return;/*
		   if (androidDebugLogMethodID != nullptr)
		   {
		   jstring jstr = jniEnvironment->NewStringUTF(str);
		   jniEnvironment->CallStaticVoidMethod(osvrJniWrapperClass,
		   androidDebugLogMethodID, jstr);
		   }
		   return;*/
#else // all platforms besides Android
#if !defined(NDEBUG) || defined(ENABLE_LOGGING)
	if (s_debugLog != nullptr) {
		s_debugLog(str);
	}
#endif // !defined(NDEBUG) || defined(ENABLE_LOGGING)

#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
	if (s_debugLogFile) {
		s_debugLogFile << str << std::endl;
	}
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#endif // all platforms besides Android
}

// --------------------------------------------------------------------------
// GraphicsDeviceEvents

#if SUPPORT_OPENGL
// -------------------------------------------------------------------
/// OpenGL setup/teardown code
/// @todo OpenGL path not implemented yet
inline void DoEventGraphicsDeviceOpenGL(UnityGfxDeviceEventType eventType) {
	BOOST_ASSERT_MSG(
		s_deviceType,
		"Should only be able to get in here with a valid device type.");
	BOOST_ASSERT_MSG(
		s_deviceType.getDeviceTypeEnum() == OSVRSupportedRenderers::OpenGL,
		"Should only be able to get in here if using OpenGL device type.");

	switch (eventType) {
	case kUnityGfxDeviceEventInitialize:
		DebugLog("OpenGL Initialize Event");
		break;
	case kUnityGfxDeviceEventShutdown:
		DebugLog("OpenGL Shutdown Event");
		break;
	default:
		break;
	}
}
#endif // SUPPORT_OPENGL


/// Needs the calling convention, even though it's static and not exported,
/// because it's registered as a callback on plugin load.
static void UNITY_INTERFACE_API
OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType) {
	switch (eventType) {
	case kUnityGfxDeviceEventInitialize: {
		if (osvrUnityRenderer == nullptr)
		{
#if SUPPORT_D3D11
			osvrUnityRenderer = new OsvrD3DRenderer();
			IUnityGraphicsD3D11 *d3d11 =
				s_UnityInterfaces->Get<IUnityGraphicsD3D11>();

			OsvrD3DRenderer* d3d = dynamic_cast<OsvrD3DRenderer*>(osvrUnityRenderer);
			// Put the device and context into a structure to let RenderManager
			// know to use this one rather than creating its own.
			d3d->s_libraryD3D.device = d3d11->GetDevice();
			ID3D11DeviceContext *ctx = nullptr;
			d3d->s_libraryD3D.device->GetImmediateContext(&ctx);
			d3d->s_libraryD3D.context = ctx;
#elif UNITY_ANDROID
			osvrUnityRenderer = new OsvrAndroidRenderer();

#endif
		}
		//osvrUnityRenderer->OnInitializeGraphicsDeviceEvent();
		break;
	}

	case kUnityGfxDeviceEventShutdown: {

		return;
	}

	case kUnityGfxDeviceEventBeforeReset: {
		DebugLog(
			"[OSVR Rendering Plugin] OnGraphicsDeviceEvent(BeforeReset).\n");
		break;
	}

	case kUnityGfxDeviceEventAfterReset: {
		DebugLog(
			"[OSVR Rendering Plugin] OnGraphicsDeviceEvent(AfterReset).\n");
		break;
	}
	}
}

// --------------------------------------------------------------------------
// UnitySetInterfaces
void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces *unityInterfaces) {
#if UNITY_WIN
#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
	s_debugLogFile.open("RenderPluginLog.txt");

	// Capture std::cout and std::cerr from RenderManager.
	if (s_debugLogFile) {
		s_oldCout = std::cout.rdbuf();
		std::cout.rdbuf(s_debugLogFile.rdbuf());

		s_oldCerr = std::cerr.rdbuf();
		std::cerr.rdbuf(s_debugLogFile.rdbuf());
	}
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#endif // UNITY_WIN
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

void UNITY_INTERFACE_API UnityPluginUnload() {
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventShutdown);
#if UNITY_WIN
#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
	if (s_debugLogFile) {
		// Restore the buffers
		std::cout.rdbuf(s_oldCout);
		std::cerr.rdbuf(s_oldCerr);
		s_debugLogFile.close();
	}
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#endif // UNITY_WIN
}


// Updates the internal "room to world" transformation (applied to all
// tracker data for this client context instance) based on the user's head
// orientation, so that the direction the user is facing becomes -Z to your
// application. Only rotates about the Y axis (yaw).
//
// Note that this method internally calls osvrClientUpdate() to get a head pose
// so your callbacks may be called during its execution!
/// @todo does this actually get called from anywhere or is it dead code?
void SetRoomRotationUsingHead() { /* s_renderD3D-> SetRoomRotationUsingHead();
								  */
}

// Clears/resets the internal "room to world" transformation back to an
// identity transformation - that is, clears the effect of any other
// manipulation of the room to world transform.
/// @todo does this actually get called from anywhere or is it dead code?
void ClearRoomToWorldTransform() { /*s_render->ClearRoomToWorldTransform(); */
}

void UNITY_INTERFACE_API ShutdownRenderManager() {
	if (osvrUnityRenderer != nullptr)
	{
		osvrUnityRenderer->ShutdownRenderManager();
	}
}

// Called from Unity to create a RenderManager, passing in a ClientContext
OSVR_ReturnCode UNITY_INTERFACE_API
CreateRenderManagerFromUnity(OSVR_ClientContext context) {
	if (osvrUnityRenderer != nullptr)
	{
		return osvrUnityRenderer->CreateRenderManager(context);
	}

	return OSVR_RETURN_SUCCESS;
}

OSVR_ReturnCode UNITY_INTERFACE_API ConstructRenderBuffers() {

	/*if (!s_deviceType) {
	DebugLog("[OSVR Rendering Plugin] Device type not supported.");
	return OSVR_RETURN_FAILURE;
	}*/
	if (osvrUnityRenderer != nullptr)
	{
		return osvrUnityRenderer->ConstructRenderBuffers();
	}
	return OSVR_RETURN_FAILURE;
}

void UNITY_INTERFACE_API SetNearClipDistance(double distance) {
	if (osvrUnityRenderer != nullptr)
	{
		osvrUnityRenderer->SetNearClipDistance(distance);
	}
}

void UNITY_INTERFACE_API SetFarClipDistance(double distance) {
	if (osvrUnityRenderer != nullptr)
	{
		osvrUnityRenderer->SetFarClipDistance(distance);
	}
}

void UNITY_INTERFACE_API SetIPD(double ipdMeters) {
	if (osvrUnityRenderer != nullptr)
	{
		osvrUnityRenderer->SetIPD(ipdMeters);
	}
}

OSVR_ViewportDescription UNITY_INTERFACE_API GetViewport(std::uint8_t eye) {

	OSVR_ViewportDescription viewportDescription;
	if (osvrUnityRenderer != nullptr)
	{
		return osvrUnityRenderer->GetViewport(eye);
	}
	return viewportDescription;
}

OSVR_ProjectionMatrix UNITY_INTERFACE_API
GetProjectionMatrix(std::uint8_t eye) {

	OSVR_ProjectionMatrix pm;
	if (osvrUnityRenderer != nullptr)
	{
		return osvrUnityRenderer->GetProjectionMatrix(eye);
	}
	return pm;

}

OSVR_Pose3 UNITY_INTERFACE_API GetEyePose(std::uint8_t eye) {

	OSVR_Pose3 pose;
	if (osvrUnityRenderer != nullptr)
	{
		return osvrUnityRenderer->GetEyePose(eye);
	}
	return pose;
}

// --------------------------------------------------------------------------
// Should pass in eyeRenderTexture.GetNativeTexturePtr(), which gets updated in
// Unity when the camera renders.
// On Direct3D-like devices, GetNativeTexturePtr() returns a pointer to the base
// texture type (IDirect3DBaseTexture9 on D3D9, ID3D11Resource on D3D11). On
// OpenGL-like devices the texture "name" is returned; cast the pointer to
// integer type to get it. On platforms that do not support native code plugins,
// this function always returns NULL.
// Note that calling this function when using multi-threaded rendering will
// synchronize with the rendering thread (a slow operation), so best practice is
// to set up needed texture pointers only at initialization time.
// For more reference, see:
// http://docs.unity3d.com/ScriptReference/Texture.GetNativeTexturePtr.html
int UNITY_INTERFACE_API SetColorBufferFromUnity(void *texturePtr,
	std::uint8_t eye, std::uint8_t buffer) {
	/*if (!s_deviceType) {
	return OSVR_RETURN_FAILURE;
	}*/

	DebugLog("[OSVR Rendering Plugin] SetColorBufferFromUnity");
	if (osvrUnityRenderer != nullptr)
	{
		osvrUnityRenderer->SetColorBuffer(texturePtr, eye, buffer);
	}
	return OSVR_RETURN_SUCCESS;
}
// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent.
/// @todo does this actually need to be exported? It seems like
/// GetRenderEventFunc returning it would be sufficient...
void UNITY_INTERFACE_API OnRenderEvent(int eventID) {
	// Unknown graphics device type? Do nothing.
	/*if (!s_deviceType) {
	return;
	}*/

	switch (eventID) {
		// Call the Render loop
	case kOsvrEventID_Render:
		if (osvrUnityRenderer != nullptr)
		{
			osvrUnityRenderer->OnRenderEvent();
		}
		break;
	case kOsvrEventID_Shutdown:
		break;
	case kOsvrEventID_Update:
		if (osvrUnityRenderer != nullptr)
		{
			osvrUnityRenderer->UpdateRenderInfo();
		}
		break;
	case kOsvrEventID_ConstructBuffers:
		if (osvrUnityRenderer != nullptr)
		{
			osvrUnityRenderer->ConstructRenderBuffers();
		}
		// SetRoomRotationUsingHead();
		break;
	case kOsvrEventID_ClearRoomToWorldTransform:
		// ClearRoomToWorldTransform();
		break;
	default:
		break;
	}
}

// --------------------------------------------------------------------------
// GetRenderEventFunc, a function we export which is used to get a
// rendering event callback function.
UnityRenderingEvent UNITY_INTERFACE_API GetRenderEventFunc() {
	return &OnRenderEvent;
}
