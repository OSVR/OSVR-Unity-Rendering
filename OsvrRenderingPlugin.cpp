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
#define ENABLE_LOGGING 1 
#define ENABLE_LOGFILE 1

// Internal includes
#include "OsvrRenderingPlugin.h"
#include "Unity/IUnityGraphics.h"
#include "UnityRendererType.h"

// Library includes
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include <osvr/Util/Finally.h>
#include <osvr/Util/MatrixConventionsC.h>

// standard includes
#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
#include <fstream>
#include <iostream>
#endif
#include <memory>

#if UNITY_WIN
#define NO_MINMAX
#define WIN32_LEAN_AND_MEAN
#endif

// Include headers for the graphics APIs we support
#if SUPPORT_D3D11
#include <d3d11.h>
#include <osvr/RenderKit/RenderManagerD3D11C.h>
#include "Unity/IUnityGraphicsD3D11.h"

#endif // SUPPORT_D3D11

#if SUPPORT_OPENGL
//#include <osvr/RenderKit/RenderManagerOpenGLC.h>
#if UNITY_WIN || UNITY_LINUX
// Needed for render buffer calls.  OSVR will have called glewInit() for us
// when we open the display.
#include <GL/glew.h>
#include <GL/gl.h>
#else // UNITY_WIN || UNITY_LINUX ^ // v others (mac) //
// Mac OpenGL include
#include <OpenGL/OpenGL.h>
#endif //
#endif // SUPPORT_OPENGL

// VARIABLES
static IUnityInterfaces *s_UnityInterfaces = nullptr;
static IUnityGraphics *s_Graphics = nullptr;
static UnityRendererType s_deviceType = {};

static OSVR_RenderParams s_renderParams;
static OSVR_RenderInfoCollection s_renderInfoCollection;
static OSVR_RenderInfoCount numRenderInfo;
static OSVR_RenderManager s_render = nullptr;
static OSVR_RenderManagerRegisterBufferState s_registerBufferState;

static OSVR_ClientContext s_clientContext = nullptr;


static void *s_leftEyeTexturePtr = nullptr;
static void *s_rightEyeTexturePtr = nullptr;

/// @todo is this redundant? (given renderParams)
static double s_nearClipDistance = 0.1;
/// @todo is this redundant? (given renderParams)
static double s_farClipDistance = 1000.0;
/// @todo is this redundant? (given renderParams)
static double s_ipd = 0.063;

#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
static std::ofstream s_debugLogFile;
static std::streambuf *s_oldCout = nullptr;
static std::streambuf *s_oldCerr = nullptr;
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)

// D3D11 vars
#if SUPPORT_D3D11
static OSVR_GraphicsLibraryD3D11 s_libraryD3D;
static OSVR_RenderManagerD3D11 s_renderD3D = nullptr;
static std::vector<OSVR_RenderInfoD3D11> s_renderInfoD3D;
static std::vector<OSVR_RenderBufferD3D11> s_renderBuffers;
static D3D11_TEXTURE2D_DESC s_textureDesc;
#endif // SUPPORT_D3D11

// OpenGL vars
#if SUPPORT_OPENGL
static OSVR_GraphicsLibraryOpenGL s_libraryOpenGL;
static OSVR_RenderManagerOpenGL s_renderOpenGL;
static std::vector<OSVR_RenderBufferOpenGL> s_colorBuffers;
static std::vector<GLuint> s_depthBuffers; //< Depth/stencil buffers to render into
static GLuint s_frameBuffer;
#endif // SUPPORT_OPENGL

// @todo There shouldn't be two OSVR_ProjectionMatrix types in the API.
inline osvr::renderkit::OSVR_ProjectionMatrix ConvertProjectionMatrix(::OSVR_ProjectionMatrix matrix)
{
	osvr::renderkit::OSVR_ProjectionMatrix ret = { 0 };
	ret.bottom = matrix.bottom;
	ret.top = matrix.top;
	ret.left = matrix.left;
	ret.right = matrix.right;
	ret.nearClip = matrix.nearClip;
	ret.farClip = matrix.farClip;
	return ret;
}

// RenderEvents
// Called from Unity with GL.IssuePluginEvent
enum RenderEvents {
    kOsvrEventID_Render = 0,
    kOsvrEventID_Shutdown = 1,
    kOsvrEventID_Update = 2,
    kOsvrEventID_SetRoomRotationUsingHead = 3,
    kOsvrEventID_ClearRoomToWorldTransform = 4
};

// --------------------------------------------------------------------------
// Helper utilities

// Allow writing to the Unity debug console from inside DLL land.
static DebugFnPtr s_debugLog = nullptr;
void UNITY_INTERFACE_API LinkDebug(DebugFnPtr d) { s_debugLog = d; }

// Only for debugging purposes, as this causes some errors at shutdown
inline void DebugLog(const char *str) {
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
}

void UNITY_INTERFACE_API ShutdownRenderManager() {
    DebugLog("[OSVR Rendering Plugin] Shutting down RenderManager.");
    if (s_render != nullptr) {
        delete s_render;
        s_render = nullptr;
        s_rightEyeTexturePtr = nullptr;
        s_leftEyeTexturePtr = nullptr;
    }
    s_clientContext = nullptr;
}

