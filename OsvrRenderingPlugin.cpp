#include "UnityPluginInterface.h"
#include "osvr\ClientKit\Context.h"
#include <osvr/ClientKit/Interface.h>
#include "osvr\RenderKit\RenderManager.h"
#include <osvr/Util/MatrixConventionsC.h>

// BANDAID FIX START
// - I don't think we'll need this if render manager passes these in after the refactor
#include <osvr/ClientKit/DisplayC.h>
// BANDAID FIX END

//standard includes
#include <iostream>
#include <string>
#include <stdlib.h>

#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

// Includes from our own directory
#include "pixelshader3d.h"
#include "vertexshader3d.h"

// Include headers for the graphics APIs we support
#if SUPPORT_D3D9
#include <d3d9.h>
#endif
#if SUPPORT_D3D11
using namespace DirectX;
#include "osvr\RenderKit\GraphicsLibraryD3D11.h"
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
void(_stdcall *debugLog)(char *) = NULL;

EXPORT_API void LinkDebug(void(_stdcall *d)(char *)) {
  debugLog = d;
}
}

static inline void DebugLog(char *str) {
//#if _DEBUG
  if (debugLog)
    debugLog(str);
//#endif
}

// COM-like Release macro
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(a)                                                        \
  if (a) {                                                                     \
    a->Release();                                                              \
    a = NULL;                                                                  \
  }
#endif

//CUBE and Shader
class Cube {
public:
	Cube(float scale, bool reverse = false) {
		vertices = {
			// VERTEX                   COLOR
			{ { -scale, -scale, -scale }, { 0.0f, 0.0f, 1.0f, 1.0f } },
			{ { -scale, scale, -scale }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { scale, scale, -scale }, { 0.0f, 1.0f, 1.0f, 1.0f } },
			{ { scale, -scale, -scale }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { -scale, -scale, scale }, { 1.0f, 0.0f, 1.0f, 1.0f } },
			{ { -scale, scale, scale }, { 1.0f, 1.0f, 0.0f, 1.0f } },
			{ { scale, scale, scale }, { 1.0f, 1.0f, 1.0f, 1.0f } },
			{ { scale, -scale, scale }, { 0.0f, 0.0f, 0.0f, 1.0f } },
		};
		indices = {
			0, 1, 2, 0, 2, 3,
			4, 6, 5, 4, 7, 6,
			4, 5, 1, 4, 1, 0,
			3, 2, 6, 3, 6, 7,
			1, 5, 6, 1, 6, 2,
			4, 0, 3, 4, 3, 7
		};

		// if you want to render the inside of the box with backface culling turned on
		// then pass true for reverse.
		if (reverse) {
			std::reverse(std::begin(indices), std::end(indices));
		}
	}

	void init(ID3D11Device* device, ID3D11DeviceContext* context) {
		if (!initialized) {
			// Create the index buffer
			CD3D11_BUFFER_DESC indexBufferDesc(
				static_cast<UINT>(sizeof(WORD) * indices.size()), D3D11_BIND_INDEX_BUFFER);
			D3D11_SUBRESOURCE_DATA indexResData = { &indices[0], 0, 0 };
			auto hr = device->CreateBuffer(&indexBufferDesc, &indexResData, indexBuffer.GetAddressOf());
			if (FAILED(hr)) {
				throw std::runtime_error("Couldn't create index buffer");
			}
			context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			// Create the vertex buffer
			CD3D11_BUFFER_DESC vertexBufferDesc(
				static_cast<UINT>(sizeof(SimpleVertex) * vertices.size()),
				D3D11_BIND_VERTEX_BUFFER);
			D3D11_SUBRESOURCE_DATA subResData = { &vertices[0], 0, 0 };
			hr = device->CreateBuffer(&vertexBufferDesc, &subResData, vertexBuffer.GetAddressOf());
			if (FAILED(hr)) {
				throw std::runtime_error("Couldn't create vertex buffer");
			}

			UINT stride = sizeof(SimpleVertex);
			UINT offset = 0;
			context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);

			// Create the input layout
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SimpleVertex, Position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SimpleVertex, Color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			hr = device->CreateInputLayout(layout, _countof(layout), g_vs, sizeof(g_vs), vertexLayout.GetAddressOf());
			if (FAILED(hr)) {
				throw std::runtime_error("Could not create input layout.");
			}
			context->IASetInputLayout(vertexLayout.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			initialized = true;
		}
	}

