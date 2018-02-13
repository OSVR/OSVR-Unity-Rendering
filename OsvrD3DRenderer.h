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

// D3D- specific includes
// Include headers for the graphics APIs we support
#if SUPPORT_D3D11
#include <d3d11.h>

#include "Unity/IUnityGraphicsD3D11.h"
#include <osvr/RenderKit/GraphicsLibraryD3D11.h>
#include <osvr/RenderKit/RenderManagerD3D11C.h>


#include "OsvrUnityRenderer.h"

//D3D Unity Rendering Plugin implementation
class OsvrD3DRenderer : public OsvrUnityRenderer {
public:
	OsvrD3DRenderer();
	~OsvrD3DRenderer();
	virtual OSVR_ReturnCode CreateRenderBuffers();
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
	virtual void* GetEyeTexture(int eye, int buffer);
	virtual void SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer);
	OSVR_GraphicsLibraryD3D11 s_libraryD3D;

private:
	struct FrameInfoD3D11 {
		// Set up the vector of textures to render to and any framebuffer
		// we need to group them.
		std::vector<OSVR_RenderBufferD3D11> renderBuffers;
		ID3D11Texture2D* depthStencilTexture;
		ID3D11DepthStencilView* depthStencilView;
		IDXGIKeyedMutex* keyedMutex;
		FrameInfoD3D11() : renderBuffers(2)
		{
		}

	};
	std::vector<FrameInfoD3D11*> frameInfo;
	OSVR_RenderParams s_renderParams;
	OSVR_RenderManager s_render = nullptr;
	OSVR_RenderManagerD3D11 s_renderD3D = nullptr;
	OSVR_ClientContext s_clientContext = nullptr;
	std::vector<OSVR_RenderInfoD3D11> s_renderInfo;
	std::vector<OSVR_RenderInfoD3D11> s_lastRenderInfo;
	//OSVR_GraphicsLibraryD3D11 s_libraryD3D;
	OSVR_RenderInfoCount numRenderInfo;
	OSVR_ProjectionMatrix lastGoodProjMatrix;
	OSVR_Pose3 lastGoodPose;
	OSVR_ViewportDescription lastGoodViewportDescription;
	D3D11_TEXTURE2D_DESC s_textureDesc;
	void *s_leftEyeTexturePtr = nullptr;
	void *s_rightEyeTexturePtr = nullptr;
	void *s_leftEyeTexturePtrBuffer2 = nullptr;
	void *s_rightEyeTexturePtrBuffer2 = nullptr;

	OSVR_ReturnCode ConstructBuffersD3D11(int eye, int buffer, FrameInfoD3D11* fInfo);

};
#endif // SUPPORT_D3D11