// --------------------------------------------------------------------------
// GraphicsDeviceEvents

#if SUPPORT_D3D11
// -------------------------------------------------------------------
///  Direct3D 11 setup/teardown code
inline void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType) {
    BOOST_ASSERT_MSG(
        s_deviceType,
        "Should only be able to get in here with a valid device type.");
    BOOST_ASSERT_MSG(
        s_deviceType.getDeviceTypeEnum() == OSVRSupportedRenderers::D3D11,
        "Should only be able to get in here if using D3D11 device type.");

    switch (eventType) {
    case kUnityGfxDeviceEventInitialize: {
        IUnityGraphicsD3D11 *d3d11 =
            s_UnityInterfaces->Get<IUnityGraphicsD3D11>();

        // Put the device and context into a structure to let RenderManager
        // know to use this one rather than creating its own.
        s_libraryD3D.device = d3d11->GetDevice();
        ID3D11DeviceContext *ctx = nullptr;
        s_libraryD3D.device->GetImmediateContext(&ctx);
        s_libraryD3D.context = ctx;
        DebugLog("[OSVR Rendering Plugin] Passed Unity device/context to "
                 "RenderManager library.");
        break;
    }
    case kUnityGfxDeviceEventShutdown: {
        // Close the Renderer interface cleanly.
        // This should be handled in ShutdownRenderManager
        /// @todo delete library.D3D11; library.D3D11 = nullptr; ?
        break;
    }
    }
}
#endif // SUPPORT_D3D11

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
inline bool SetupRenderingOpenGL() {

	// Turn on depth testing, so we get correct ordering.
	glEnable(GL_DEPTH_TEST);

	return true;
}
#endif // SUPPORT_OPENGL

inline void dispatchEventToRenderer(UnityRendererType renderer,
                                    UnityGfxDeviceEventType eventType) {
    if (!renderer) {
        DebugLog("[OSVR Rendering Plugin] Current device type not supported");
        return;
    }
    switch (renderer.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
    case OSVRSupportedRenderers::D3D11:
        DoEventGraphicsDeviceD3D11(eventType);
        break;
#endif
#if SUPPORT_OPENGL
    case OSVRSupportedRenderers::OpenGL:
        DoEventGraphicsDeviceOpenGL(eventType);
        break;
#endif
    case OSVRSupportedRenderers::EmptyRenderer:
    default:
        break;
    }
}

/// Needs the calling convention, even though it's static and not exported,
/// because it's registered as a callback on plugin load.
static void UNITY_INTERFACE_API
OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType) {
    switch (eventType) {
    case kUnityGfxDeviceEventInitialize: {
        DebugLog(
            "[OSVR Rendering Plugin] OnGraphicsDeviceEvent(Initialize).\n");
        s_deviceType = s_Graphics->GetRenderer();
        if (!s_deviceType) {
            DebugLog("[OSVR Rendering Plugin] "
                     "OnGraphicsDeviceEvent(Initialize): New device type is "
                     "not supported!\n");
        }
        break;
    }

    case kUnityGfxDeviceEventShutdown: {
        DebugLog("[OSVR Rendering Plugin] OnGraphicsDeviceEvent(Shutdown).\n");
        /// Here, we want to dispatch before we reset the device type, so the
        /// right device type gets shut down. Thus we return instead of break.
        dispatchEventToRenderer(s_deviceType, eventType);
        s_deviceType.reset();
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

    dispatchEventToRenderer(s_deviceType, eventType);
}

// --------------------------------------------------------------------------
// UnitySetInterfaces
void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces *unityInterfaces) {
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
    s_UnityInterfaces = unityInterfaces;
    s_Graphics = s_UnityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

    // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

void UNITY_INTERFACE_API UnityPluginUnload() {
    s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventShutdown);

#if defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
    if (s_debugLogFile) {
        // Restore the buffers
        std::cout.rdbuf(s_oldCout);
        std::cerr.rdbuf(s_oldCerr);
        s_debugLogFile.close();
    }
#endif // defined(ENABLE_LOGGING) && defined(ENABLE_LOGFILE)
}

inline void UpdateRenderInfoD3D() {

	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerGetNumRenderInfo(
		s_render, s_renderParams, &numRenderInfo))) {
		DebugLog("[OSVR Rendering Plugin] Could not get context number of render infos.");
		return;
	}
	s_renderInfoD3D.clear();
	for (OSVR_RenderInfoCount i = 0; i < numRenderInfo; i++) {
		OSVR_RenderInfoD3D11 info;
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerGetRenderInfoD3D11(
			s_renderD3D, i, s_renderParams, &info))) {
			DebugLog("[OSVR Rendering Plugin] Could not get render info.");
			return ;
		}
		s_renderInfoD3D.push_back(info);
	}
}

inline void UpdateRenderInfoCollection() {
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerGetRenderInfoCollection(
		s_render, s_renderParams, &s_renderInfoCollection))) {
		DebugLog("[OSVR Rendering Plugin] Could not get render info collection.");
	}
	osvrRenderManagerGetNumRenderInfoInCollection(s_renderInfoCollection, &numRenderInfo);

}

