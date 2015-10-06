#include "OsvrRenderingPlugin.h"
#include "Unity/IUnityGraphics.h"
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include "osvr\RenderKit\RenderManager.h"
#include <osvr/Util/MatrixConventionsC.h>

//standard includes
#include <iostream>
#include <string>
#include <stdlib.h>
#include <time.h>

#include <windows.h>
#include <initguid.h>
#include <wrl.h>
#include <DirectXMath.h>


// Include headers for the graphics APIs we support
#if SUPPORT_D3D11
using namespace DirectX;
#include <d3d11.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include "osvr\RenderKit\GraphicsLibraryD3D11.h"
#endif
#if SUPPORT_D3D12
#include <d3d12.h>
#include "Unity/IUnityGraphicsD3D12.h"
#endif
#if SUPPORT_OPENGL
#if UNITY_WIN || UNITY_LINUX
// Needed for render buffer calls.  OSVR will have called glewInit() for us
// when we open the display.
#include <GL/glew.h>
#include <GL/gl.h>

#include "osvr/RenderKit/GraphicsLibraryOpenGL.h"
#include "osvr/RenderKit/RenderKitGraphicsTransforms.h"
#else
#include <OpenGL/OpenGL.h>
#endif
#endif


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

static inline void DebugLog(const char *str) {
	//#if _DEBUG
	if (debugLog)
		debugLog(str);
	//#endif
}



// COM-like Release macro
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(a) if (a) { a->Release(); a = nullptr; }
#endif

//VARIABLES
static IUnityInterfaces* s_UnityInterfaces = nullptr;
static IUnityGraphics* s_Graphics = nullptr;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static osvr::renderkit::RenderManager *render;
static float g_Time;
static OSVR_ClientContext clientContext;
static std::vector<osvr::renderkit::RenderBuffer> renderBuffers;
static std::vector<osvr::renderkit::RenderInfo> renderInfo;
static unsigned int eyeWidth = 0;
static unsigned int eyeHeight = 0;
static osvr::renderkit::GraphicsLibrary library;
static void *leftEyeTexturePtr = nullptr;
static void *rightEyeTexturePtr = nullptr;

//D3D11 vars
#if SUPPORT_D3D11
// Set up the vector of textures to render to and any framebuffer
// we need to group them.
// Create a D3D11 device and context to be used, rather than
// having RenderManager make one for us.  This is an example
// of using an external one, which would be needed for clients
// that already have a rendering pipeline, like Unity.
static std::vector<ID3D11Texture2D *> depthStencilTextures;
static std::vector<ID3D11DepthStencilView *> depthStencilViews;
static ID3D11DepthStencilState *depthStencilState;
static D3D11_DEPTH_STENCIL_DESC depthStencilDescription;
static D3D11_TEXTURE2D_DESC textureDesc;
#endif


//OpenGL vars
#if SUPPORT_OPENGL
GLuint frameBuffer;               //< Groups a color buffer and a depth buffer
GLuint leftEyeColorBuffer;
GLuint rightEyeColorBuffer;
std::vector<GLuint> depthBuffers; //< Depth/stencil buffers to render into
#endif

// --------------------------------------------------------------------------
// Forward function declarations of functions defined below
static int CreateDepthStencilState(int eye);
bool SetupRendering(osvr::renderkit::GraphicsLibrary library);

// --------------------------------------------------------------------------
// C API and internal function implementation

// RenderEvents
// If we ever decide to add more events, here's the place for it.
enum RenderEvents 
{ 
	kOsvrEventID_Render = 0,
	kOsvrEventID_Shutdown = 1
};

// --------------------------------------------------------------------------
// SetTimeFromUnity. Would probably be passed Time.time:
// Which is the time in seconds since the start of the game.
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetTimeFromUnity(float t)
{
	long seconds = (long)t;
	int microseconds = t - seconds;
	g_Time = t;// OSVR_TimeValue{ seconds, microseconds };
}

// --------------------------------------------------------------------------
// SetUnityStreamingAssetsPath, an example function we export which is called by one of the scripts.

