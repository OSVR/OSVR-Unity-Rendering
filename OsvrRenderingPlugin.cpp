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

// Internal includes
#include "OsvrRenderingPlugin.h"
#include "Unity/IUnityGraphics.h"

// Library includes
#include "osvr/RenderKit/RenderManager.h"
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include <osvr/Util/MatrixConventionsC.h>

// standard includes
//#include <iostream>
#include <memory>
//#include <stdlib.h>
#include <string>
//#include <time.h>

#if UNITY_WIN
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
//#include <DirectXMath.h>
//#include <initguid.h>
//#include <windows.h>
//#include <wrl.h>
#endif

// Include headers for the graphics APIs we support
#if SUPPORT_D3D11
#include <d3d11.h>

#include "Unity/IUnityGraphicsD3D11.h"
#include <osvr/RenderKit/GraphicsLibraryD3D11.h>
// using namespace DirectX;
#endif

#if SUPPORT_OPENGL
#if UNITY_WIN || UNITY_LINUX
// Needed for render buffer calls.  OSVR will have called glewInit() for us
// when we open the display.
#include <GL/glew.h>

#include <GL/gl.h>

#include <osvr/RenderKit/GraphicsLibraryOpenGL.h>
#include <osvr/RenderKit/RenderKitGraphicsTransforms.h>
#else
// Mac OpenGL include
#include <OpenGL/OpenGL.h>
#endif
#endif

// COM-like Release macro
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(a) if (a) { a->Release(); a = nullptr; }
#endif

//VARIABLES
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static osvr::renderkit::RenderManager::RenderParams renderParams;
static osvr::renderkit::RenderManager *render;
static OSVR_ClientContext clientContext;
static std::vector<osvr::renderkit::RenderBuffer> renderBuffers;
static std::vector<osvr::renderkit::RenderInfo> renderInfo;
static osvr::renderkit::GraphicsLibrary library;
static void *leftEyeTexturePtr = nullptr;
static void *rightEyeTexturePtr = nullptr;
double nearClipDistance = 0.1;
double farClipDistance = 1000.0;
double ipd = 0.063;

//forward function declarations
int ConstructBuffersD3D11(int eye);
int ConstructBuffersOpenGL(int eye);

//D3D11 vars
#if SUPPORT_D3D11
static D3D11_TEXTURE2D_DESC textureDesc;
#endif

//OpenGL vars
#if SUPPORT_OPENGL
GLuint frameBuffer;
#endif

// RenderEvents
// Called from Unity with GL.IssuePluginEvent
enum RenderEvents 
{ 
	kOsvrEventID_Render = 0,
	kOsvrEventID_Shutdown = 1,
	kOsvrEventID_Update = 2,
	kOsvrEventID_SetRoomRotationUsingHead = 3,
	kOsvrEventID_ClearRoomToWorldTransform = 4
};


// --------------------------------------------------------------------------
// Helper utilities

// Allow writing to the Unity debug console from inside DLL land.
extern "C" {
	void(_stdcall *debugLog)(const char *) = nullptr;

	void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API LinkDebug(void(_stdcall *d)(const char *))
	{
		debugLog = d;
	}
}

// Only for debugging purposes, as this causes some errors at shutdown
static inline void DebugLog(const char *str) {
	#if _DEBUG
	if (debugLog)
		debugLog(str);
	#endif
}

// --------------------------------------------------------------------------
// UnitySetInterfaces
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

extern "C" void	UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
	s_UnityInterfaces = unityInterfaces;
	s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
	s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

	// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	OnGraphicsDeviceEvent(kUnityGfxDeviceEventShutdown);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API ShutdownRenderManager()
{
	DebugLog("[OSVR Rendering Plugin] Shutting down RenderManager.");
	if (render != nullptr)
	{
		delete render;
		render = nullptr;
		rightEyeTexturePtr = nullptr;
		leftEyeTexturePtr = nullptr;
	}
	
}


// --------------------------------------------------------------------------
// GraphicsDeviceEvents

// Actual setup/teardown functions defined below
#if SUPPORT_D3D11
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType);
#endif