#if 0
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UpdateDistortionMesh(float distanceScale[2], float centerOfProjection[2],
                     float *polynomial, int desiredTriangles = 12800) {
    std::vector<osvr::renderkit::RenderManager::DistortionParameters> dp;
    osvr::renderkit::RenderManager::DistortionParameters distortion;
    distortion.m_desiredTriangles = desiredTriangles;
    std::vector<float> Ds;
    Ds.push_back(distanceScale[0]);
    Ds.push_back(distanceScale[1]);
    distortion.m_distortionD = Ds;
    std::vector<float> poly;
    int len = sizeof(polynomial) / sizeof(int);
    for (size_t i = 0; i < len; i++) {
        poly.push_back(polynomial[i]);
    }
    // assume each color is the same for now
    distortion.m_distortionPolynomialRed = poly;
    distortion.m_distortionPolynomialGreen = poly;
    distortion.m_distortionPolynomialBlue = poly;
    for (size_t i = 0; i < s_renderInfo.size(); i++) {
        std::vector<float> COP = {static_cast<float>(centerOfProjection[0]),
                                  static_cast<float>(centerOfProjection[1])};
        distortion.m_distortionCOP = COP;
        dp.push_back(distortion);
    }
    return s_render->UpdateDistortionMeshes(
        osvr::renderkit::RenderManager::DistortionMeshType::SQUARE, dp);
}

#endif

// Updates the internal "room to world" transformation (applied to all
// tracker data for this client context instance) based on the user's head
// orientation, so that the direction the user is facing becomes -Z to your
// application. Only rotates about the Y axis (yaw).
//
// Note that this method internally calls osvrClientUpdate() to get a head pose
// so your callbacks may be called during its execution!
/// @todo does this actually get called from anywhere or is it dead code?
void SetRoomRotationUsingHead() { /*s_render->SetRoomRotationUsingHead();*/ }

// Clears/resets the internal "room to world" transformation back to an
// identity transformation - that is, clears the effect of any other
// manipulation of the room to world transform.
/// @todo does this actually get called from anywhere or is it dead code?
void ClearRoomToWorldTransform() { /*s_render->ClearRoomToWorldTransform();*/ }

// Called from Unity to create a RenderManager, passing in a ClientContext
OSVR_ReturnCode UNITY_INTERFACE_API
CreateRenderManagerFromUnity(OSVR_ClientContext context) {
    /// See if we're already created/running - shouldn't happen, but might.
    if (s_render != nullptr) {
		if (OSVR_RETURN_SUCCESS == osvrRenderManagerGetDoingOkay(s_render)) {
            DebugLog("[OSVR Rendering Plugin] RenderManager already created "
                     "and doing OK - will just return success without trying "
                     "to re-initialize.");
            return OSVR_RETURN_SUCCESS;
        }

        DebugLog("[OSVR Rendering Plugin] RenderManager already created, "
                 "but not doing OK. Will shut down before creating again.");
        ShutdownRenderManager();
    }
    if (s_clientContext != nullptr) {
        DebugLog(
            "[OSVR Rendering Plugin] Client context already set! Replacing...");
    }
    s_clientContext = context;

    if (!s_deviceType) {
        DebugLog("[OSVR Rendering Plugin] Attempted to create render manager, "
                 "but device type wasn't set (to a supported type) by the "
                 "plugin load/init routine. Order issue?");
        return OSVR_RETURN_FAILURE;
    }

    bool setLibraryFromOpenDisplayReturn = false;
    /// @todo We should always have a legit value in
    /// s_deviceType.getDeviceTypeEnum() at this point, right?
    switch (s_deviceType.getDeviceTypeEnum()) {

#if SUPPORT_D3D11 		
    case OSVRSupportedRenderers::D3D11:
		//s_libraryD3D.context and s_libraryD3D.device should be set by now
		//via DoEventGraphicsDeviceD3D11
		if (OSVR_RETURN_SUCCESS != osvrCreateRenderManagerD3D11(
			s_clientContext, "Direct3D11", s_libraryD3D, &s_render, &s_renderD3D)) {
			DebugLog("[OSVR Rendering Plugin] Could not create RenderManager D3D.");
			return OSVR_RETURN_FAILURE;
		}

#ifdef ATTEMPT_D3D_SHARING
        setLibraryFromOpenDisplayReturn = true;
#endif // ATTEMPT_D3D_SHARING

		//osvrClientUpdate(s_clientContext);
		// create a new set of RenderParams
		osvrRenderManagerGetDefaultRenderParams(&s_renderParams);
		UpdateRenderInfoD3D();

        break;
#endif // SUPPORT_D3D11

#if SUPPORT_OPENGL
    case OSVRSupportedRenderers::OpenGL:
		s_libraryOpenGL.toolkit = nullptr;
		if (OSVR_RETURN_SUCCESS != osvrCreateRenderManagerOpenGL(
			s_clientContext, "OpenGL", s_libraryOpenGL, &s_render, &s_renderOpenGL)) {
			DebugLog("[OSVR Rendering Plugin] Could not create the RenderManager OpenGL.");
			return 1;
		}
        setLibraryFromOpenDisplayReturn = false;

		// Open the display and make sure this worked.
		// Open the display and make sure this worked.
		OSVR_OpenResultsOpenGL openResults;
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerOpenDisplayOpenGL(
			s_renderOpenGL, &openResults)) ||
			(openResults.status == OSVR_OPEN_STATUS_FAILURE)) {

			DebugLog("[OSVR Rendering Plugin] Could not open display.");
			ShutdownRenderManager();
			return OSVR_RETURN_FAILURE;
		}
		if (setLibraryFromOpenDisplayReturn) {
			// Set our library from the one RenderManager created.
			s_libraryOpenGL = openResults.library;
		}

		// Set up the rendering state we need.
		if (!SetupRenderingOpenGL()) {
			DebugLog("[OSVR Rendering Plugin] Failed to setup OpenGL rendering state.");
			return OSVR_RETURN_FAILURE;
		}

		//osvrClientUpdate(s_clientContext);
		// create a new set of RenderParams
		osvrRenderManagerGetDefaultRenderParams(&s_renderParams);
		UpdateRenderInfoCollection();
        break;