	void draw(ID3D11Device* device, ID3D11DeviceContext* context) {
		UINT stride = sizeof(SimpleVertex);
		UINT offset = 0;

		init(device, context);
		context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
		context->IASetInputLayout(vertexLayout.Get());
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->DrawIndexed(static_cast<UINT>(indices.size()), 0, 0);
	}

private:
	Cube(const Cube&) = delete;
	Cube& operator=(const Cube&) = delete;

	struct SimpleVertex {
		XMFLOAT3 Position;
		XMFLOAT4 Color;
	};
	bool initialized = false;
	std::vector<SimpleVertex> vertices;
	std::vector<WORD> indices;
	Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> vertexLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
};

class SimpleShader {
public:
	SimpleShader() {}

	void init(ID3D11Device *device, ID3D11DeviceContext *context) {
		if (!initialized) {
			// Setup vertex shader
			auto hr = device->CreateVertexShader(g_vs, sizeof(g_vs), nullptr, vertexShader.GetAddressOf());
			if (FAILED(hr)) {
				throw std::runtime_error("Could not create vertex shader.");
			}

			// Setup pixel shader
			hr = device->CreatePixelShader(g_ps, sizeof(g_ps), nullptr, pixelShader.GetAddressOf());
			if (FAILED(hr)) {
				throw std::runtime_error("Could not create pixel shader.");
			}

			// Setup constant buffer (used for passing projection/view/world matrices to the vertex shader)
			D3D11_BUFFER_DESC constantBufferDesc;
			memset(&constantBufferDesc, 0, sizeof(D3D11_BUFFER_DESC));
			constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constantBufferDesc.ByteWidth = sizeof(cbPerObject);
			constantBufferDesc.CPUAccessFlags = 0;
			constantBufferDesc.MiscFlags = 0;
			constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;

			if (FAILED(device->CreateBuffer(&constantBufferDesc, nullptr, cbPerObjectBuffer.GetAddressOf()))) {
				throw std::runtime_error("couldn't create the cbPerObject constant buffer.");
			}
			initialized = true;
		}
	}

	void use(ID3D11Device* device, ID3D11DeviceContext* context,
		const XMMATRIX &projection, const XMMATRIX &view, const XMMATRIX &world) {
		cbPerObject wvp = {
			projection,
			view,
			world
		};

		init(device, context);
		context->VSSetShader(vertexShader.Get(), nullptr, 0);
		context->PSSetShader(pixelShader.Get(), nullptr, 0);
		context->UpdateSubresource(cbPerObjectBuffer.Get(), 0, nullptr, &wvp, 0, 0);
		context->VSSetConstantBuffers(0, 1, cbPerObjectBuffer.GetAddressOf());
	}

private:
	SimpleShader(const SimpleShader&) = delete;
	SimpleShader& operator=(const SimpleShader&) = delete;
	struct cbPerObject {
		XMMATRIX projection;
		XMMATRIX view;
		XMMATRIX world;
	};

	bool initialized = false;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
	Microsoft::WRL::ComPtr<ID3D11Buffer> cbPerObjectBuffer;
};

//VARIABLES
static Cube roomCube(1.0f, true);
static SimpleShader simpleShader;

static osvr::renderkit::RenderManager *render;
static int g_DeviceType = -1;
static OSVR_TimeValue g_Time;
OSVR_ClientContext clientContext;
std::vector<osvr::renderkit::RenderBuffer> renderBuffers;
unsigned int eyeWidth = 0;
unsigned int eyeHeight = 0;

// BANDAID FIX START
// - We're not currently iterating through the eyes in the render manager, so we just switch between left/right eye
//   every time our render callback is called. Hopefully we don't get out of sync.
static OSVR_DisplayConfig display;
static uint8_t currentEye(0);
// BANDAID FIX END

