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
	void(_stdcall *debugLog)(const char *) = NULL;

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
#define SAFE_RELEASE(a) if (a) { a->Release(); a = NULL; }
#endif

//VARIABLES
static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;
static UnityGfxRenderer s_DeviceType = kUnityGfxRendererNull;

static osvr::renderkit::RenderManager *render;
static float g_Time;
static OSVR_ClientContext clientContext;
static std::vector<osvr::renderkit::RenderBuffer> renderBuffers;
static std::vector<osvr::renderkit::RenderInfo> renderInfo;
static unsigned int eyeWidth = 0;
static unsigned int eyeHeight = 0;
static osvr::renderkit::GraphicsLibrary library;
static bool init = false;
static void *leftEyeTexturePtr = NULL;
static void *rightEyeTexturePtr = NULL;
static void *leftPixelData = NULL;
static void *rightPixelData = NULL;

//OpenGL vars
#if SUPPORT_OPENGL
GLuint frameBuffer;               //< Groups a color buffer and a depth buffer
GLuint leftEyeColorBuffer;
GLuint rightEyeColorBuffer;
std::vector<GLuint> depthBuffers; //< Depth/stencil buffers to render into
#endif

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
static ID3D11Device *myDevice;
static ID3D11DeviceContext *myContext;
#endif

// --------------------------------------------------------------------------
// Forward function declarations of functions defined below
static void SetDefaultGraphicsState();
static void draw_cube(double radius);
static int CreateDepthStencilState();
bool SetupRendering(osvr::renderkit::GraphicsLibrary library);

// --------------------------------------------------------------------------
// C API and internal function implementation

// RenderEvents
// If we ever decide to add more events, here's the place for it.
enum RenderEvents { kOsvrEventID_Render = 0 };

// GetEventID, returns the event code used when raising the render event for
// this plugin.
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetEventID()
{
	DebugLog("[OSVR Rendering Plugin] GetEventID");
	return s_DeviceType;
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UpdateTexture(void* colors, int index)
{
	DebugLog("[OSVR Rendering Plugin] UpdateTexture");
	if (index == 0)
	{
		leftPixelData = colors;
	}
	else
	{
		rightPixelData = colors;
	}
	
	DebugLog("[OSVR Rendering Plugin] UpdatedTexture");
}

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
		DebugLog("OnGraphicsDeviceEvent(Initialize).\n");
		s_DeviceType = s_Graphics->GetRenderer();
		currentDeviceType = s_DeviceType;
		break;
	}

	case kUnityGfxDeviceEventShutdown:
	{
		DebugLog("OnGraphicsDeviceEvent(Shutdown).\n");
		s_DeviceType = kUnityGfxRendererNull;
		frameBuffer = NULL;
		leftEyeTexturePtr = NULL;
		rightEyeTexturePtr = NULL;
		break;
	}

	case kUnityGfxDeviceEventBeforeReset:
	{
		DebugLog("OnGraphicsDeviceEvent(BeforeReset).\n");
		break;
	}

	case kUnityGfxDeviceEventAfterReset:
	{
		DebugLog("OnGraphicsDeviceEvent(AfterReset).\n");
		break;
	}
	};

	DebugLog("Current device type is ");
	std::string s = std::to_string(currentDeviceType);
	char const *pchar = s.c_str();  //use char const* as target type
	DebugLog(pchar);
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
	clientContext = context;
	//@todo Get the display config file from the display path
	//std::string displayConfigJsonFileName = "";// clientContext.getStringParameter("/display");
	//use local display config for now until we can pass in OSVR_ClientContext
	std::string displayConfigJsonFileName = "C:/Users/DuFF/Documents/OSVR/DirectRender/test_display_config.json";
	//std::string displayConfigJsonFileName = "C:/Users/Sensics/OSVR/DirectRender/test_display_config.json";
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
	osvrClientUpdate(clientContext);
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

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetEyeWidth()
{
	return eyeWidth;
}
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetEyeHeight()
{
	return eyeHeight;
}