#endif // SUPPORT_OPENGL
    } //end DeviceType switch statement
	
	if ((s_render == nullptr) || (OSVR_RETURN_SUCCESS != osvrRenderManagerGetDoingOkay(s_render))) {
        DebugLog("[OSVR Rendering Plugin] Could not create RenderManager");

        ShutdownRenderManager();
        return OSVR_RETURN_FAILURE;
    }  

    DebugLog("[OSVR Rendering Plugin] CreateRenderManagerFromUnity Success!");
    return OSVR_RETURN_SUCCESS;
}

/// Helper function that handles doing the loop of constructing buffers, and
/// returning failure if any of them in the loop return failure.
template <typename F, typename G>
inline OSVR_ReturnCode applyRenderBufferConstructor(const int numBuffers,
                                                    F &&bufferConstructor,
                                                    G &&bufferCleanup) {
    /// If we bail any time before the end, we'll automatically clean up the
    /// render buffers with this lambda.
    auto cleanupBuffers = osvr::util::finally([&] {
        DebugLog("[OSVR Rendering Plugin] Cleaning up render buffers.");
        for (auto &rb : s_renderBuffers) {
            bufferCleanup(rb);
        }
        s_renderBuffers.clear();
        DebugLog("[OSVR Rendering Plugin] Render buffer cleanup complete.");
    });

    /// Construct all the buffers as isntructed
    for (int i = 0; i < numBuffers; ++i) {
        auto ret = bufferConstructor(i);
        if (ret != OSVR_RETURN_SUCCESS) {
            DebugLog("[OSVR Rendering Plugin] Failed in a buffer constructor!");
            return OSVR_RETURN_FAILURE;
        }
    }

    /// Register our constructed buffers so that we can use them for
    /// presentation.
    /*if (!s_render->RegisterRenderBuffers(s_renderBuffers)) {
        DebugLog("[OSVR Rendering Plugin] RegisterRenderBuffers() returned false, cannot continue");
        return OSVR_RETURN_FAILURE;
    }*/
    /// Only if we succeed, do we cancel the cleanup and carry on.
    cleanupBuffers.cancel();
    return OSVR_RETURN_SUCCESS;
}