static std::string s_UnityStreamingAssetsPath;
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetUnityStreamingAssetsPath(const char* path)
{
	s_UnityStreamingAssetsPath = path;
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

// --------------------------------------------------------------------------
// GraphicsDeviceEvent

// Actual setup/teardown functions defined below
#if SUPPORT_D3D11
static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType);
#endif
#if SUPPORT_D3D12
static void DoEventGraphicsDeviceD3D12(UnityGfxDeviceEventType eventType);
#endif
#if SUPPORT_OPENGLES
static void DoEventGraphicsDeviceGLES(UnityGfxDeviceEventType eventType);
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

#if SUPPORT_D3D12
	if (currentDeviceType == kUnityGfxRendererD3D12)
		DoEventGraphicsDeviceD3D12(eventType);
#endif

#if SUPPORT_OPENGLES
	if (currentDeviceType == kUnityGfxRendererOpenGLES20 ||
		currentDeviceType == kUnityGfxRendererOpenGLES30)
		DoEventGraphicsDeviceGLES(eventType);
#endif
}


// Called from Unity to create a RenderManager, passing in a ClientContext
extern "C" OSVR_ReturnCode UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CreateRenderManagerFromUnity(OSVR_ClientContext context) {
	DoEventGraphicsDeviceD3D11(kUnityGfxDeviceEventInitialize);
	clientContext = context;
	//@todo Get the display config file from the display path
	//std::string displayConfigJsonFileName = "";// clientContext.getStringParameter("/display");
	//use local display config for now until we can pass in OSVR_ClientContext
	std::string displayConfigJsonFileName = "C:/Users/DuFF/Documents/OSVR/DirectRender/test_display_config.json";
	//std::string displayConfigJsonFileName = "C:/Users/Sensics/OSVR/DirectRender/test_display_config.json";
	/*const char *path = "/display";
	size_t length;
	osvrClientGetStringParameterLength(clientContext, path, &length);
	char *displayDescription = (char* )malloc(length);
	osvrClientGetStringParameter(clientContext, path, displayDescription, length);
	std::string displayConfigJsonFileName = path;*/
	std::string pipelineConfigJsonFileName = "C:/Users/DuFF/Documents/OSVR/DirectRender/test_rendermanager_config.json";

	render = osvr::renderkit::createRenderManager(context, displayConfigJsonFileName,
		pipelineConfigJsonFileName, library);
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

	// Set up the rendering state we need.
	if (!SetupRendering(ret.library)) {
		DebugLog("[OSVR Rendering Plugin] Could not SetupRendering");
		return OSVR_RETURN_FAILURE;
	}

	// Do a call to get the information we need to construct our
	// color and depth render-to-texture buffers.
	renderInfo = render->GetRenderInfo();
	for (size_t i = 0; i < renderInfo.size(); i++) {
		// Determine the appropriate size for the frame buffer to be used for
		// this eye.
		eyeWidth = static_cast<int>(renderInfo[i].viewport.width);
		eyeHeight = static_cast<int>(renderInfo[i].viewport.height);
	}

	DebugLog("[OSVR Rendering Plugin] Success!");
	return OSVR_RETURN_SUCCESS;
}