//Shutdown
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API Shutdown()
{
	DebugLog("[OSVR Rendering Plugin] Shutdown.");

	osvrClientUpdate(clientContext);
	renderInfo = render->GetRenderInfo();

#if SUPPORT_OPENGL
	// Clean up after ourselves.
	glDeleteFramebuffers(1, &frameBuffer);
	for (size_t i = 0; i < renderInfo.size(); i++) {
		glDeleteTextures(1, &renderBuffers[i].OpenGL->colorBufferName);
		delete renderBuffers[i].OpenGL;
		glDeleteRenderbuffers(1, &depthBuffers[i]);
	}
#endif
#if SUPPORT_D3D11
	//@todo cleanup d3d11
#endif
	DebugLog("[OSVR Rendering Plugin] delete render now.");

	// Close the Renderer interface cleanly.
	delete render;
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
	osvrClientUpdate(clientContext);
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

	if (eye == 1)//do this once after the last eye 
	{
		CreateDepthStencilState();
	}
	return hr;
}

int CreateDepthStencilState()
{
	osvrClientUpdate(clientContext);
	renderInfo = render->GetRenderInfo();
	HRESULT hr;
	// Create depth stencil state.
	// Describe how depth and stencil tests should be performed.
	depthStencilDescription = { 0 };

	depthStencilDescription.DepthEnable = true;
	depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;

	depthStencilDescription.StencilEnable = true;
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

	hr = renderInfo[0].library.D3D11->device->CreateDepthStencilState(
		&depthStencilDescription,
		&depthStencilState);
	if (FAILED(hr)) {
		std::cerr << "Could not create depth/stencil state"
			<< std::endl;
		return -3;
	}
	return hr;
}

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