#if SUPPORT_OPENGL
inline OSVR_ReturnCode RegisterBufferStateOpenGL()
{
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerStartRegisterRenderBuffers(
		&s_registerBufferState))) {
		DebugLog("[OSVR Rendering Plugin] Could not start registering render buffers.");
		return OSVR_RETURN_FAILURE;
	}
}
inline GLuint GetColorBufferOpenGL(int eye) {
	return (GLuint)(size_t)(eye == 0 ? s_leftEyeTexturePtr
		: s_rightEyeTexturePtr);
}
inline OSVR_ReturnCode ConstructBuffersOpenGL(int eye) {
    // Init glew
    glewExperimental = true;
    /// @todo doesn't rendermanager do this glewInit for us?
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        DebugLog("[OSVR Rendering Plugin] glewInit failed, aborting.");
        /// @todo shouldn't we return here then?
    }

    if (eye == 0) {
        // do this once
        glGenFramebuffers(1, &s_frameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, s_frameBuffer);
		// Construct the buffers we're going to need for our render-to-texture
		// code.
		RegisterBufferStateOpenGL();
    }

	
	OSVR_RenderInfoOpenGL renderInfoOpenGL = { 0 };
	if (OSVR_RETURN_SUCCESS != osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
		s_renderInfoCollection, eye, &renderInfoOpenGL)) {
		DebugLog("[OSVR Rendering Plugin] Could not get render info.");
		return OSVR_RETURN_FAILURE;
	}

	// Determine the appropriate size for the frame buffer to be used for
	// this eye.
	int width = static_cast<int>(renderInfoOpenGL.viewport.width);
	int height = static_cast<int>(renderInfoOpenGL.viewport.height);

    // The color buffer for this eye.  We need to put this into
    // a generic structure for the Present function, but we only need
    // to fill in the OpenGL portion.
    if (eye == 0) // left eye
    {
		// The color buffer for this eye.  We need to put this into
		// a generic structure for the Present function, but we only need
		// to fill in the OpenGL portion.
		//  Note that this must be used to generate a RenderBuffer, not just
		// a texture, if we want to be able to present it to be rendered
		// via Direct3D for DirectMode.  This is selected based on the
		// config file value, so we want to be sure to use the more general
		// case.
		//  Note that this texture format must be RGBA and unsigned byte,
		// so that we can present it to Direct3D for DirectMode
		
       /* GLuint leftEyeColorBuffer = 0;
        glGenRenderbuffers(1, &leftEyeColorBuffer);
        osvr::renderkit::RenderBuffer rb;
        rb.OpenGL = new osvr::renderkit::RenderBufferOpenGL;
        rb.OpenGL->colorBufferName = leftEyeColorBuffer;
        s_renderBuffers.push_back(rb);
        // "Bind" the newly created texture : all future texture
        // functions will modify this texture glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, leftEyeColorBuffer);

        // Give an empty image to OpenGL ( the last "0" means "empty" )
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     static_cast<GLsizei>(s_renderInfo[eye].viewport.width),
                     static_cast<GLsizei>(s_renderInfo[eye].viewport.height), 0,
                     GL_RGB, GL_UNSIGNED_BYTE, &leftEyeColorBuffer);*/
    } else // right eye
    {
      /*  GLuint rightEyeColorBuffer = 0;
        glGenRenderbuffers(1, &rightEyeColorBuffer);
        osvr::renderkit::RenderBuffer rb;
        rb.OpenGL = new osvr::renderkit::RenderBufferOpenGL;
        rb.OpenGL->colorBufferName = rightEyeColorBuffer;
        s_renderBuffers.push_back(rb);
        // "Bind" the newly created texture : all future texture
        // functions will modify this texture glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rightEyeColorBuffer);

        // Give an empty image to OpenGL ( the last "0" means "empty" )
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     static_cast<GLsizei>(s_renderInfo[eye].viewport.width),
                     static_cast<GLsizei>(s_renderInfo[eye].viewport.height), 0,
                     GL_RGB, GL_UNSIGNED_BYTE, &rightEyeColorBuffer);*/
    }

	GLuint colorBufferName = GetColorBufferOpenGL(eye);
	if (OSVR_RETURN_SUCCESS
		!= osvrRenderManagerCreateColorBufferOpenGL(width, height, &colorBufferName)) {
		DebugLog("[OSVR Rendering Plugin] Could not create color buffer.");
		return OSVR_RETURN_FAILURE;
	}

	OSVR_RenderBufferOpenGL rb;
	rb.colorBufferName = colorBufferName;
	s_colorBuffers.push_back(rb);

	// The depth buffer
	GLuint depthrenderbuffer;
	if (OSVR_RETURN_SUCCESS
		!= osvrRenderManagerCreateDepthBufferOpenGL(width, height, &depthrenderbuffer)) {
		DebugLog("[OSVR Rendering Plugin] Could not create depth buffer.");
		return OSVR_RETURN_FAILURE;
	}
	rb.depthStencilBufferName = depthrenderbuffer;
	s_depthBuffers.push_back(depthrenderbuffer);

	if (OSVR_RETURN_SUCCESS != osvrRenderManagerRegisterRenderBufferOpenGL(
		s_registerBufferState, rb)) {
		DebugLog("[OSVR Rendering Plugin] Could not register render buffer.");
		return OSVR_RETURN_FAILURE;
	}

	//only the last eye
	if (eye == numRenderInfo - 1)
	{
		// Register our constructed buffers so that we can use them for
		// presentation.
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerFinishRegisterRenderBuffers(
			s_render, s_registerBufferState, false))) {
			DebugLog("[OSVR Rendering Plugin] Could not start finish registering render buffers.");
			ShutdownRenderManager();
			return OSVR_RETURN_FAILURE;
		}
	}

    // Bilinear filtering
   /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	*/
    return OSVR_RETURN_SUCCESS;
}

inline void CleanupBufferOpenGL(OSVR_RenderBufferOpenGL rb) {
    /// @todo incomplete cleanup - but better than leaking in case of failure.
    //delete rb;
    //rb.OpenGL = nullptr;
	return;
}
#endif // SUPPORT_OPENGL

#if SUPPORT_D3D11
inline OSVR_ReturnCode RegisterBufferStateD3D()
{
	DebugLog("[OSVR Rendering Plugin] RegisterBufferStateD3D");
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerStartRegisterRenderBuffers(
		&s_registerBufferState))) {
		DebugLog("[OSVR Rendering Plugin] Could not start registering render buffers.");
		return OSVR_RETURN_FAILURE;
	}
	for (size_t i = 0; i < numRenderInfo; i++) {
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerRegisterRenderBufferD3D11(
			s_registerBufferState, s_renderBuffers[i]))) {
			DebugLog("[OSVR Rendering Plugin]Could not register render buffer.");
			return OSVR_RETURN_FAILURE;
		}
	}
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerFinishRegisterRenderBuffers(
		s_render, s_registerBufferState, false))) {
		DebugLog("[OSVR Rendering Plugin]Could not start finish registering render buffers.");
		return OSVR_RETURN_FAILURE;
	}
}
inline ID3D11Texture2D *GetEyeTextureD3D11(int eye) {
    return reinterpret_cast<ID3D11Texture2D *>(eye == 0 ? s_leftEyeTexturePtr
                                                        : s_rightEyeTexturePtr);
}