//OpenGL vars
#if SUPPORT_OPENGL
GLuint frameBuffer;               //< Groups a color buffer and a depth buffer
GLuint leftEyeColorBuffer;
GLuint leftEyeDepthBuffer;
GLuint rightEyeColorBuffer;
GLuint rightEyeDepthBuffer;
std::vector<GLuint> depthBuffers; //< Depth/stencil buffers to render into
#endif

//D3D11 vars
#if SUPPORT_D3D11
// Set up the vector of textures to render to and any framebuffer
// we need to group them.
std::vector<ID3D11Texture2D *> depthStencilTextures;
std::vector<ID3D11DepthStencilView *> depthStencilViews;
ID3D11DepthStencilState *depthStencilState;
static ID3D11Texture2D* leftEyeTexturePtr;
static ID3D11Texture2D* rightEyeTexturePtr;
#endif

// --------------------------------------------------------------------------
// Forward function declarations of functions defined below
bool SetupRendering(osvr::renderkit::GraphicsLibrary library);
static void SetDefaultGraphicsState();
void draw_cube(double radius);
int CreateDepthStencilState();

// --------------------------------------------------------------------------
// C API and internal function implementation

// RenderEvents
// If we ever decide to add more events, here's the place for it.
enum RenderEvents { kOsvrEventID_Render = 0 };

// GetEventID, returns the event code used when raising the render event for
// this plugin.
extern "C" int EXPORT_API GetEventID() 
{ 
	DebugLog("[OSVR Rendering Plugin] GetEventID");
	return kOsvrEventID_Render; 
}

// --------------------------------------------------------------------------
// SetTimeFromUnity. Would probably be passed Time.time:
// Which is the time in seconds since the start of the game.
extern "C" void EXPORT_API SetTimeFromUnity(float t)
{
	long seconds = (long)t;
	int microseconds = t - seconds;
	g_Time = OSVR_TimeValue{ seconds, microseconds };
}

// Called from Unity to create a RenderManager, passing in a ClientContext
extern "C" OSVR_ReturnCode EXPORT_API CreateRenderManagerFromUnity(OSVR_ClientContext context) {
  clientContext = context;
  //@todo Get the display config file from the display path
  //std::string displayConfigJsonFileName = "";// clientContext.getStringParameter("/display");
  //use local display config for now until we can pass in OSVR_ClientContext
  //std::string displayConfigJsonFileName = "C:/Users/DuFF/Documents/OSVR/DirectRender/test_display_config.json"; 
  std::string displayConfigJsonFileName = "C:/Users/Sensics/OSVR/DirectRender/test_display_config.json";
  std::string pipelineConfigJsonFileName = ""; //@todo schema needs to be defined
  
  // BANDAID FIX START
  // - Getting the display config in the example. This won't be necessary once the render manager
  //   does this for us.
  OSVR_ReturnCode displayReturnCode;
  do {
	  osvrClientUpdate(clientContext);
	  displayReturnCode = osvrClientGetDisplay(clientContext, &display);
  } while (displayReturnCode == OSVR_RETURN_FAILURE);

  // make sure we get a pose report first
  for (int i = 0; i < 2000; i++)
  {
	  osvrClientUpdate(clientContext);
  }
  // BANDAID FIX END

  // Open Direct3D and set up the context for rendering to
  // an HMD.  Do this using the OSVR RenderManager interface,
  // which maps to the nVidia or other vendor direct mode
  // to reduce the latency.
  // NOTE: The pipelineConfig file needs to ask for a D3D
  // context, or this won't work.
  render = osvr::renderkit::createRenderManager(context, displayConfigJsonFileName,
	  pipelineConfigJsonFileName);
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
	  DebugLog("[OSVR Rendering Plugin] Could not setup rendering");
	  return OSVR_RETURN_FAILURE;
  }

  // Do a call to get the information we need to construct our
  // color and depth render-to-texture buffers.
  std::vector<osvr::renderkit::RenderInfo> renderInfo;
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