// Callbacks to draw things in world space, left-hand space, and right-hand
// space.
void RenderViewD3D11(
	const osvr::renderkit::RenderInfo &renderInfo   //< Info needed to render
	, ID3D11RenderTargetView *renderTargetView
	, ID3D11DepthStencilView *depthStencilView,
	int eyeIndex
	)
{
	//DebugLog("RenderView");
	auto context = renderInfo.library.D3D11->context;
	auto device = renderInfo.library.D3D11->device;
	float projectionD3D[16];
	float viewD3D[16];
	XMMATRIX identity = XMMatrixIdentity();
	float leftHandWorld[16];
	float rightHandWorld[16];

	// Set up to render to the textures for this eye
	context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

	// Set up the viewport we're going to draw into.
	CD3D11_VIEWPORT viewport(
		static_cast<float>(renderInfo.viewport.left),
		static_cast<float>(renderInfo.viewport.lower),
		static_cast<float>(renderInfo.viewport.width),
		static_cast<float>(renderInfo.viewport.height));
	context->RSSetViewports(1, &viewport);

	// Make a grey background
	FLOAT colorRgba[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	context->ClearRenderTargetView(renderTargetView, colorRgba);
	context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	osvr::renderkit::OSVR_PoseState_to_D3D(viewD3D, renderInfo.pose);
	osvr::renderkit::OSVR_Projection_to_D3D(projectionD3D, renderInfo.projection);

	XMMATRIX _projectionD3D(projectionD3D), _viewD3D(viewD3D);

	ID3D11DeviceContext* ctx = NULL;
	renderInfo.library.D3D11->device->GetImmediateContext(&ctx);
	ID3D11Texture2D* d3dtex = eyeIndex == 0 ? reinterpret_cast<ID3D11Texture2D*>(leftEyeTexturePtr) : reinterpret_cast<ID3D11Texture2D*>(rightEyeTexturePtr);
	ctx->CopyResource(renderBuffers[eyeIndex].D3D11->colorBuffer, d3dtex);

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

	unsigned char* data = new unsigned char[texWidth*texHeight * 4];
	FillTextureFromCode(texWidth, texHeight, texHeight * 4, data);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, data);
	delete[] data;
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

	
	DebugLog("SetColorBufferFromUnity");
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

	// Update the system state so the GetRenderInfo will have up-to-date
	// information about the tracker state.  Then get the RenderInfo
	// @todo Check that we won't need to adjust any of our buffers.
	osvrClientUpdate(clientContext);
	renderInfo = render->GetRenderInfo();

	// @todo Define more events that we might want to send
	// BeginFrame, EndFrame, DrawUILayer?
	// Call the Render loop
	switch (eventID) {
	case kOsvrEventID_Render:
		if (s_DeviceType == kUnityGfxRendererD3D11)
		{
			//ClearBuffers();
			// Render into each buffer using the specified information.
			for (size_t i = 0; i < renderInfo.size(); i++) {
				renderInfo[i].library.D3D11->context->OMSetDepthStencilState(depthStencilState, 1);
				RenderViewD3D11(renderInfo[i], renderBuffers[i].D3D11->colorBufferView,
					depthStencilViews[i], i);
			}

			// Send the rendered results to the screen
			if (!render->PresentRenderBuffers(renderBuffers)) {
				DebugLog("[OSVR Rendering Plugin] PresentRenderBuffers() returned false, maybe because it was asked to quit");
			}
		}
		// OpenGL
		else if (s_DeviceType == kUnityGfxRendererOpenGL)
		{

			// Update the system state so the GetRenderInfo will have up-to-date
			// information about the tracker state.  Then get the RenderInfo
			// @todo Check that we won't need to adjust any of our buffers.
			osvrClientUpdate(clientContext);
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

//draw a simple cube scene
static GLfloat matspec[4] = { 0.5, 0.5, 0.5, 0.0 };
static float red_col[] = { 1.0, 0.0, 0.0 };
static float grn_col[] = { 0.0, 1.0, 0.0 };
static float blu_col[] = { 0.0, 0.0, 1.0 };
static float yel_col[] = { 1.0, 1.0, 0.0 };
static float lightblu_col[] = { 0.0, 1.0, 1.0 };
static float pur_col[] = { 1.0, 0.0, 1.0 };
void draw_cube(double radius)
{
	GLfloat matspec[4] = { 0.5, 0.5, 0.5, 0.0 };
	glPushMatrix();
	glScaled(radius, radius, radius);
	glMaterialfv(GL_FRONT, GL_SPECULAR, matspec);
	glMaterialf(GL_FRONT, GL_SHININESS, 64.0);
	glBegin(GL_POLYGON);
	glColor3fv(lightblu_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, lightblu_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, lightblu_col);
	glNormal3f(0.0, 0.0, -1.0);
	glVertex3f(1.0, 1.0, -1.0);
	glVertex3f(1.0, -1.0, -1.0);
	glVertex3f(-1.0, -1.0, -1.0);
	glVertex3f(-1.0, 1.0, -1.0);
	glEnd();
	glBegin(GL_POLYGON);
	glColor3fv(blu_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, blu_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, blu_col);
	glNormal3f(0.0, 0.0, 1.0);
	glVertex3f(-1.0, 1.0, 1.0);
	glVertex3f(-1.0, -1.0, 1.0);
	glVertex3f(1.0, -1.0, 1.0);
	glVertex3f(1.0, 1.0, 1.0);
	glEnd();
	glBegin(GL_POLYGON);
	glColor3fv(yel_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, yel_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, yel_col);
	glNormal3f(0.0, -1.0, 0.0);
	glVertex3f(1.0, -1.0, 1.0);
	glVertex3f(-1.0, -1.0, 1.0);
	glVertex3f(-1.0, -1.0, -1.0);
	glVertex3f(1.0, -1.0, -1.0);
	glEnd();
	glBegin(GL_POLYGON);
	glColor3fv(grn_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, grn_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, grn_col);
	glNormal3f(0.0, 1.0, 0.0);
	glVertex3f(1.0, 1.0, 1.0);
	glVertex3f(1.0, 1.0, -1.0);
	glVertex3f(-1.0, 1.0, -1.0);
	glVertex3f(-1.0, 1.0, 1.0);
	glEnd();
	glBegin(GL_POLYGON);
	glColor3fv(pur_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, pur_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, pur_col);
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(-1.0, 1.0, 1.0);
	glVertex3f(-1.0, 1.0, -1.0);
	glVertex3f(-1.0, -1.0, -1.0);
	glVertex3f(-1.0, -1.0, 1.0);
	glEnd();
	glBegin(GL_POLYGON);
	glColor3fv(red_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, red_col);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, red_col);
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(1.0, -1.0, 1.0);
	glVertex3f(1.0, -1.0, -1.0);
	glVertex3f(1.0, 1.0, -1.0);
	glVertex3f(1.0, 1.0, 1.0);
	glEnd();
	glPopMatrix();
}

// -------------------------------------------------------------------
// Shared code

#if SUPPORT_D3D11
typedef std::vector<unsigned char> Buffer;
bool LoadFileIntoBuffer(const std::string& fileName, Buffer& data)
{
	FILE* fp;
	fopen_s(&fp, fileName.c_str(), "rb");
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		data.resize(size);

		fread(&data[0], size, 1, fp);

		fclose(fp);

		return true;
	}
	else
	{
		//std::string errorMessage = "Failed to find ";
		//errorMessage += fileName;
		DebugLog("Failed to find file.");
		return false;
	}
}
#endif


// -------------------------------------------------------------------
//  Direct3D 11 setup/teardown code


#if SUPPORT_D3D11

static ID3D11Device* g_D3D11Device = NULL;
static ID3D11Buffer* g_D3D11VB = NULL; // vertex buffer
static ID3D11Buffer* g_D3D11CB = NULL; // constant buffer
static ID3D11VertexShader* g_D3D11VertexShader = NULL;
static ID3D11PixelShader* g_D3D11PixelShader = NULL;
static ID3D11InputLayout* g_D3D11InputLayout = NULL;
static ID3D11RasterizerState* g_D3D11RasterState = NULL;
static ID3D11BlendState* g_D3D11BlendState = NULL;
static ID3D11DepthStencilState* g_D3D11DepthState = NULL;

static D3D11_INPUT_ELEMENT_DESC s_DX11InputElementDesc[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static bool EnsureD3D11ResourcesAreCreated()
{
	if (g_D3D11VertexShader)
		return true;

	// D3D11 has to load resources. Wait for Unity to provide the streaming assets path first.
	if (s_UnityStreamingAssetsPath.empty())
		return false;

	D3D11_BUFFER_DESC desc;
	memset(&desc, 0, sizeof(desc));

	// vertex buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 1024;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	g_D3D11Device->CreateBuffer(&desc, NULL, &g_D3D11VB);

	// constant buffer
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 64; // hold 1 matrix
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	g_D3D11Device->CreateBuffer(&desc, NULL, &g_D3D11CB);


	HRESULT hr = -1;
	Buffer vertexShader;
	Buffer pixelShader;
	std::string vertexShaderPath(s_UnityStreamingAssetsPath);
	vertexShaderPath += "/Shaders/DX11_9_1/SimpleVertexShader.cso";
	std::string fragmentShaderPath(s_UnityStreamingAssetsPath);
	fragmentShaderPath += "/Shaders/DX11_9_1/SimplePixelShader.cso";
	LoadFileIntoBuffer(vertexShaderPath, vertexShader);
	LoadFileIntoBuffer(fragmentShaderPath, pixelShader);

	if (vertexShader.size() > 0 && pixelShader.size() > 0)
	{
		hr = g_D3D11Device->CreateVertexShader(&vertexShader[0], vertexShader.size(), nullptr, &g_D3D11VertexShader);
		if (FAILED(hr)) DebugLog("Failed to create vertex shader.\n");
		hr = g_D3D11Device->CreatePixelShader(&pixelShader[0], pixelShader.size(), nullptr, &g_D3D11PixelShader);
		if (FAILED(hr)) DebugLog("Failed to create pixel shader.\n");
	}
	else
	{
		DebugLog("Failed to load vertex or pixel shader.\n");
	}
	// input layout
	if (g_D3D11VertexShader && vertexShader.size() > 0)
	{
		g_D3D11Device->CreateInputLayout(s_DX11InputElementDesc, 2, &vertexShader[0], vertexShader.size(), &g_D3D11InputLayout);
	}

	// render states
	D3D11_RASTERIZER_DESC rsdesc;
	memset(&rsdesc, 0, sizeof(rsdesc));
	rsdesc.FillMode = D3D11_FILL_SOLID;
	rsdesc.CullMode = D3D11_CULL_NONE;
	rsdesc.DepthClipEnable = TRUE;
	g_D3D11Device->CreateRasterizerState(&rsdesc, &g_D3D11RasterState);

	D3D11_DEPTH_STENCIL_DESC dsdesc;
	memset(&dsdesc, 0, sizeof(dsdesc));
	dsdesc.DepthEnable = TRUE;
	dsdesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsdesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	g_D3D11Device->CreateDepthStencilState(&dsdesc, &g_D3D11DepthState);

	D3D11_BLEND_DESC bdesc;
	memset(&bdesc, 0, sizeof(bdesc));
	bdesc.RenderTarget[0].BlendEnable = FALSE;
	bdesc.RenderTarget[0].RenderTargetWriteMask = 0xF;
	g_D3D11Device->CreateBlendState(&bdesc, &g_D3D11BlendState);

	return true;
}

static void ReleaseD3D11Resources()
{
	SAFE_RELEASE(g_D3D11VB);
	SAFE_RELEASE(g_D3D11CB);
	SAFE_RELEASE(g_D3D11VertexShader);
	SAFE_RELEASE(g_D3D11PixelShader);
	SAFE_RELEASE(g_D3D11InputLayout);
	SAFE_RELEASE(g_D3D11RasterState);
	SAFE_RELEASE(g_D3D11BlendState);
	SAFE_RELEASE(g_D3D11DepthState);
}

static void DoEventGraphicsDeviceD3D11(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		IUnityGraphicsD3D11* d3d11 = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();
		g_D3D11Device = d3d11->GetDevice();

		// Put the device and context into a structure to let RenderManager
		// know to use this one rather than creating its own.
		library.D3D11 = new osvr::renderkit::GraphicsLibraryD3D11;
		library.D3D11->device = d3d11->GetDevice();
		ID3D11DeviceContext *ctx = NULL;
		library.D3D11->device->GetImmediateContext(&ctx);
		library.D3D11->context = ctx;
		DebugLog("[OSVR Rendering Plugin] Passed Unity device/context to RenderManager library.");

		EnsureD3D11ResourcesAreCreated();
	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{
		ReleaseD3D11Resources();
	}
}

#endif // #if SUPPORT_D3D11



// -------------------------------------------------------------------
// Direct3D 12 setup/teardown code


#if SUPPORT_D3D12
const UINT kNodeMask = 0;

static IUnityGraphicsD3D12* s_D3D12 = NULL;
static ID3D12Resource* s_D3D12Upload = NULL;

static ID3D12CommandAllocator* s_D3D12CmdAlloc = NULL;
static ID3D12GraphicsCommandList* s_D3D12CmdList = NULL;

static ID3D12Fence* s_D3D12Fence = NULL;
static UINT64 s_D3D12FenceValue = 1;
static HANDLE s_D3D12Event = NULL;

ID3D12Resource* GetD3D12UploadResource(UINT64 size)
{
	if (s_D3D12Upload)
	{
		D3D12_RESOURCE_DESC desc = s_D3D12Upload->GetDesc();
		if (desc.Width == size)
			return s_D3D12Upload;
		else
			s_D3D12Upload->Release();
	}

	// Texture upload buffer
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = kNodeMask;
	heapProps.VisibleNodeMask = kNodeMask;

	D3D12_RESOURCE_DESC heapDesc = {};
	heapDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	heapDesc.Alignment = 0;
	heapDesc.Width = size;
	heapDesc.Height = 1;
	heapDesc.DepthOrArraySize = 1;
	heapDesc.MipLevels = 1;
	heapDesc.Format = DXGI_FORMAT_UNKNOWN;
	heapDesc.SampleDesc.Count = 1;
	heapDesc.SampleDesc.Quality = 0;
	heapDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	heapDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ID3D12Device* device = s_D3D12->GetDevice();
	HRESULT hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&heapDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&s_D3D12Upload));
	if (FAILED(hr)) DebugLog("Failed to CreateCommittedResource.\n");

	return s_D3D12Upload;
}