#if SUPPORT_OPENGL
static void DoEventGraphicsDeviceOpenGL(UnityGfxDeviceEventType eventType);
#endif

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	UnityGfxRenderer currentDeviceType = s_DeviceType;

	switch (eventType)
	{
	case kUnityGfxDeviceEventInitialize:
	{
		DebugLog("[OSVR Rendering Plugin] OnGraphicsDeviceEvent(Initialize).\n");
		s_DeviceType = s_Graphics->GetRenderer();
		currentDeviceType = s_DeviceType;
		break;
	}

	case kUnityGfxDeviceEventShutdown:
	{
		DebugLog("[OSVR Rendering Plugin] OnGraphicsDeviceEvent(Shutdown).\n");
		s_DeviceType = kUnityGfxRendererNull;
		break;
	}

	case kUnityGfxDeviceEventBeforeReset:
	{
		DebugLog("[OSVR Rendering Plugin] OnGraphicsDeviceEvent(BeforeReset).\n");
		break;
	}

	case kUnityGfxDeviceEventAfterReset:
	{
		DebugLog("[OSVR Rendering Plugin] OnGraphicsDeviceEvent(AfterReset).\n");
		break;
	}
	};

#if SUPPORT_OPENGL
	if (currentDeviceType == kUnityGfxRendererOpenGL)
		DoEventGraphicsDeviceOpenGL(eventType);
#endif

#if SUPPORT_D3D11
	if (currentDeviceType == kUnityGfxRendererD3D11)
		DoEventGraphicsDeviceD3D11(eventType);
#endif
}

void UpdateRenderInfo()
{
	renderInfo = render->GetRenderInfo(renderParams);
}


extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UpdateDistortionMesh(float distanceScale[2], float centerOfProjection[2], float *polynomial, int desiredTriangles = 12800)
{
	std::vector<osvr::renderkit::RenderManager::DistortionParameters> dp;
	osvr::renderkit::RenderManager::DistortionParameters distortion;
	distortion.m_desiredTriangles = desiredTriangles;
	std::vector<float> Ds;
	Ds.push_back(distanceScale[0]);
	Ds.push_back(distanceScale[1]);
	distortion.m_distortionD = Ds;
	std::vector<float> poly;
	int len = sizeof(polynomial) / sizeof(int);
	for (size_t i = 0; i < len; i++){
		poly.push_back(polynomial[i]);
	}
	//assume each color is the same for now
	distortion.m_distortionPolynomialRed = poly;
	distortion.m_distortionPolynomialGreen = poly;
	distortion.m_distortionPolynomialBlue = poly;
	for (size_t i = 0; i < renderInfo.size(); i++) {
		std::vector<float> COP = {
			static_cast<float>(centerOfProjection[0]),
			static_cast<float>(centerOfProjection[1]) };
		distortion.m_distortionCOP = COP;
		dp.push_back(distortion);
	}
	return render->UpdateDistortionMeshes(osvr::renderkit::RenderManager::DistortionMeshType::SQUARE, dp);
}

// Updates the internal "room to world" transformation (applied to all
// tracker data for this client context instance) based on the user's head
// orientation, so that the direction the user is facing becomes -Z to your
// application. Only rotates about the Y axis (yaw).
// 
// Note that this method internally calls osvrClientUpdate() to get a head pose
// so your callbacks may be called during its execution!
void SetRoomRotationUsingHead()
{
	render->SetRoomRotationUsingHead();
}

// Clears/resets the internal "room to world" transformation back to an
// identity transformation - that is, clears the effect of any other
// manipulation of the room to world transform.
void ClearRoomToWorldTransform()
{
	render->ClearRoomToWorldTransform();
}

// Called from Unity to create a RenderManager, passing in a ClientContext
extern "C" OSVR_ReturnCode UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CreateRenderManagerFromUnity(OSVR_ClientContext context) {
	clientContext = context;
	render = osvr::renderkit::createRenderManager(context, "Direct3D11", library);
	if ((render == nullptr) || (!render->doingOkay())) {
		DebugLog("[OSVR Rendering Plugin] Could not create RenderManager");

		return OSVR_RETURN_FAILURE;
	}

	// Open the display and make sure this worked.
	osvr::renderkit::RenderManager::OpenResults ret = render->OpenDisplay();
	if (ret.status == osvr::renderkit::RenderManager::OpenStatus::FAILURE) {
		DebugLog("[OSVR Rendering Plugin] Could not open display");
		return OSVR_RETURN_FAILURE;
	}

	//create a new set of RenderParams for passing to GetRenderInfo()
	renderParams = osvr::renderkit::RenderManager::RenderParams();
	UpdateRenderInfo();

	DebugLog("[OSVR Rendering Plugin] Success!");
	return OSVR_RETURN_SUCCESS;
}