inline OSVR_ReturnCode ConstructBuffersD3D11(int eye) {
    DebugLog("[OSVR Rendering Plugin] ConstructBuffersD3D11");
    HRESULT hr;
    // The color buffer for this eye.  We need to put this into
    // a generic structure for the Present function, but we only need
    // to fill in the Direct3D portion.
    //  Note that this texture format must be RGBA and unsigned byte,
    // so that we can present it to Direct3D for DirectMode.
    ID3D11Texture2D *D3DTexture = GetEyeTextureD3D11(eye);
    unsigned width = static_cast<unsigned>(s_renderInfoD3D[eye].viewport.width);
	unsigned height = static_cast<unsigned>(s_renderInfoD3D[eye].viewport.height);
	DebugLog("[OSVR Rendering Plugin] got tex");
    D3DTexture->GetDesc(&s_textureDesc);

    // Fill in the resource view for your render texture buffer here
    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
    // This must match what was created in the texture to be rendered
    /// @todo Figure this out by introspection on the texture?
    // renderTargetViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    /// @todo Interesting - change this line to DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    /// and not only do you not get direct mode, you get multicolored static on
    /// the display.
    renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    renderTargetViewDesc.Texture2D.MipSlice = 0;

	DebugLog("[OSVR Rendering Plugin] got rtvdesc");
    // Create the render target view.
	ID3D11RenderTargetView *renderTargetView; // < Pointer to our render target view
    hr = s_renderInfoD3D[eye].library.device->CreateRenderTargetView(
        D3DTexture, &renderTargetViewDesc, &renderTargetView);
    if (FAILED(hr)) {
        DebugLog(
            "[OSVR Rendering Plugin] Could not create render target for eye");
        return OSVR_RETURN_FAILURE;
    }
	DebugLog("[OSVR Rendering Plugin] created render target view");
    // Push the filled-in RenderBuffer onto the stack.
	OSVR_RenderBufferD3D11 rbD3D;
    rbD3D.colorBuffer = D3DTexture;
    rbD3D.colorBufferView = renderTargetView;
	s_renderBuffers.push_back(rbD3D);
	DebugLog("[OSVR Rendering Plugin] pushed buffer onto stack");
	// Register our constructed buffers so that we can use them for
	// presentation.
	RegisterBufferStateD3D();

    // OK, we succeeded, must release ownership of that pointer now that it's in
    // RenderManager's hands.
   // rbD3D.release();
	DebugLog("[OSVR Rendering Plugin] Construction success!");

    return OSVR_RETURN_SUCCESS;
}

inline void CleanupBufferD3D11(OSVR_RenderBufferD3D11 rb) {
    //delete rb.D3D11;
   // rb.D3D11 = nullptr;
	return;
}
#endif // SUPPORT_D3D11

OSVR_ReturnCode UNITY_INTERFACE_API ConstructRenderBuffers() {
    if (!s_deviceType) {
        DebugLog("Device type not supported.");
        return OSVR_RETURN_FAILURE;
    }

    switch (s_deviceType.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
    case OSVRSupportedRenderers::D3D11:
		UpdateRenderInfoD3D();
        return applyRenderBufferConstructor(numRenderInfo, ConstructBuffersD3D11,
                                            CleanupBufferD3D11);
        break;
#endif
#if SUPPORT_OPENGL
    case OSVRSupportedRenderers::OpenGL:
		UpdateRenderInfoCollection();
		return applyRenderBufferConstructor(numRenderInfo, ConstructBuffersOpenGL,
                                            CleanupBufferOpenGL);
        break;
#endif
    case OSVRSupportedRenderers::EmptyRenderer:
    default:
        DebugLog("Device type not supported.");
        return OSVR_RETURN_FAILURE;
    }
}

void UNITY_INTERFACE_API SetNearClipDistance(double distance) {
    s_nearClipDistance = distance;
    s_renderParams.nearClipDistanceMeters = s_nearClipDistance;
}

void UNITY_INTERFACE_API SetFarClipDistance(double distance) {
    s_farClipDistance = distance;
    s_renderParams.farClipDistanceMeters = s_farClipDistance;
}

void UNITY_INTERFACE_API SetIPD(double ipdMeters) {
    s_ipd = ipdMeters;
   // s_renderParams.IPDMeters = s_ipd;
}

OSVR_ViewportDescription UNITY_INTERFACE_API
GetViewport(int eye) {
	switch (s_deviceType.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
	case OSVRSupportedRenderers::D3D11:
		return s_renderInfoD3D[eye].viewport;
		break;
#endif
#if SUPPORT_OPENGL
	case OSVRSupportedRenderers::OpenGL:
			OSVR_RenderInfoOpenGL renderInfoOpenGL = { 0 };
			osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
				s_renderInfoCollection, eye, &renderInfoOpenGL);
			return renderInfoOpenGL.viewport;
		break;
#endif
	}
    
}