static void CreateD3D12Resources()
{
	ID3D12Device* device = s_D3D12->GetDevice();

	HRESULT hr = E_FAIL;

	// Command list
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_D3D12CmdAlloc));
	if (FAILED(hr)) DebugLog("Failed to CreateCommandAllocator.\n");
	hr = device->CreateCommandList(kNodeMask, D3D12_COMMAND_LIST_TYPE_DIRECT, s_D3D12CmdAlloc, nullptr, IID_PPV_ARGS(&s_D3D12CmdList));
	if (FAILED(hr)) DebugLog("Failed to CreateCommandList.\n");
	s_D3D12CmdList->Close();

	// Fence
	s_D3D12FenceValue = 1;
	device->CreateFence(s_D3D12FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_D3D12Fence));
	if (FAILED(hr)) DebugLog("Failed to CreateFence.\n");
	s_D3D12Event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

static void ReleaseD3D12Resources()
{
	SAFE_RELEASE(s_D3D12Upload);

	if (s_D3D12Event)
		CloseHandle(s_D3D12Event);

	SAFE_RELEASE(s_D3D12Fence);
	SAFE_RELEASE(s_D3D12CmdList);
	SAFE_RELEASE(s_D3D12CmdAlloc);
}

static void DoEventGraphicsDeviceD3D12(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		s_D3D12 = s_UnityInterfaces->Get<IUnityGraphicsD3D12>();
		CreateD3D12Resources();
	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{
		ReleaseD3D12Resources();
	}
}
#endif // #if SUPPORT_D3D12