extern "C" int EXPORT_API GetEyeWidth()
{
	return eyeWidth;
}
extern "C" int EXPORT_API GetEyeHeight()
{
	return eyeHeight;
}

//Shutdown
extern "C" void EXPORT_API Shutdown()
{
	DebugLog("[OSVR Rendering Plugin] Shutdown.");
	std::vector<osvr::renderkit::RenderInfo> renderInfo;
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
		}

		
	
		// Give an empty image to OpenGL ( the last "0" means "empty" )
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
			eyeWidth,
			eyeHeight,
			0,
			GL_RGB, GL_UNSIGNED_BYTE, 0);

		// Bilinear filtering
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		// The depth buffer
		if (eye == 0) //left eye
		{
			glGenRenderbuffers(1, &leftEyeDepthBuffer);
			glBindRenderbuffer(GL_RENDERBUFFER, leftEyeDepthBuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
				eyeWidth,
				eyeHeight);
			depthBuffers.push_back(leftEyeDepthBuffer);
		}
		else //right eye
		{
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
	osvrClientUpdate(clientContext);
	std::vector<osvr::renderkit::RenderInfo> renderInfo;
	renderInfo = render->GetRenderInfo();
	HRESULT hr;
	// The color buffer for this eye.  We need to put this into
	// a generic structure for the Present function, but we only need
	// to fill in the Direct3D portion.
	//  Note that this texture format must be RGBA and unsigned byte,
	// so that we can present it to Direct3D for DirectMode.
	ID3D11Texture2D *D3DTexture = (ID3D11Texture2D*)texturePtr;
	unsigned width = static_cast<int>(renderInfo[eye].viewport.width);
	unsigned height = static_cast<int>(renderInfo[eye].viewport.height);

	// Initialize a new render target texture description.
	D3D11_TEXTURE2D_DESC textureDesc;
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
		std::cerr << "Can't create texture for eye " << eye << std::endl;
		return -1;
	}

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
		std::cerr << "Could not create render target for eye " << eye
			<< std::endl;
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
		std::cerr << "Could not create depth/stencil texture for eye "
			<< eye << std::endl;
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
		std::cerr << "Could not create depth/stencil view for eye "
			<< eye << std::endl;
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
	std::vector<osvr::renderkit::RenderInfo> renderInfo;
	renderInfo = render->GetRenderInfo();
	HRESULT hr;
	// Create depth stencil state.
	// Describe how depth and stencil tests should be performed.
	D3D11_DEPTH_STENCIL_DESC depthStencilDescription = { 0 };

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
bool SetupRendering(osvr::renderkit::GraphicsLibrary library) {
	
	osvr::renderkit::GraphicsLibraryOpenGL *glLibrary = library.OpenGL;

	// Turn on depth testing, so we get correct ordering.
	glEnable(GL_DEPTH_TEST);

	return true;
}