OSVR_ProjectionMatrix UNITY_INTERFACE_API
GetProjectionMatrix(int eye) {
	switch (s_deviceType.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
	case OSVRSupportedRenderers::D3D11:
		return s_renderInfoD3D[eye].projection;
		break;
#endif
#if SUPPORT_OPENGL
	case OSVRSupportedRenderers::OpenGL:
		OSVR_RenderInfoOpenGL renderInfoOpenGL = { 0 };
		osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
			s_renderInfoCollection, eye, &renderInfoOpenGL);
		return renderInfoOpenGL.projection;
		break;
#endif
	}
	
}

OSVR_Pose3 UNITY_INTERFACE_API GetEyePose(int eye) {
	switch (s_deviceType.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
	case OSVRSupportedRenderers::D3D11:
		return s_renderInfoD3D[eye].pose;
		break;
#endif
#if SUPPORT_OPENGL
	case OSVRSupportedRenderers::OpenGL:
		OSVR_RenderInfoOpenGL renderInfoOpenGL = { 0 };
		osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
			s_renderInfoCollection, eye, &renderInfoOpenGL);
		return renderInfoOpenGL.pose;
		break;
#endif
	}
    
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
int UNITY_INTERFACE_API SetColorBufferFromUnity(void *texturePtr, int eye) {
	DebugLog("[OSVR Rendering Plugin] SetColorBufferFromUnity");

    if (!s_deviceType) {
		DebugLog("[OSVR Rendering Plugin] DeviceType not set. Cannot set eye buffers.");
        return OSVR_RETURN_FAILURE;
    }

	if (eye == 0) {
		s_leftEyeTexturePtr = texturePtr;
	}
	else {
		s_rightEyeTexturePtr = texturePtr;
	}

    return OSVR_RETURN_SUCCESS;
}

#if SUPPORT_D3D11
// Renders the view from our Unity cameras by copying data at
// Unity.RenderTexture.GetNativeTexturePtr() to RenderManager colorBuffers
void RenderViewD3D11(const OSVR_RenderInfoD3D11 ri,
                     ID3D11RenderTargetView *renderTargetView, int eyeIndex) {
	auto context = ri.library.context;
    // Set up to render to the textures for this eye
    context->OMSetRenderTargets(1, &renderTargetView, NULL);

    // copy the updated RenderTexture from Unity to RenderManager colorBuffer
    s_renderBuffers[eyeIndex].colorBuffer = GetEyeTextureD3D11(eyeIndex);
}
#endif // SUPPORT_D3D11

#if SUPPORT_OPENGL
// Render the world from the specified point of view.
//@todo This is not functional yet.
inline void RenderViewOpenGL(
    const OSVR_RenderInfoOpenGL ri, //< Info needed to render
    GLuint frameBufferObj, //< Frame buffer object to bind our buffers to
    GLuint colorBuffer,    //< Color buffer to render into
	GLuint depthBuffer,
    int eyeIndex) {
    // Render to our framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferObj);

    // Set color and depth buffers for the frame buffer
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorBuffer, 0);
     glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		 GL_RENDERBUFFER, depthBuffer);

    // Set the list of draw buffers.
    GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers

    // Always check that our framebuffer is ok
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        DebugLog("[OSVR Rendering Plugin] RenderView: Incomplete Framebuffer");
        return;
    }

    // Set the viewport to cover our entire render texture.
    glViewport(0, 0, static_cast<GLsizei>(ri.viewport.width),
               static_cast<GLsizei>(ri.viewport.height));

    // Set the OpenGL projection matrix
    GLdouble projection[16];
	osvr::renderkit::OSVR_ProjectionMatrix temp;
	temp.bottom = ri.projection.bottom;
	osvr::renderkit::OSVR_Projection_to_OpenGL(projection, ConvertProjectionMatrix(ri.projection));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMultMatrixd(projection);

    /// Put the transform into the OpenGL ModelView matrix
    GLdouble modelView[16];
    osvr::renderkit::OSVR_PoseState_to_OpenGL(modelView, ri.pose);
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
    glBindTexture(GL_TEXTURE_2D,
		GetColorBufferOpenGL(eyeIndex));
   // int texWidth, texHeight;
    //glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
    //glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);

	//GLuint glTex = GetColorBufferOpenGL(eyeIndex);

    // unsigned char* data = new unsigned char[texWidth*texHeight * 4];
    // FillTextureFromCode(texWidth, texHeight, texHeight * 4, data);
    // glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight, GL_RGBA,
    // GL_UNSIGNED_BYTE, (GLuint));
    // delete[] data;
    // Draw a cube with a 5-meter radius as the room we are floating in.
    // draw_cube(5.0);
}
#endif // SUPPORT_OPENGL