// -------------------------------------------------------------------
// GLES setup/teardown code


#if SUPPORT_OPENGLES

#define VPROG_SRC(ver, attr, varying)								\
	ver																\
	attr " highp vec3 pos;\n"										\
	attr " lowp vec4 color;\n"										\
	"\n"															\
	varying " lowp vec4 ocolor;\n"									\
	"\n"															\
	"uniform highp mat4 worldMatrix;\n"								\
	"uniform highp mat4 projMatrix;\n"								\
	"\n"															\
	"void main()\n"													\
	"{\n"															\
	"	gl_Position = (projMatrix * worldMatrix) * vec4(pos,1);\n"	\
	"	ocolor = color;\n"											\
	"}\n"															\

static const char* kGlesVProgTextGLES2 = VPROG_SRC("\n", "attribute", "varying");
static const char* kGlesVProgTextGLES3 = VPROG_SRC("#version 300 es\n", "in", "out");

#undef VPROG_SRC

#define FSHADER_SRC(ver, varying, outDecl, outVar)	\
	ver												\
	outDecl											\
	varying " lowp vec4 ocolor;\n"					\
	"\n"											\
	"void main()\n"									\
	"{\n"											\
	"	" outVar " = ocolor;\n"						\
	"}\n"											\

static const char* kGlesFShaderTextGLES2 = FSHADER_SRC("\n", "varying", "\n", "gl_FragColor");
static const char* kGlesFShaderTextGLES3 = FSHADER_SRC("#version 300 es\n", "in", "out lowp vec4 fragColor;\n", "fragColor");