extern "C" OSVR_ReturnCode UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API ConstructRenderBuffers()
{
	UpdateRenderInfo();

	//construct buffers
	for (size_t i = 0; i < renderInfo.size(); i++) {
		switch (s_DeviceType)
		{
		case kUnityGfxRendererD3D11:
			ConstructBuffersD3D11(i);
			break;
		case kUnityGfxRendererOpenGL:
			ConstructBuffersOpenGL(i);
			break;
		default:
			DebugLog("Device type not supported.");
			return OSVR_RETURN_FAILURE;
		}
	}
	return OSVR_RETURN_SUCCESS;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetNearClipDistance(double distance)
{
	nearClipDistance = distance;
	renderParams.nearClipDistanceMeters = nearClipDistance;		
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetFarClipDistance(double distance)
{
	farClipDistance = distance;
	renderParams.farClipDistanceMeters = farClipDistance;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetIPD(double ipdMeters)
{
	ipd = ipdMeters;
	renderParams.IPDMeters = ipd;
}

extern "C" osvr::renderkit::OSVR_ViewportDescription UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetViewport(int eye)
{
	return renderInfo[eye].viewport;
}

extern "C" osvr::renderkit::OSVR_ProjectionMatrix UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetProjectionMatrix(int eye)
{
	return renderInfo[eye].projection;
}

extern "C" OSVR_Pose3 UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetEyePose(int eye)
{
	return renderInfo[eye].pose;
}

int ConstructBuffersOpenGL(int eye)
{
	//Init glew
	glewExperimental = true;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		DebugLog("glewInit failed, aborting.");
	}

	if (eye == 0)
	{
		//do this once
		glGenFramebuffers(1, &frameBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
	}

	// The color buffer for this eye.  We need to put this into
	// a generic structure for the Present function, but we only need
	// to fill in the OpenGL portion.
	if (eye == 0) //left eye
	{
		GLuint leftEyeColorBuffer = 0;
		glGenRenderbuffers(1, &leftEyeColorBuffer);
		osvr::renderkit::RenderBuffer rb;
		rb.OpenGL = new osvr::renderkit::RenderBufferOpenGL;
		rb.OpenGL->colorBufferName = leftEyeColorBuffer;
		renderBuffers.push_back(rb);
		// "Bind" the newly created texture : all future texture
		// functions will modify this texture glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, leftEyeColorBuffer);

		// Give an empty image to OpenGL ( the last "0" means "empty" )
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
			renderInfo[eye].viewport.width,
			renderInfo[eye].viewport.height,
			0,
			GL_RGB, GL_UNSIGNED_BYTE, &leftEyeColorBuffer);
	}
	else //right eye
	{
		GLuint rightEyeColorBuffer = 0;
		glGenRenderbuffers(1, &rightEyeColorBuffer);
		osvr::renderkit::RenderBuffer rb;
		rb.OpenGL = new osvr::renderkit::RenderBufferOpenGL;
		rb.OpenGL->colorBufferName = rightEyeColorBuffer;
		renderBuffers.push_back(rb);
		// "Bind" the newly created texture : all future texture
		// functions will modify this texture glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, rightEyeColorBuffer);

		// Give an empty image to OpenGL ( the last "0" means "empty" )
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
			renderInfo[eye].viewport.width,
			renderInfo[eye].viewport.height,
			0,
			GL_RGB, GL_UNSIGNED_BYTE, &rightEyeColorBuffer);
	}

	// Bilinear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return OSVR_RETURN_SUCCESS;

}

int ConstructBuffersD3D11(int eye)
{
	DebugLog("[OSVR Rendering Plugin] ConstructBuffersD3D11");
	HRESULT hr;
	// The color buffer for this eye.  We need to put this into
	// a generic structure for the Present function, but we only need
	// to fill in the Direct3D portion.
	//  Note that this texture format must be RGBA and unsigned byte,
	// so that we can present it to Direct3D for DirectMode.
	ID3D11Texture2D* D3DTexture = eye == 0 ? reinterpret_cast<ID3D11Texture2D*>(leftEyeTexturePtr) : reinterpret_cast<ID3D11Texture2D*>(rightEyeTexturePtr);
	unsigned width = static_cast<int>(renderInfo[eye].viewport.width);
	unsigned height = static_cast<int>(renderInfo[eye].viewport.height);

	D3DTexture->GetDesc(&textureDesc);

	// Fill in the resource view for your render texture buffer here
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	memset(&renderTargetViewDesc, 0, sizeof(renderTargetViewDesc));
	// This must match what was created in the texture to be rendered
	// @todo Figure this out by introspection on the texture?
	//renderTargetViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// Create the render target view.
	ID3D11RenderTargetView *renderTargetView; //< Pointer to our render target view
	hr = renderInfo[eye].library.D3D11->device->CreateRenderTargetView(
		D3DTexture, &renderTargetViewDesc, &renderTargetView);
	if (FAILED(hr)) {
		DebugLog("[OSVR Rendering Plugin] Could not create render target for eye");
		return OSVR_RETURN_FAILURE;
	}

	// Push the filled-in RenderBuffer onto the stack.
	osvr::renderkit::RenderBufferD3D11 *rbD3D = new osvr::renderkit::RenderBufferD3D11;
	rbD3D->colorBuffer = D3DTexture;
	rbD3D->colorBufferView = renderTargetView;
	osvr::renderkit::RenderBuffer rb;
	rb.D3D11 = rbD3D;
	renderBuffers.push_back(rb);

	// Register our constructed buffers so that we can use them for
	// presentation.
	if (!render->RegisterRenderBuffers(renderBuffers)) {
		DebugLog("RegisterRenderBuffers() returned false, cannot continue");
		return OSVR_RETURN_FAILURE;
	}
	
	return OSVR_RETURN_SUCCESS;
}

// Renders the view from our Unity cameras by copying data at Unity.RenderTexture.GetNativeTexturePtr() to RenderManager colorBuffers
void RenderViewD3D11(const osvr::renderkit::RenderInfo &renderInfo, ID3D11RenderTargetView *renderTargetView, int eyeIndex)
{
	auto context = renderInfo.library.D3D11->context;
	// Set up to render to the textures for this eye
	context->OMSetRenderTargets(1, &renderTargetView, NULL);

	//copy the updated RenderTexture from Unity to RenderManager colorBuffer
	renderBuffers[eyeIndex].D3D11->colorBuffer = eyeIndex == 0 ? reinterpret_cast<ID3D11Texture2D*>(leftEyeTexturePtr) : reinterpret_cast<ID3D11Texture2D*>(rightEyeTexturePtr);
}

// Render the world from the specified point of view.
//@todo This is not functional yet.
void RenderViewOpenGL(
	const osvr::renderkit::RenderInfo &renderInfo,  //< Info needed to render
	GLuint frameBuffer, //< Frame buffer object to bind our buffers to
	GLuint colorBuffer, //< Color buffer to render into
	int eyeIndex
	)
{
	// Render to our framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

	// Set color and depth buffers for the frame buffer
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		colorBuffer, 0);
	//glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		//GL_RENDERBUFFER, depthBuffer);

	// Set the list of draw buffers.
	GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers

	// Always check that our framebuffer is ok
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		DebugLog("RenderView: Incomplete Framebuffer");
		return;
	}

	// Set the viewport to cover our entire render texture.
	glViewport(0, 0,
		static_cast<GLsizei>(renderInfo.viewport.width),
		static_cast<GLsizei>(renderInfo.viewport.height));

	// Set the OpenGL projection matrix 
	GLdouble projection[16];
	osvr::renderkit::OSVR_Projection_to_OpenGL(projection, renderInfo.projection);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMultMatrixd(projection);

	/// Put the transform into the OpenGL ModelView matrix
	GLdouble modelView[16];
	osvr::renderkit::OSVR_PoseState_to_OpenGL(modelView, renderInfo.pose);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMultMatrixd(modelView);

	// Clear the screen to red and clear depth
	glClearColor(1, 0, 0, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// =================================================================
	// This is where we draw our world and hands and any other objects.
	// We're in World Space.  To find out about where to render objects
	// in OSVR spaces (like left/right hand space) we need to query the
	// interface and handle the coordinate tranforms ourselves.

	// update native texture from code
	glBindTexture(GL_TEXTURE_2D, renderBuffers[eyeIndex].OpenGL->colorBufferName);
	int texWidth, texHeight;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);

	GLuint glTex = eyeIndex == 0 ? (GLuint)leftEyeTexturePtr : (GLuint)rightEyeTexturePtr;

	//unsigned char* data = new unsigned char[texWidth*texHeight * 4];
	//FillTextureFromCode(texWidth, texHeight, texHeight * 4, data);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, (GLuint));
	//delete[] data;
	// Draw a cube with a 5-meter radius as the room we are floating in.
	//draw_cube(5.0);
}