bool SetupRendering(osvr::renderkit::GraphicsLibrary library)
{
#if SUPPORT_OPENGL
	if (s_DeviceType == kUnityGfxRendererOpenGL)
	{
		// Make sure our pointers are filled in correctly.  The config file selects
		// the graphics library to use, and may not match our needs.
		if (library.OpenGL == nullptr) {
			std::cerr << "SetupRendering: No OpenGL GraphicsLibrary, check config file" << std::endl;
			return false;
		}

		osvr::renderkit::GraphicsLibraryOpenGL *glLibrary = library.OpenGL;

		// Turn on depth testing, so we get correct ordering.
		glEnable(GL_DEPTH_TEST);

		return true;
	}
#endif
#if SUPPORT_D3D11
	return true;
	//@todo might want to move the code from UnityPluginLoad here
#endif
#if SUPPORT_D3D12
	return true;
	//@todo
#endif
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

//Shutdown
void Shutdown()
{
	DebugLog("[OSVR Rendering Plugin] Shutdown.");
	switch (s_DeviceType)
	{
	case kUnityGfxRendererD3D11:

		/*for (size_t i = 0; i < renderInfo.size(); i++) {
			SAFE_RELEASE(depthStencilViews[i]);
			SAFE_RELEASE(depthStencilTextures[i]);
			SAFE_RELEASE(renderBuffers[i].D3D11->colorBuffer);
			SAFE_RELEASE(renderBuffers[i].D3D11->colorBufferView);			
		}
		renderBuffers.clear();
		SAFE_RELEASE(depthStencilState);*/
		rightEyeTexturePtr = nullptr;
		leftEyeTexturePtr = nullptr;
		delete render;
		DebugLog("[OSVR Rendering Plugin] Shut it down.");
		break;
	case kUnityGfxRendererOpenGL:
		// Clean up after ourselves.
		glDeleteFramebuffers(1, &frameBuffer);
		for (size_t i = 0; i < renderInfo.size(); i++) {
			glDeleteTextures(1, &renderBuffers[i].OpenGL->colorBufferName);
			delete renderBuffers[i].OpenGL;
			glDeleteRenderbuffers(1, &depthBuffers[i]);
		}
		break;
	default:
		DebugLog("Device type not supported.");
		break;
	}	
}

void ConstructBuffersOpenGL(void *texturePtr, int eye)
{
	//Init glew
	glewExperimental = true;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		DebugLog("glewInit failed, aborting.");
	}

	osvrClientUpdate(clientContext);

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
		leftEyeColorBuffer = (GLuint)(size_t)texturePtr;
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
			eyeWidth,
			eyeHeight,
			0,
			GL_RGB, GL_UNSIGNED_BYTE, &leftEyeColorBuffer);
	}
	else //right eye
	{
		rightEyeColorBuffer = (GLuint)(size_t)texturePtr;
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
			eyeWidth,
			eyeHeight,
			0,
			GL_RGB, GL_UNSIGNED_BYTE, &rightEyeColorBuffer);
	}





	// Bilinear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// The depth buffer
	if (eye == 0) //left eye
	{
		GLuint leftEyeDepthBuffer = 0;
		glGenRenderbuffers(1, &leftEyeDepthBuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, leftEyeDepthBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
			eyeWidth,
			eyeHeight);
		depthBuffers.push_back(leftEyeDepthBuffer);
	}
	else //right eye
	{
		GLuint rightEyeDepthBuffer = 0;
		glGenRenderbuffers(1, &rightEyeDepthBuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, rightEyeDepthBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
			eyeWidth,
			eyeHeight);
		depthBuffers.push_back(rightEyeDepthBuffer);
	}
}
int ConstructBuffersD3D11(void *texturePtr, int eye)
{
	DebugLog("[OSVR Rendering Plugin] ConstructBuffersD3D11");
	renderInfo = render->GetRenderInfo();
	HRESULT hr;
	// The color buffer for this eye.  We need to put this into
	// a generic structure for the Present function, but we only need
	// to fill in the Direct3D portion.
	//  Note that this texture format must be RGBA and unsigned byte,
	// so that we can present it to Direct3D for DirectMode.
	ID3D11Texture2D* D3DTexture = NULL;
	unsigned width = static_cast<int>(renderInfo[eye].viewport.width);
	unsigned height = static_cast<int>(renderInfo[eye].viewport.height);

	// Initialize a new render target texture description.
	memset(&textureDesc, 0, sizeof(textureDesc));
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	//textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	// We need it to be both a render target and a shader resource
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	// Create a new render target texture to use.
	hr = renderInfo[eye].library.D3D11->device->CreateTexture2D(
		&textureDesc, NULL, &D3DTexture);
	if (FAILED(hr)) {
		DebugLog("[OSVR Rendering Plugin] Can't create texture for eye");
		return -1;
	}
	DebugLog("[OSVR Rendering Plugin] Created texture from texturePtr");
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
		return -2;
	}

	// Push the filled-in RenderBuffer onto the stack.
	osvr::renderkit::RenderBufferD3D11 *rbD3D = new osvr::renderkit::RenderBufferD3D11;
	rbD3D->colorBuffer = D3DTexture;
	rbD3D->colorBufferView = renderTargetView;
	osvr::renderkit::RenderBuffer rb;
	rb.D3D11 = rbD3D;
	renderBuffers.push_back(rb);

	//==================================================================
	// Create a depth buffer

	// Make the depth/stencil texture.
	D3D11_TEXTURE2D_DESC textureDescription = { 0 };
	textureDescription.SampleDesc.Count = 1;
	textureDescription.SampleDesc.Quality = 0;
	textureDescription.Usage = D3D11_USAGE_DEFAULT;
	textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	textureDescription.Width = width;
	textureDescription.Height = height;
	textureDescription.MipLevels = 1;
	textureDescription.ArraySize = 1;
	textureDescription.CPUAccessFlags = 0;
	textureDescription.MiscFlags = 0;
	/// @todo Make this a parameter
	textureDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	ID3D11Texture2D *depthStencilBuffer;
	hr = renderInfo[eye].library.D3D11->device->CreateTexture2D(
		&textureDescription, NULL, &depthStencilBuffer);
	if (FAILED(hr)) {
		DebugLog("Could not create depth/stencil texture for eye ");
		return -4;
	}
	depthStencilTextures.push_back(depthStencilBuffer);

	// Create the depth/stencil view description
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription;
	memset(&depthStencilViewDescription, 0, sizeof(depthStencilViewDescription));
	depthStencilViewDescription.Format = textureDescription.Format;
	depthStencilViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDescription.Texture2D.MipSlice = 0;

	ID3D11DepthStencilView *depthStencilView;
	hr = renderInfo[eye].library.D3D11->device->CreateDepthStencilView(
		depthStencilBuffer,
		&depthStencilViewDescription,
		&depthStencilView);
	if (FAILED(hr)) {
		DebugLog("Could not create depth/stencil view for eye ");
		return -5;
	}
	depthStencilViews.push_back(depthStencilView);

	CreateDepthStencilState(eye);
	
	return hr;
}