inline void DoRender() {
    if (!s_deviceType) {
        return;
    }

    switch (s_deviceType.getDeviceTypeEnum()) {
#if SUPPORT_D3D11
    case OSVRSupportedRenderers::D3D11: {
		const auto n = static_cast<int>(s_renderInfoD3D.size());
        // Render into each buffer using the specified information.
        for (int i = 0; i < n; ++i) {
			RenderViewD3D11(s_renderInfoD3D[i],
                            s_renderBuffers[i].colorBufferView, i);
        }

        // Send the rendered results to the screen
        // Flip Y because Unity RenderTextures are upside-down on D3D11
        /*if (!s_render->PresentRenderBuffers(
			s_renderBuffers, s_renderInfoD3D,
                osvr::renderkit::RenderManager::RenderParams(),
                std::vector<osvr::renderkit::OSVR_ViewportDescription>(),
                true)) {
            DebugLog("[OSVR Rendering Plugin] PresentRenderBuffers() returned "
                     "false, maybe because it was asked to quit");
        }*/
		// Send the rendered results to the screen
		OSVR_RenderManagerPresentState presentState;
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerStartPresentRenderBuffers(
			&presentState))) {
			DebugLog("[OSVR Rendering Plugin] Could not start presenting render buffers.");
			return;
		}
		OSVR_ViewportDescription fullView;
		fullView.left = fullView.lower = 0;
		fullView.width = fullView.height = 1;
		for (size_t i = 0; i < numRenderInfo; i++) {
			if ((OSVR_RETURN_SUCCESS != osvrRenderManagerPresentRenderBufferD3D11(
				presentState, s_renderBuffers[i], s_renderInfoD3D[i], fullView))) {
				DebugLog("[OSVR Rendering Plugin] Could not present render buffer.");
				return;
			}
		}
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerFinishPresentRenderBuffers(
			s_render, presentState, s_renderParams, false))) {
			DebugLog("[OSVR Rendering Plugin] Could not finish presenting render buffers.");
			ShutdownRenderManager();
			return;
		}
        break;
    }
#endif // SUPPORT_D3D11

#if SUPPORT_OPENGL
    case OSVRSupportedRenderers::OpenGL: {
        // OpenGL
        //@todo OpenGL path is not working yet
        // Render into each buffer using the specified information.
		const auto n = numRenderInfo;
        for (int i = 0; i < n; ++i) {
			OSVR_RenderInfoOpenGL renderInfoOpenGL = { 0 };
			osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
				s_renderInfoCollection, i, &renderInfoOpenGL);

			RenderViewOpenGL(renderInfoOpenGL, s_frameBuffer,
                             s_colorBuffers[i].colorBufferName, 
							 s_depthBuffers[i], i);
        }

        // Send the rendered results to the screen
       /* if (!s_render->PresentRenderBuffers(s_renderBuffers, s_renderInfo)) {
            DebugLog("PresentRenderBuffers() returned false, maybe because "
                     "it was asked to quit");
        }*/
		OSVR_RenderManagerPresentState presentState;
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerStartPresentRenderBuffers(
			&presentState))) {
			DebugLog("[OSVR Rendering Plugin] Could not start presenting render buffers.");
			return;
		}
		OSVR_ViewportDescription fullView;
		fullView.left = fullView.lower = 0;
		fullView.width = fullView.height = 1;
		for (size_t i = 0; i < numRenderInfo; i++) {
			OSVR_RenderInfoOpenGL renderInfo = { 0 };
			osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
				s_renderInfoCollection, i, &renderInfo);

			if ((OSVR_RETURN_SUCCESS != osvrRenderManagerPresentRenderBufferOpenGL(
				presentState, s_colorBuffers[i], renderInfo, fullView))) {
				DebugLog("[OSVR Rendering Plugin] Could not present render buffer.");
				return;
			}
		}

		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerFinishPresentRenderBuffers(
			s_render, presentState, s_renderParams, false))) {
			DebugLog("[OSVR Rendering Plugin] Could not finish presenting render buffers.");
			ShutdownRenderManager();
			return;
		}
        break;
    }
#endif // SUPPORT_OPENGL

    case OSVRSupportedRenderers::EmptyRenderer:
    default:
        break;
    }
}

// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent.
/// @todo does this actually need to be exported? It seems like
/// GetRenderEventFunc returning it would be sufficient...
void UNITY_INTERFACE_API OnRenderEvent(int eventID) {
    // Unknown graphics device type? Do nothing.
    if (!s_deviceType) {
        return;
    }

    switch (eventID) {
    // Call the Render loop
    case kOsvrEventID_Render:
        DoRender();
        break;
    case kOsvrEventID_Shutdown:
        break;
    case kOsvrEventID_Update:
		switch (s_deviceType.getDeviceTypeEnum()) {

#if SUPPORT_D3D11 		
		case OSVRSupportedRenderers::D3D11:
			UpdateRenderInfoD3D();
			break;
#endif
#if SUPPORT_OPENGL
		case OSVRSupportedRenderers::OpenGL:
			UpdateRenderInfoCollection();
			break;
#endif
		}
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
// GetRenderEventFunc, a function we export which is used to get a
// rendering event callback function.
UnityRenderingEvent UNITY_INTERFACE_API GetRenderEventFunc() {
    return &OnRenderEvent;
}
