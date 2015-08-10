#include "UnityPluginInterface.h"
#include "osvr\ClientKit\ClientKit.h"
#include "osvr\RenderKit\RenderManager.h"
#include <math.h>
#include <stdio.h>
#include <vector>
#include <d3d11.h>
#include <wrl.h>
#include "osvr\RenderKit\GraphicsLibraryD3D11.h"

// Includes from our own directory
#include "pixelshader.h"
#include "vertexshader.h"

// Include headers for the graphics APIs we support
#if SUPPORT_D3D9
#include <d3d9.h>
#endif
#if SUPPORT_D3D11
#include <d3d11.h>
#endif
#if SUPPORT_OPENGL
#if UNITY_WIN || UNITY_LINUX
#include <GL/gl.h>
#else
#include <OpenGL/OpenGL.h>
#endif
#endif

// --------------------------------------------------------------------------
// Helper utilities

// Allow writing to the Unity debug console from inside DLL land.
extern "C" {
void(_stdcall *debugLog)(char *) = NULL;

__declspec(dllexport) void LinkDebug(void(_stdcall *d)(char *)) {
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

// --------------------------------------------------------------------------
// Static global variables we use for rendering.
static Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
static Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
static osvr::renderkit::RenderManager *render;
static int g_DeviceType = -1;
static float g_Time;

// --------------------------------------------------------------------------
// Internal function declarations
bool SetupRendering(osvr::renderkit::GraphicsLibrary library);
void RenderEyeTextures(
    void *userData //< Passed into AddRenderCallback
    ,
    osvr::renderkit::GraphicsLibrary library //< Graphics library context to use
    ,
    osvr::renderkit::OSVR_ViewportDescription
        viewport //< Viewport we're rendering into
    ,
    OSVR_PoseState pose //< OSVR ModelView matrix set by RenderManager
    ,
    osvr::renderkit::OSVR_ProjectionMatrix
        projection //< Projection matrix set by RenderManager
    ,
    OSVR_TimeValue deadline //< When the frame should be sent to the screen
    );

static void SetDefaultGraphicsState();

// --------------------------------------------------------------------------
// C API and internal function implementation

// RenderEvents
// If we ever decide to add more events, here's the place for it.
enum RenderEvents { kOsvrEventID_Render = 0 };
// GetEventID, returns the event code used when raising the render event for
// this plugin.
extern "C" int EXPORT_API GetEventID() { 
	DebugLog("[OSVR Rendering Plugin] GetEventID");
	return kOsvrEventID_Render; 
}

// Called from Unity to create a RenderManager, passing in a ClientContext
// Will passing a ClientContext like this from C# work?
extern "C" void EXPORT_API
CreateRenderManagerFromUnity(osvr::clientkit::ClientContext &clientContext) {
  // Get the display config file from the display path
  std::string displayConfigJsonFileName = clientContext.getStringParameter("/me/head");
  std::string pipelineConfigJsonFileName = ""; //@todo schema needs to be defined

  // Open Direct3D and set up the context for rendering to
  // an HMD.  Do this using the OSVR RenderManager interface,
  // which maps to the nVidia or other vendor direct mode
  // to reduce the latency.
  // NOTE: The pipelineConfig file needs to ask for a D3D
  // context, or this won't work.
  render = osvr::renderkit::createRenderManager(clientContext, displayConfigJsonFileName, 
	  pipelineConfigJsonFileName);
  if ((render == nullptr) || (!render->doingOkay())) {
	  DebugLog("[OSVR Rendering Plugin] Could not create RenderManager");
            
    return;
  }

  // Register callback to do Rendering
  render->AddRenderCallback("/", RenderEyeTextures);

  // Open the display and make sure this worked.
  osvr::renderkit::RenderManager::OpenResults ret = render->OpenDisplay();
  if (ret.status == osvr::renderkit::RenderManager::OpenStatus::FAILURE) {
	  DebugLog("[OSVR Rendering Plugin] Could not open display");
    return;
  }

  // Set up the rendering state we need.
  if (!SetupRendering(ret.library)) {
	  DebugLog("[OSVR Rendering Plugin] Could not setup rendering");
    return;
  }
}

// @todo Figure out what should be in here, this code is taken from
// RenderManagerD3DExample.cpp
bool SetupRendering(osvr::renderkit::GraphicsLibrary library) {
  ID3D11Device *device = library.D3D11->device;
  ID3D11DeviceContext *context = library.D3D11->context;
  ID3D11RenderTargetView *renderTargetView = library.D3D11->renderTargetView;

  // Setup vertex shader
  auto hr = device->CreateVertexShader(g_triangle_vs, sizeof(g_triangle_vs),
                                       nullptr, vertexShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Setup pixel shader
  hr = device->CreatePixelShader(g_triangle_ps, sizeof(g_triangle_ps), nullptr,
                                 pixelShader.GetAddressOf());
  if (FAILED(hr)) {
    return false;
  }

  // Set the input layout
  ID3D11InputLayout *vertexLayout;
  D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = device->CreateInputLayout(layout, _countof(layout), g_triangle_vs,
                                 sizeof(g_triangle_vs), &vertexLayout);
  if (SUCCEEDED(hr)) {
    context->IASetInputLayout(vertexLayout);
    vertexLayout->Release();
    vertexLayout = nullptr;
  }

  // Create vertex buffer
  ID3D11Buffer *vertexBuffer;
  struct XMFLOAT3 {
    float x;
    float y;
    float z;
  };
  struct SimpleVertex {
    XMFLOAT3 Pos;
  };
  SimpleVertex vertices[3];
  vertices[0].Pos.x = 0.0f;
  vertices[0].Pos.y = 0.5f;
  vertices[0].Pos.z = 0.5f;
  vertices[1].Pos.x = 0.5f;
  vertices[1].Pos.y = -0.5f;
  vertices[1].Pos.z = 0.5f;
  vertices[2].Pos.x = -0.5f;
  vertices[2].Pos.y = -0.5f;
  vertices[2].Pos.z = 0.5f;
  CD3D11_BUFFER_DESC bufferDesc(sizeof(SimpleVertex) * _countof(vertices),
                                D3D11_BIND_VERTEX_BUFFER);
  D3D11_SUBRESOURCE_DATA subResData = {vertices, 0, 0};
  hr = device->CreateBuffer(&bufferDesc, &subResData, &vertexBuffer);
  if (SUCCEEDED(hr)) {
    // Set vertex buffer
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    vertexBuffer->Release();
    vertexBuffer = nullptr;
  }
  // Set primitive topology
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  return true;
}

// Callback to draw eye textures
void RenderEyeTextures(
    void *userData //< Passed into AddRenderCallback
    ,
    osvr::renderkit::GraphicsLibrary library //< Graphics library context to use
    ,
    osvr::renderkit::OSVR_ViewportDescription
        viewport //< Viewport we're rendering into
    ,
    OSVR_PoseState pose //< OSVR ModelView matrix set by RenderManager
    ,
    osvr::renderkit::OSVR_ProjectionMatrix
        projection //< Projection matrix set by RenderManager
    ,
    OSVR_TimeValue deadline //< When the frame should be sent to the screen
    ) {
  ID3D11Device *device = library.D3D11->device;
  ID3D11DeviceContext *context = library.D3D11->context;
  ID3D11RenderTargetView *renderTargetView = library.D3D11->renderTargetView;

  // Draw a triangle using the simple shaders
  // context->VSSetShader(vertexShader.Get(), nullptr, 0);
  // context->PSSetShader(pixelShader.Get(), nullptr, 0);
  // context->Draw(3, 0);

  /// @todo Pass eye render textures to render manager?
}

// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the
// scripts.
extern "C" void EXPORT_API SetTimeFromUnity(float t) { g_Time = t; }

// --------------------------------------------------------------------------
// SetEyeTextureFromUnity, an example function we export which is called by one
// of the scripts.
// Should pass in something like eyeRenderTexture.GetNativeTexturePtr()
extern "C" int EXPORT_API SetEyeTextureFromUnity(void *texturePtr, int eye) {
  if (g_DeviceType == -1)
    return -1;

  //@todo pass the texturePtr to RenderManager

  // some sample D3D code below if we were to handle drawing rendertextures
  // here rather than pass to render manager
  /*
  #if SUPPORT_D3D9
  //@todo
  #endif
  #if SUPPORT_D3D11
  ID3D11Texture2D* eyeTexture = (ID3D11Texture2D*)texturePtr;

          if (eyeTexture == texturePtr) return 0;

          // Cache for future dirty checks.
          if (eye == 0)
          {
                  g_leftEyeTexture = eyeTexture;
                  if (g_D3D11RenderTargetViewLeft != NULL)
  g_D3D11RenderTargetViewLeft->Release();
          }
          else
          {
                  g_rightEyeTexture = eyeTexture;
                  if (g_D3D11RenderTargetViewRight != NULL)
  g_D3D11RenderTargetViewRight->Release();
          }

          D3D11_RENDER_TARGET_VIEW_DESC desc;
          desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
          desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
          desc.Texture2D.MipSlice = 0;

          HRESULT hr = g_D3D11Device->CreateRenderTargetView(eyeTexture, &desc,
  eye == 0 ? &g_D3D11RenderTargetViewLeft : &g_D3D11RenderTargetViewRight);
          DebugLog(hr == S_OK ? "Set D3D output RTV.\n" : "Error setting D3D11
  output SRV.\n");

  #endif
  #if SUPPORT_OPENGL
  //@todo
  #endif
  */
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
    DebugLog("Set D3D9 graphics device\n");
    g_DeviceType = deviceType;
    SetGraphicsDeviceD3D9((IDirect3DDevice9 *)device,
                          (GfxDeviceEventType)eventType);
  }
#endif

#if SUPPORT_D3D11
  // D3D11 device, remember device pointer and device type.
  // The pointer we get is ID3D11Device.
  if (deviceType == kGfxRendererD3D11) {
    DebugLog("Set D3D11 graphics device\n");
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
    DebugLog("Set OpenGL graphics device\n");
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

  // @todo Define more events that we might want to send
  // BeginFrame, EndFrame, DrawUILayer?
  // Call the Render loop
  switch (eventID) {
  case kOsvrEventID_Render:
  default:
    render->Render();
    break;
  }
}

// -------------------------------------------------------------------
//  Direct3D 9 setup/teardown code

#if SUPPORT_D3D9

static IDirect3DDevice9 *g_D3D9Device;

// A dynamic vertex buffer just to demonstrate how to handle D3D9 device resets.
static IDirect3DVertexBuffer9 *g_D3D9DynamicVB;

static void SetGraphicsDeviceD3D9(IDirect3DDevice9 *device,
                                  GfxDeviceEventType eventType) {
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
    DebugLog(
        "D3D11: HLSL shader compiler not found, will not render anything\n");
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
  // D3D11 case
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
  // OpenGL case
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