int CreateDepthStencilState(int eye)
{
	renderInfo = render->GetRenderInfo();
	HRESULT hr;
	// Create depth stencil state.
	// Describe how depth and stencil tests should be performed.
	depthStencilDescription = { 0 };

	depthStencilDescription.DepthEnable = false;
	depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;

	depthStencilDescription.StencilEnable = false;
	depthStencilDescription.StencilReadMask = 0xFF;
	depthStencilDescription.StencilWriteMask = 0xFF;

	// Front-facing stencil operations
	depthStencilDescription.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDescription.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDescription.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDescription.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Back-facing stencil operations
	depthStencilDescription.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDescription.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDescription.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDescription.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	hr = renderInfo[eye].library.D3D11->device->CreateDepthStencilState(
		&depthStencilDescription,
		&depthStencilState);
	if (FAILED(hr)) {
		std::cerr << "Could not create depth/stencil state"
			<< std::endl;
		return -3;
	}
	return hr;
}

/**
static void FillTextureFromCode(int width, int height, int stride, unsigned char* dst)
{
	srand(time(NULL));
	float t = (float)rand();// g_Time * 4.0f;


	for (int y = 0; y < height; ++y)
	{
		unsigned char* ptr = dst;
		for (int x = 0; x < width; ++x)
		{
			// Simple oldskool "plasma effect", a bunch of combined sine waves
			int vv = int(
				(127.0f + (127.0f * sinf(x / 7.0f + t))) +
				(10.0f + (127.0f * sinf(y / 5.0f - t))) +
				(127.0f + (127.0f * sinf((x + y) / 6.0f - t))) +
				(127.0f + (127.0f * sinf(sqrtf(float(x*x + y*y)) / 4.0f - t)))
				) / 4;

			// Write the texture pixel
			ptr[0] = vv;
			ptr[1] = vv;
			ptr[2] = vv;
			ptr[3] = vv;

			// To next pixel (our pixels are 4 bpp)
			ptr += 4;
		}

		// To next image row
		dst += stride;
	}
}
**/

// Callbacks to draw things in world space, left-hand space, and right-hand
// space.
void RenderViewD3D11(
	const osvr::renderkit::RenderInfo &renderInfo   //< Info needed to render
	, ID3D11RenderTargetView *renderTargetView
	, ID3D11DepthStencilView *depthStencilView,
	int eyeIndex
	)
{
	auto context = renderInfo.library.D3D11->context;
	// Set up to render to the textures for this eye
	context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

	ID3D11Texture2D* d3dtex = eyeIndex == 0 ? reinterpret_cast<ID3D11Texture2D*>(leftEyeTexturePtr) : reinterpret_cast<ID3D11Texture2D*>(rightEyeTexturePtr);
	context->CopyResource(renderBuffers[eyeIndex].D3D11->colorBuffer, d3dtex);
}