// Callbacks to draw things in world space, left-hand space, and right-hand
// space.
void RenderViewD3D11(
	const osvr::renderkit::RenderInfo &renderInfo   //< Info needed to render
	, ID3D11RenderTargetView *renderTargetView
	, ID3D11DepthStencilView *depthStencilView
	)
{
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
	FLOAT colorRgba[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
	context->ClearRenderTargetView(renderTargetView, colorRgba);
	context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// BANDAID FIX START
	// - the following is a bandaid fix for proper view and projection matrix calculation.
	//   RenderManager will be refactored to iterate over viewer/eye/surface tree, call render for each locus in the tree,
	//   and pass the projection, view pose and matrix, and viewport. For now, we'll ignore the old values from the arguments above
	//   and query for them here, ourselves, using hard-coded viewer 0, surface 0, and alternating eye 0 and 1

	const uint16_t matrixFlags = OSVR_MATRIX_ROWMAJOR | OSVR_MATRIX_LHINPUT | OSVR_MATRIX_ROWVECTORS;

	// get projection matrix
	osvrClientGetViewerEyeSurfaceProjectionMatrixf(display, 0, currentEye,
		0, 0.01f, 1000.0f, matrixFlags, projectionD3D);

	// get the view pose
	OSVR_PoseState eyePose = renderInfo.pose;
	osvrClientGetViewerEyePose(display, 0, currentEye, &eyePose);

	// Convert poses to matrices
	osvrPose3ToMatrixf(&eyePose, matrixFlags, viewD3D);
	//osvrPose3ToMatrixf(&leftHandPose, matrixFlags, leftHandWorld);
	//osvrPose3ToMatrixf(&rightHandPose, matrixFlags, rightHandWorld);

	currentEye = (currentEye + 1) % 2;
	// BANDAID FIX END

	XMMATRIX _projectionD3D(projectionD3D),
		_viewD3D(viewD3D),
		_leftHandWorld(leftHandWorld),
		_rightHandWorld(rightHandWorld);

	// draw room
	simpleShader.use(device, context, _projectionD3D, _viewD3D, identity);
	roomCube.draw(device, context);

	// draw left hand
	//simpleShader.use(device, context, _projectionD3D, _viewD3D, _leftHandWorld);
	//handCube.draw(device, context);

	// draw right hand
	//simpleShader.use(device, context, _projectionD3D, _viewD3D, _rightHandWorld);
	//handCube.draw(device, context);
}

// Render the world from the specified point of view.
void RenderViewOpenGL(
	const osvr::renderkit::RenderInfo &renderInfo,  //< Info needed to render
	GLuint frameBuffer, //< Frame buffer object to bind our buffers to
	GLuint colorBuffer, //< Color buffer to render into
	GLuint depthBuffer  //< Depth buffer to render into
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

	// Draw a cube with a 5-meter radius as the room we are floating in.
	draw_cube(5.0);
}

// --------------------------------------------------------------------------
// Should pass in eyeRenderTexture.GetNativeTexturePtr(), which gets updated in Unity when the camera renders.
// On Direct3D-like devices, GetNativeTexturePtr() returns a pointer to the base texture type (IDirect3DBaseTexture9 on D3D9, 
// ID3D11Resource on D3D11). On OpenGL-like devices the texture "name" is returned; cast the pointer to integer 
// type to get it. On platforms that do not support native code plugins, this function always returns NULL.
// Note that calling this function when using multi - threaded rendering will synchronize with the rendering 
// thread(a slow operation), so best practice is to set up needed texture pointers only at initialization time.
//http://docs.unity3d.com/ScriptReference/Texture.GetNativeTexturePtr.html
extern "C" int EXPORT_API SetColorBufferFromUnity(void *texturePtr, int eye) {
  if (g_DeviceType == -1)
    return OSVR_RETURN_FAILURE;

  switch (g_DeviceType)
  {
  case kGfxRendererD3D11:
	  ConstructBuffersD3D11(texturePtr, eye);
	  break;
  case kGfxRendererOpenGL:
	  //texturePtr points to "name"
	  ConstructBuffersOpenGL(texturePtr, eye);
	  break;
  case kGfxRendererD3D9:
	  //@todo texturePtr points to type IDirect3DBaseTexture9
  default:
	  DebugLog("Device type not supported.");
	  return OSVR_RETURN_FAILURE;
  } 
  return OSVR_RETURN_SUCCESS;
}

//This isn't being used
extern "C" int EXPORT_API SetDepthBufferFromUnity(void *texturePtr, int eye) {
	if (g_DeviceType == -1)
		return OSVR_RETURN_FAILURE;

	depthBuffers[eye] = (GLuint)(size_t)texturePtr;

	return OSVR_RETURN_SUCCESS;
}

//this isn't being used
extern "C" int EXPORT_API GetPixels(void* buffer, int x, int y, int width, int height) {
	if (glGetError())
		return -1;

	glReadPixels(x, y, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buffer);

	if (glGetError())
		return -2;
	return 0;
}


// Actual setup/teardown functions defined below
#if SUPPORT_D3D9
static void SetGraphicsDeviceD3D9(IDirect3DDevice9 *device,
                                  GfxDeviceEventType eventType);
#endif
#if SUPPORT_D3D11
static void SetGraphicsDeviceD3D11(ID3D11Device *device,
                                   GfxDeviceEventType eventType);
#endif
// --------------------------------------------------------------------------
// UnitySetGraphicsDevice
extern "C" void EXPORT_API UnitySetGraphicsDevice(void *device, int deviceType,
                                                  int eventType) {
  // Set device type to -1, i.e. "not recognized by our plugin"
  g_DeviceType = -1;

#if SUPPORT_D3D9
  // D3D9 device, remember device pointer and device type.
  // The pointer we get is IDirect3DDevice9.
  if (deviceType == kGfxRendererD3D9) {
    DebugLog("[OSVR Rendering Plugin] Set D3D9 graphics device");
    g_DeviceType = deviceType;
    SetGraphicsDeviceD3D9((IDirect3DDevice9 *)device,
                          (GfxDeviceEventType)eventType);
  }
#endif

#if SUPPORT_D3D11
  // D3D11 device, remember device pointer and device type.
  // The pointer we get is ID3D11Device.
  if (deviceType == kGfxRendererD3D11) {
    DebugLog("[OSVR Rendering Plugin] Set D3D11 graphics device");
    g_DeviceType = deviceType;
    SetGraphicsDeviceD3D11((ID3D11Device *)device,
                           (GfxDeviceEventType)eventType);
  }
#endif

#if SUPPORT_OPENGL
  // If we've got an OpenGL device, remember device type. There's no OpenGL
  // "device pointer" to remember since OpenGL always operates on a currently
  // set
  // global context.
  if (deviceType == kGfxRendererOpenGL) {
    DebugLog("[OSVR Rendering Plugin] Set OpenGL graphics device");
    g_DeviceType = deviceType;
  }
#endif
}


// --------------------------------------------------------------------------
// UnityRenderEvent
// This will be called for GL.IssuePluginEvent script calls; eventID will
// be the integer passed to IssuePluginEvent. In this example, we just ignore
// that value.
extern "C" void EXPORT_API UnityRenderEvent(int eventID) {
  // Unknown graphics device type? Do nothing.
  if (g_DeviceType == -1)
    return;

  std::vector<osvr::renderkit::RenderInfo> renderInfo;
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
	  if (g_DeviceType == kGfxRendererD3D11)
	  {
		  // Render into each buffer using the specified information.
		  for (size_t i = 0; i < renderInfo.size(); i++) {
			  renderInfo[i].library.D3D11->context->OMSetDepthStencilState(depthStencilState, 1);
			  RenderViewD3D11(renderInfo[i], renderBuffers[i].D3D11->colorBufferView,
				  depthStencilViews[i]);
		  }

		  // Send the rendered results to the screen
		  if (!render->PresentRenderBuffers(renderBuffers)) {
			  std::cerr << "PresentRenderBuffers() returned false, maybe because it was asked to quit" << std::endl;
			  quit = true;
		  }
	  }
	  // OpenGL
	  else if (g_DeviceType == kGfxRendererOpenGL)
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
				  depthBuffers[i]);
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
//  Direct3D 9 setup/teardown code

#if SUPPORT_D3D9

static IDirect3DDevice9 *g_D3D9Device;

// A dynamic vertex buffer just to demonstrate how to handle D3D9 device resets.
static IDirect3DVertexBuffer9 *g_D3D9DynamicVB;

static void SetGraphicsDeviceD3D9(IDirect3DDevice9 *device,
                                  GfxDeviceEventType eventType) {
  DebugLog("[OSVR Rendering Plugin] Set D3D9 graphics device");
  g_D3D9Device = device;

  // Create or release a small dynamic vertex buffer depending on the event
  // type.
  switch (eventType) {
  case kGfxDeviceEventInitialize:
  case kGfxDeviceEventAfterReset:
    // After device is initialized or was just reset, create the VB.
    if (!g_D3D9DynamicVB)
      g_D3D9Device->CreateVertexBuffer(1024,
                                       D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0,
                                       D3DPOOL_DEFAULT, &g_D3D9DynamicVB, NULL);
    break;
  case kGfxDeviceEventBeforeReset:
  case kGfxDeviceEventShutdown:
    // Before device is reset or being shut down, release the VB.
    SAFE_RELEASE(g_D3D9DynamicVB);
    break;
  }
}

#endif // #if SUPPORT_D3D9

// -------------------------------------------------------------------
//  Direct3D 11 setup/teardown code

#if SUPPORT_D3D11

static ID3D11Device *g_D3D11Device;
static ID3D11Buffer *g_D3D11VB; // vertex buffer
static ID3D11Buffer *g_D3D11CB; // constant buffer
// Cache copy for performing fast dirty checks when setting textures from Unity.
static ID3D11Texture2D *g_leftEyeTexture = NULL;
static ID3D11Texture2D *g_rightEyeTexture = NULL;
static ID3D11ShaderResourceView *g_D3D11ShaderResourceView = NULL;
static ID3D11RenderTargetView *g_D3D11RenderTargetViewLeft = NULL;
static ID3D11RenderTargetView *g_D3D11RenderTargetViewRight = NULL;
static ID3D11VertexShader *g_D3D11VertexShader;
static ID3D11PixelShader *g_D3D11PixelShader;
static ID3D11InputLayout *g_D3D11InputLayout;
static ID3D11RasterizerState *g_D3D11RasterState;
static ID3D11BlendState *g_D3D11BlendState;
static ID3D11DepthStencilState *g_D3D11DepthState;

#if !UNITY_METRO
typedef HRESULT(WINAPI *D3DCompileFunc)(
    const void *pSrcData, unsigned long SrcDataSize, const char *pFileName,
    const D3D10_SHADER_MACRO *pDefines, ID3D10Include *pInclude,
    const char *pEntrypoint, const char *pTarget, unsigned int Flags1,
    unsigned int Flags2, ID3D10Blob **ppCode, ID3D10Blob **ppErrorMsgs);

static const char *kD3D11ShaderText =
    "cbuffer MyCB : register(b0) {\n"
    "	float4x4 worldMatrix;\n"
    "}\n"
    "void VS (float3 pos : POSITION, float4 color : COLOR, out float4 ocolor : "
    "COLOR, out float4 opos : SV_Position) {\n"
    "	opos = mul (worldMatrix, float4(pos,1));\n"
    "	ocolor = color;\n"
    "}\n"
    "float4 PS (float4 color : COLOR) : SV_TARGET {\n"
    "	return color;\n"
    "}\n";
#elif UNITY_METRO
typedef std::vector<unsigned char> Buffer;
bool LoadFileIntoBuffer(const char *fileName, Buffer &data) {
  FILE *fp;
  fopen_s(&fp, fileName, "rb");
  if (fp) {
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data.resize(size);

    fread(&data[0], size, 1, fp);

    fclose(fp);

    return true;
  } else {
    return false;
  }
}

#endif
static D3D11_INPUT_ELEMENT_DESC s_DX11InputElementDesc[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA,
     0},
};
static void CreateD3D11Resources() {
  DebugLog("[OSVR Rendering Plugin] CreateD3D11Resources");
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

#if !UNITY_METRO
  // shaders
  HMODULE compiler = LoadLibraryA("D3DCompiler_43.dll");

  if (compiler == NULL) {
    // Try compiler from Windows 8 SDK
    compiler = LoadLibraryA("D3DCompiler_46.dll");
  }
  if (compiler) {
    ID3D10Blob *vsBlob = NULL;
    ID3D10Blob *psBlob = NULL;

    D3DCompileFunc compileFunc =
        (D3DCompileFunc)GetProcAddress(compiler, "D3DCompile");
    if (compileFunc) {
      HRESULT hr;
      hr = compileFunc(kD3D11ShaderText, strlen(kD3D11ShaderText), NULL, NULL,
                       NULL, "VS", "vs_4_0", 0, 0, &vsBlob, NULL);
      if (SUCCEEDED(hr)) {
        g_D3D11Device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                          vsBlob->GetBufferSize(), NULL,
                                          &g_D3D11VertexShader);
      }

      hr = compileFunc(kD3D11ShaderText, strlen(kD3D11ShaderText), NULL, NULL,
                       NULL, "PS", "ps_4_0", 0, 0, &psBlob, NULL);
      if (SUCCEEDED(hr)) {
        g_D3D11Device->CreatePixelShader(psBlob->GetBufferPointer(),
                                         psBlob->GetBufferSize(), NULL,
                                         &g_D3D11PixelShader);
      }
    }

    // input layout
    if (g_D3D11VertexShader && vsBlob) {
      g_D3D11Device->CreateInputLayout(
          s_DX11InputElementDesc, 2, vsBlob->GetBufferPointer(),
          vsBlob->GetBufferSize(), &g_D3D11InputLayout);
    }

    SAFE_RELEASE(vsBlob);
    SAFE_RELEASE(psBlob);

    FreeLibrary(compiler);
  } else {
    DebugLog("D3D11: HLSL shader compiler not found, will not render anything");
  }