#undef FSHADER_SRC

static GLuint	g_VProg;
static GLuint	g_FShader;
static GLuint	g_Program;
static int		g_WorldMatrixUniformIndex;
static int		g_ProjMatrixUniformIndex;

static GLuint CreateShader(GLenum type, const char* text)
{
	GLuint ret = glCreateShader(type);
	glShaderSource(ret, 1, &text, NULL);
	glCompileShader(ret);

	return ret;
}

static void DoEventGraphicsDeviceGLES(UnityGfxDeviceEventType eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		if (s_DeviceType == kUnityGfxRendererOpenGLES20)
		{
			::printf("OpenGLES 2.0 device\n");
			g_VProg = CreateShader(GL_VERTEX_SHADER, kGlesVProgTextGLES2);
			g_FShader = CreateShader(GL_FRAGMENT_SHADER, kGlesFShaderTextGLES2);
		}
		else if (s_DeviceType == kUnityGfxRendererOpenGLES30)
		{
			::printf("OpenGLES 3.0 device\n");
			g_VProg = CreateShader(GL_VERTEX_SHADER, kGlesVProgTextGLES3);
			g_FShader = CreateShader(GL_FRAGMENT_SHADER, kGlesFShaderTextGLES3);
		}

		g_Program = glCreateProgram();
		glBindAttribLocation(g_Program, 1, "pos");
		glBindAttribLocation(g_Program, 2, "color");
		glAttachShader(g_Program, g_VProg);
		glAttachShader(g_Program, g_FShader);
		glLinkProgram(g_Program);

		g_WorldMatrixUniformIndex = glGetUniformLocation(g_Program, "worldMatrix");
		g_ProjMatrixUniformIndex = glGetUniformLocation(g_Program, "projMatrix");
	}
	else if (eventType == kUnityGfxDeviceEventShutdown)
	{

	}
}
#endif