// Render the world from the specified point of view.
void RenderViewOpenGL(
	const osvr::renderkit::RenderInfo &renderInfo,  //< Info needed to render
	GLuint frameBuffer, //< Frame buffer object to bind our buffers to
	GLuint colorBuffer, //< Color buffer to render into
	GLuint depthBuffer,  //< Depth buffer to render into
	int eyeIndex
	)
{
	// Render to our framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

	// Set color and depth buffers for the frame buffer
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		colorBuffer, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, depthBuffer);

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
//http://docs.unity3d.com/ScriptReference/Texture.GetNativeTexturePtr.html
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetColorBufferFromUnity(void *texturePtr, int eye) {
	if (s_DeviceType == -1)
		return OSVR_RETURN_FAILURE;
	
	DebugLog("[OSVR Renderingg Plugin] SetColorBufferFromUnity");
	switch (s_DeviceType)
	{
	case kUnityGfxRendererD3D11:
		if (eye == 0)
		{
			leftEyeTexturePtr = texturePtr;			
			ConstructBuffersD3D11(leftEyeTexturePtr, eye);
		}
		else
		{
			rightEyeTexturePtr = texturePtr;
			ConstructBuffersD3D11(rightEyeTexturePtr, eye);
		}
		
		break;
	case kUnityGfxRendererOpenGL:
		//texturePtr points to "name"
		ConstructBuffersOpenGL(texturePtr, eye);
		break;
	default:
		DebugLog("Device type not supported.");
		return OSVR_RETURN_FAILURE;
	}
	return OSVR_RETURN_SUCCESS;
}


// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API OnRenderEvent(int eventID) {
	// Unknown graphics device type? Do nothing.
	if (s_DeviceType == -1)
		return;

	// @todo Define more events that we might want to send
	// BeginFrame, EndFrame, DrawUILayer?
	// Call the Render loop
	switch (eventID) {
	case kOsvrEventID_Render:
		renderInfo = render->GetRenderInfo();

		if (s_DeviceType == kUnityGfxRendererD3D11)
		{
			
			// Render into each buffer using the specified information.
			for (size_t i = 0; i < renderInfo.size(); i++) {
				renderInfo[i].library.D3D11->context->OMSetDepthStencilState(depthStencilState, 1);
				RenderViewD3D11(renderInfo[i], renderBuffers[i].D3D11->colorBufferView,
					depthStencilViews[i], i);
			}

			// Send the rendered results to the screen
			// Flip Y because Unity RenderTextures are upside-down on D3D11
			if (!render->PresentRenderBuffers(renderBuffers, true)) {
				DebugLog("[OSVR Rendering Plugin] PresentRenderBuffers() returned false, maybe because it was asked to quit");
			}
		}
		// OpenGL
		else if (s_DeviceType == kUnityGfxRendererOpenGL)
		{

			// Update the system state so the GetRenderInfo will have up-to-date
			// information about the tracker state.  Then get the RenderInfo
			// @todo Check that we won't need to adjust any of our buffers.
			renderInfo = render->GetRenderInfo();
			// Render into each buffer using the specified information.
			for (size_t i = 0; i < renderInfo.size(); i++) {
				RenderViewOpenGL(renderInfo[i], frameBuffer,
					renderBuffers[i].OpenGL->colorBufferName,
					depthBuffers[i], i);
			}

			// Send the rendered results to the screen
			if (!render->PresentRenderBuffers(renderBuffers)) {
				DebugLog("PresentRenderBuffers() returned false, maybe because it was asked to quit");
			}

		}
		break;
	case kOsvrEventID_Shutdown:
		Shutdown();
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
		DebugLog("[OSVR Rendering Plugin] Close the Renderer interface cleanly..");
		delete render;
	}
}

#endif // #if SUPPORT_D3D11



// -------------------------------------------------------------------
// Direct3D 12 setup/teardown code


#if SUPPORT_D3D12
#endif // #if SUPPORT_D3D12

// -------------------------------------------------------------------
// OpenGL setup/teardown code


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