#elif UNITY_METRO
  HRESULT hr = -1;
  Buffer vertexShader;
  Buffer pixelShader;
  LoadFileIntoBuffer("Data\\StreamingAssets\\SimpleVertexShader.cso",
                     vertexShader);
  LoadFileIntoBuffer("Data\\StreamingAssets\\SimplePixelShader.cso",
                     pixelShader);

  if (vertexShader.size() > 0 && pixelShader.size() > 0) {
    hr = g_D3D11Device->CreateVertexShader(
        &vertexShader[0], vertexShader.size(), nullptr, &g_D3D11VertexShader);
    if (FAILED(hr))
      DebugLog("Failed to create vertex shader.");
    hr = g_D3D11Device->CreatePixelShader(&pixelShader[0], pixelShader.size(),
                                          nullptr, &g_D3D11PixelShader);
    if (FAILED(hr))
      DebugLog("Failed to create pixel shader.");
  } else {
    DebugLog("Failed to load vertex or pixel shader.");
  }
  // input layout
  if (g_D3D11VertexShader && vertexShader.size() > 0) {
    g_D3D11Device->CreateInputLayout(s_DX11InputElementDesc, 2,
                                     &vertexShader[0], vertexShader.size(),
                                     &g_D3D11InputLayout);
  }
#endif
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
}