// --------------------------------------------------------------------------
// Should pass in eyeRenderTexture.GetNativeTexturePtr(), which gets updated in Unity when the camera renders.
// On Direct3D-like devices, GetNativeTexturePtr() returns a pointer to the base texture type (IDirect3DBaseTexture9 on D3D9, 
// ID3D11Resource on D3D11). On OpenGL-like devices the texture "name" is returned; cast the pointer to integer 
// type to get it. On platforms that do not support native code plugins, this function always returns NULL.
// Note that calling this function when using multi - threaded rendering will synchronize with the rendering 
// thread(a slow operation), so best practice is to set up needed texture pointers only at initialization time.
// For more reference, see: http://docs.unity3d.com/ScriptReference/Texture.GetNativeTexturePtr.html
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetColorBufferFromUnity(void *texturePtr, int eye) {
	if (s_DeviceType == -1)
		return OSVR_RETURN_FAILURE;
	
	DebugLog("[OSVR Rendering Plugin] SetColorBufferFromUnity");
	if (eye == 0)
	{
		leftEyeTexturePtr = texturePtr;		
	}
	else
	{
		rightEyeTexturePtr = texturePtr;
	}
	
	return OSVR_RETURN_SUCCESS;
}


// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent.
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API OnRenderEvent(int eventID) {
	// Unknown graphics device type? Do nothing.
	if (s_DeviceType == -1)
		return;


	switch (eventID) {
		// Call the Render loop
	case kOsvrEventID_Render:
		if (s_DeviceType == kUnityGfxRendererD3D11)
		{
			// Render into each buffer using the specified information.
			for (size_t i = 0; i < renderInfo.size(); i++) {
				RenderViewD3D11(renderInfo[i], renderBuffers[i].D3D11->colorBufferView, i);
			}

			// Send the rendered results to the screen
			// Flip Y because Unity RenderTextures are upside-down on D3D11
			if (!render->PresentRenderBuffers(renderBuffers, renderInfo, osvr::renderkit::RenderManager::RenderParams(), std::vector<osvr::renderkit::OSVR_ViewportDescription>(), true)) {
				DebugLog("[OSVR Rendering Plugin] PresentRenderBuffers() returned false, maybe because it was asked to quit");
			}
		}
		// OpenGL
		//@todo OpenGL path is not working yet
		else if (s_DeviceType == kUnityGfxRendererOpenGL)
		{
			// Render into each buffer using the specified information.
			for (size_t i = 0; i < renderInfo.size(); i++) {
				RenderViewOpenGL(renderInfo[i], frameBuffer, renderBuffers[i].OpenGL->colorBufferName,i);
			}

			// Send the rendered results to the screen
			if (!render->PresentRenderBuffers(renderBuffers, renderInfo)) {
				DebugLog("PresentRenderBuffers() returned false, maybe because it was asked to quit");
			}

		}
		break;
	case kOsvrEventID_Shutdown:
		break;
	case kOsvrEventID_Update:
		UpdateRenderInfo();
		break;
	case kOsvrEventID_SetRoomRotationUsingHead:
		SetRoomRotationUsingHead();
		break;
	case kOsvrEventID_ClearRoomToWorldTransform:
		ClearRoomToWorldTransform();
		break;
	default:
		break;
	}
}