// -------------------------------------------------------------------
// GLES setup/teardown code


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

// --------------------------------------------------------------------------
// SetDefaultGraphicsState
//
// Helper function to setup some "sane" graphics state. Rendering state
// upon call into our plugin can be almost completely arbitrary depending
// on what was rendered in Unity before.
// Before calling into the plugin, Unity will set shaders to null,
// and will unbind most of "current" objects (e.g. VBOs in OpenGL case).
//
// Here, we set culling off, lighting off, alpha blend & test off, Z
// comparison to less equal, and Z writes off.

static void SetDefaultGraphicsState() {
	DebugLog("[OSVR Rendering Plugin] Set default graphics state");


#if SUPPORT_D3D11
	// D3D11 case
	if (s_DeviceType == kUnityGfxRendererD3D11)
	{
		ID3D11DeviceContext* ctx = NULL;
		g_D3D11Device->GetImmediateContext(&ctx);
		ctx->OMSetDepthStencilState(g_D3D11DepthState, 0);
		ctx->RSSetState(g_D3D11RasterState);
		ctx->OMSetBlendState(g_D3D11BlendState, NULL, 0xFFFFFFFF);
		ctx->Release();
	}
#endif


#if SUPPORT_D3D12
	// D3D12 case
	if (s_DeviceType == kUnityGfxRendererD3D12)
	{
		// Stateless. Nothing to do.
	}
#endif


#if SUPPORT_OPENGL
	// OpenGL case
	if (s_DeviceType == kUnityGfxRendererOpenGL)
	{
		glDisable(GL_CULL_FACE);
		glDisable(GL_LIGHTING);
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}
#endif


#if SUPPORT_OPENGLES
	// OpenGLES case
	if (s_DeviceType == kUnityGfxRendererOpenGLES20 ||
		s_DeviceType == kUnityGfxRendererOpenGLES30)
	{
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
	}
#endif
}