static void ReleaseD3D11Resources() {
  SAFE_RELEASE(g_D3D11VB);
  SAFE_RELEASE(g_D3D11CB);
  SAFE_RELEASE(g_D3D11VertexShader);
  SAFE_RELEASE(g_D3D11PixelShader);
  SAFE_RELEASE(g_D3D11InputLayout);
  SAFE_RELEASE(g_D3D11RasterState);
  SAFE_RELEASE(g_D3D11BlendState);
  SAFE_RELEASE(g_D3D11DepthState);
}

static void SetGraphicsDeviceD3D11(ID3D11Device *device,
                                   GfxDeviceEventType eventType) {
  DebugLog("[OSVR Rendering Plugin] Set D3D11 graphics device");
  g_D3D11Device = device;

  if (eventType == kGfxDeviceEventInitialize)
    CreateD3D11Resources();
  if (eventType == kGfxDeviceEventShutdown)
    ReleaseD3D11Resources();
}

#endif // #if SUPPORT_D3D11

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
#if SUPPORT_D3D9
  // D3D9 case
  if (g_DeviceType == kGfxRendererD3D9) {
    g_D3D9Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_D3D9Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_D3D9Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_D3D9Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_D3D9Device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_D3D9Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  }
#endif

#if SUPPORT_D3D11
  //@todo D3D11 case
  if (g_DeviceType == kGfxRendererD3D11) {
    ID3D11DeviceContext *ctx = NULL;
    g_D3D11Device->GetImmediateContext(&ctx);
    ctx->OMSetDepthStencilState(g_D3D11DepthState, 0);
    ctx->RSSetState(g_D3D11RasterState);
    ctx->OMSetBlendState(g_D3D11BlendState, NULL, 0xFFFFFFFF);
    ctx->Release();
  }
#endif

#if SUPPORT_OPENGL
  //@todo OpenGL case
  if (g_DeviceType == kGfxRendererOpenGL) {
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
  }
#endif
}