// --------------------------------------------------------------------------
// GetRenderEventFunc, an example function we export which is used to get a rendering event callback function.
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	return OnRenderEvent;
}


// -------------------------------------------------------------------
//  Direct3D 11 setup/teardown code
#if SUPPORT_D3D11
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();

		// Put the device and context into a structure to let RenderManager
		// know to use this one rather than creating its own.
		library.D3D11 = new osvr::renderkit::GraphicsLibraryD3D11;
		library.D3D11->device = d3d11->GetDevice();
		ID3D11DeviceContext *ctx = NULL;
		library.D3D11->device->GetImmediateContext(&ctx);
		library.D3D11->context = ctx;
		DebugLog("[OSVR Rendering Plugin] Passed Unity device/context to RenderManager library.");
	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{
		// Close the Renderer interface cleanly.
		// This should be handled in ShutdownRenderManager
	}
}

#endif


// -------------------------------------------------------------------
// OpenGL setup/teardown code
//@todo OpenGL path not implemented yet
#if SUPPORT_OPENGL

static void DoEventGraphicsDeviceOpenGL(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		if (s_DeviceType == kUnityGfxRendererOpenGL)
		{
			DebugLog("OpenGL Initialize Event");
		}

	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{
		if (s_DeviceType == kUnityGfxRendererOpenGL)
		{
			DebugLog("OpenGL Shutdown Event");
		}
	}
}
#endif