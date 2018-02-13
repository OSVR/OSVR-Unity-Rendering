#include "OsvrRenderingPlugin.h"
#if SUPPORT_D3D11
#include "OsvrUnityRenderer.h"
#include "OsvrD3DRenderer.h"


OsvrD3DRenderer::OsvrD3DRenderer() : OsvrUnityRenderer()
{

}
void OsvrD3DRenderer::SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer)
{
	if (eye == 0) {
		if (buffer == 0)
		{
			s_leftEyeTexturePtr = texturePtr;
		}
		else
		{
			s_leftEyeTexturePtrBuffer2 = texturePtr;
		}
	}
	else {
		if (buffer == 0)
		{
			s_rightEyeTexturePtr = texturePtr;
		}
		else
		{
			s_rightEyeTexturePtrBuffer2 = texturePtr;
		}
	}
}

#if UNITY_WIN
void OsvrD3DRenderer::UpdateRenderInfo()
{
	if (s_render == nullptr)
	{
		return;
	}
	// Do a call to get the information we need to construct our
	// color and depth render-to-texture buffers.
	OSVR_RenderParams renderParams;
	osvrRenderManagerGetDefaultRenderParams(&renderParams);

	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerGetNumRenderInfo(
		s_render, renderParams, &numRenderInfo))) {
		DebugLog("[OSVR Rendering Plugin] Could not get context number of render infos.");
		ShutdownRenderManager();
		return;
	}

	s_renderInfo.clear();
	for (OSVR_RenderInfoCount i = 0; i < numRenderInfo; i++) {
		OSVR_RenderInfoD3D11 info;
		if ((OSVR_RETURN_SUCCESS != osvrRenderManagerGetRenderInfoD3D11(
			s_renderD3D, i, renderParams, &info))) {
			DebugLog("[OSVR Rendering Plugin] Could not get render info.");
			ShutdownRenderManager();
			return;
		}
		s_renderInfo.push_back(info);
	}
	if (numRenderInfo > 0)
	{
		s_lastRenderInfo = s_renderInfo;
	}
}
#endif // UNITY_WIN
OSVR_ReturnCode OsvrD3DRenderer::ConstructBuffersD3D11(int eye, int buffer, FrameInfoD3D11* fInfo)
{
	//DebugLog("[OSVR Rendering Plugin] ConstructBuffersD3D11");
	HRESULT hr;
	// The color buffer for this eye.  We need to put this into
	// a generic structure for the Present function, but we only need
	// to fill in the Direct3D portion.
	//  Note that this texture format must be RGBA and unsigned byte,
	// so that we can present it to Direct3D for DirectMode.
	ID3D11Texture2D *D3DTexture = reinterpret_cast<ID3D11Texture2D*>(GetEyeTexture(eye, buffer));
	unsigned width = static_cast<unsigned>(s_renderInfo[eye].viewport.width);
	unsigned height = static_cast<unsigned>(s_renderInfo[eye].viewport.height);

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

	// Create the render target view.
	ID3D11RenderTargetView *renderTargetView =
		nullptr; //< Pointer to our render target view
	hr = s_renderInfo[eye].library.device->CreateRenderTargetView(
		D3DTexture, &renderTargetViewDesc, &renderTargetView);
	if (FAILED(hr)) {
		DebugLog("[OSVR Rendering Plugin] Could not create render target for eye");
		return OSVR_RETURN_FAILURE;
	}

	// Push the filled-in RenderBuffer onto the stack.
	OSVR_RenderBufferD3D11 rbD3D;
	rbD3D.colorBuffer = D3DTexture;
	rbD3D.colorBufferView = renderTargetView;
	fInfo->renderBuffers.push_back(rbD3D);

	IDXGIKeyedMutex* keyedMutex = nullptr;
	hr = D3DTexture->QueryInterface(
		__uuidof(IDXGIKeyedMutex), (LPVOID*)&keyedMutex);
	if (FAILED(hr) || keyedMutex == nullptr) {
		DebugLog("[OSVR Rendering Plugin] Could not mutex pointer");
		return OSVR_RETURN_FAILURE;
	}
	fInfo->keyedMutex = keyedMutex;

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
	ID3D11Texture2D* depthStencilBuffer;
	hr = s_libraryD3D.device->CreateTexture2D(
		&textureDescription, NULL, &depthStencilBuffer);
	if (FAILED(hr)) {
		DebugLog("[OSVR Rendering Plugin] Could not create depth/stencil texture");
		return OSVR_RETURN_FAILURE;
	}
	fInfo->depthStencilTexture = depthStencilBuffer;

	// Create the depth/stencil view description
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription;
	memset(&depthStencilViewDescription, 0, sizeof(depthStencilViewDescription));
	depthStencilViewDescription.Format = textureDescription.Format;
	depthStencilViewDescription.ViewDimension =
		D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDescription.Texture2D.MipSlice = 0;

	ID3D11DepthStencilView* depthStencilView;
	hr = s_libraryD3D.device->CreateDepthStencilView(
		depthStencilBuffer,
		&depthStencilViewDescription,
		&depthStencilView);
	if (FAILED(hr)) {
		DebugLog("[OSVR Rendering Plugin] Could not create depth/stencil view");
		return OSVR_RETURN_FAILURE;
	}
	fInfo->depthStencilView = depthStencilView;

	return OSVR_RETURN_SUCCESS;
}

OSVR_ReturnCode OsvrD3DRenderer::CreateRenderBuffers()
{
	for (int i = 0; i < numBuffers; i++)
	{
		FrameInfoD3D11* f = new FrameInfoD3D11();
		f->renderBuffers.clear();
		for (int j = 0; j < numRenderInfo; j++)
		{
			ConstructBuffersD3D11(i, j, f);
		}
		frameInfo.push_back(f);
	}

	// Register our constructed buffers so that we can use them for
	// presentation.
	OSVR_RenderManagerRegisterBufferState registerBufferState;
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerStartRegisterRenderBuffers(
		&registerBufferState))) {
		DebugLog("[OSVR Rendering Plugin] Could not start registering render buffers");
		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}
	for (size_t i = 0; i < frameInfo.size(); i++) {
		for (int j = 0; j < numRenderInfo; j++)
		{
			if ((OSVR_RETURN_SUCCESS != osvrRenderManagerRegisterRenderBufferD3D11(
				registerBufferState, frameInfo[i]->renderBuffers[j]))) {
				DebugLog("[OSVR Rendering Plugin] Could not register render buffer ");
				ShutdownRenderManager();
				return OSVR_RETURN_FAILURE;
			}
		}

	}
	if ((OSVR_RETURN_SUCCESS != osvrRenderManagerFinishRegisterRenderBuffers(
		s_render, registerBufferState, false))) {
		DebugLog("[OSVR Rendering Plugin] Could not finish registering render buffers");
		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}
}

OSVR_ReturnCode OsvrD3DRenderer::CreateRenderManager(OSVR_ClientContext context)
{
	if (s_render != nullptr) {
		if (osvrRenderManagerGetDoingOkay(s_render)) {
			DebugLog("[OSVR Rendering Plugin] RenderManager already created "
				"and doing OK - will just return success without trying "
				"to re-initialize.");
			return OSVR_RETURN_SUCCESS;
		}

		DebugLog("[OSVR Rendering Plugin] RenderManager already created, "
			"but not doing OK. Will shut down before creating again.");
		ShutdownRenderManager();
		return OSVR_RETURN_SUCCESS;
	}
	if (s_clientContext != nullptr) {
		DebugLog(
			"[OSVR Rendering Plugin] Client context already set! Replacing...");
	}
	s_clientContext = context;

	if (!s_deviceType) {
		// @todo pass the platform from Unity
		// This is a patch to workaround a bug in Unity where the renderer type
		// is not being set on Windows x86 builds. Until the OpenGL path is
		// working, it's safe to assume we're using D3D11, but we'd rather get
		// the platform from Unity than assume it's D3D11.

		s_deviceType = kUnityGfxRendererD3D11;
	}

	bool setLibraryFromOpenDisplayReturn = false;
	/// @todo We should always have a legit value in
	/// s_deviceType.getDeviceTypeEnum() at this point, right?
	switch (s_deviceType.getDeviceTypeEnum()) {

#if SUPPORT_D3D11
	case OSVRSupportedRenderers::D3D11:
		if (OSVR_RETURN_SUCCESS !=
			osvrCreateRenderManagerD3D11(context, "Direct3D11", s_libraryD3D,
			&s_render, &s_renderD3D)) {
			DebugLog("[OSVR Rendering Plugin] Could not create RenderManagerD3D");
			return OSVR_RETURN_FAILURE;
		}
		break;
#endif // SUPPORT_D3D11
	}

	if (s_render == nullptr) {
		DebugLog("[OSVR Rendering Plugin] Could not create RenderManagerD3D");
		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}

	// Open the display and make sure this worked.
	OSVR_OpenResultsD3D11 openResults;
	if ((OSVR_RETURN_SUCCESS !=
		osvrRenderManagerOpenDisplayD3D11(s_renderD3D, &openResults)) ||
		(openResults.status == OSVR_OPEN_STATUS_FAILURE)) {
		//DebugLog("[OSVR Rendering Plugin] Could not open display");
		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}
	if (openResults.library.device == nullptr) {
		DebugLog("[OSVR Rendering Plugin] Could not get device when opening display");

		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}
	if (openResults.library.context == nullptr) {
		DebugLog("[OSVR Rendering Plugin] Could not get context when opening display");

		ShutdownRenderManager();
		return OSVR_RETURN_FAILURE;
	}

	// create a new set of RenderParams for passing to GetRenderInfo()
	osvrRenderManagerGetDefaultRenderParams(&s_renderParams);

	UpdateRenderInfo();

	DebugLog("[OSVR Rendering Plugin] Created RenderManager Successfully");
	return OSVR_RETURN_SUCCESS;
}

OSVR_Pose3 OsvrD3DRenderer::GetEyePose(std::uint8_t eye)
{
	OSVR_Pose3 pose;
	osvrPose3SetIdentity(&pose);
	if (numRenderInfo > 0 && eye <= numRenderInfo - 1 && s_render != nullptr) {
		pose = s_lastRenderInfo[eye].pose;
		lastGoodPose = pose;
	}
	else {
		std::string errorLog = "[OSVR Rendering Plugin] Error in GetEyePose, "
			"returning default values. Eye = " +
			std::to_string(int(eye));
		DebugLog(errorLog.c_str());
		pose = lastGoodPose;
	}
	return pose;
}

OSVR_ProjectionMatrix OsvrD3DRenderer::GetProjectionMatrix(std::uint8_t eye)
{
	OSVR_ProjectionMatrix pm;
	if (numRenderInfo > 0 && eye <= numRenderInfo - 1) {
		pm = s_lastRenderInfo[eye].projection;
		lastGoodProjMatrix = pm;
	}
	else {
		std::string errorLog = "[OSVR Rendering Plugin] Error in "
			"GetProjectionMatrix, returning default values. "
			"Eye = " +
			std::to_string(int(eye));
		DebugLog(errorLog.c_str());
		pm = lastGoodProjMatrix;
	}
	return pm;
}

OSVR_ViewportDescription OsvrD3DRenderer::GetViewport(std::uint8_t eye)
{
	OSVR_ViewportDescription viewportDescription;
	if (numRenderInfo > 0 && eye <= numRenderInfo - 1) {
		viewportDescription = s_lastRenderInfo[eye].viewport;

		// cache the viewport width and height
		// patches issue where sometimes empty viewport is returned
		//@todo fix the real cause of why this method bugs out occasionally on
		//some machines, more often on others
		if (viewportWidth == 0 && s_lastRenderInfo[eye].viewport.width != 0) {
			viewportWidth = s_lastRenderInfo[eye].viewport.width;
		}
		if (viewportHeight == 0 && s_lastRenderInfo[eye].viewport.height != 0) {
			viewportHeight = s_lastRenderInfo[eye].viewport.height;
		}
		lastGoodViewportDescription = viewportDescription;
	}
	else {
		// we shouldn't be here unless we hit a bug, in which case, we avoid
		// error by returning cached viewport values
		std::string errorLog = "[OSVR Rendering Plugin] Error in GetViewport, "
			"returning cached values. Eye = " +
			std::to_string(int(eye));
		DebugLog(errorLog.c_str());
		viewportDescription.left = 0;
		viewportDescription.lower = 0;
		viewportDescription.width = viewportWidth;
		viewportDescription.height = viewportHeight;
		lastGoodViewportDescription = viewportDescription;
	}
	return viewportDescription;
}

void* OsvrD3DRenderer::GetEyeTexture(int eye, int buffer)
{
	if (buffer == 0)
	{
		return (eye == 0 ? s_leftEyeTexturePtr
			: s_rightEyeTexturePtr);
	}
	else
	{
		return (eye == 0 ? s_leftEyeTexturePtrBuffer2
			: s_rightEyeTexturePtrBuffer2);
	}
}

void OsvrD3DRenderer::OnRenderEvent()
{
	int frame = iterations % numBuffers;

	const auto n = static_cast<int>(numRenderInfo);
	// Render into each buffer using the specified information.
	for (int i = 0; i < n; ++i) {

		auto context = s_renderInfo[i].library.context;
		// Set up to render to the textures for this eye
		context->OMSetRenderTargets(1, &frameInfo[frame]->renderBuffers[i].colorBufferView, NULL);

		// copy the updated RenderTexture from Unity to RenderManager colorBuffer
		frameInfo[frame]->renderBuffers[i].colorBuffer = reinterpret_cast<ID3D11Texture2D *>(GetEyeTexture(i, frame));
	}
	// Send the rendered results to the screen
	OSVR_RenderManagerPresentState presentState;
	if ((OSVR_RETURN_SUCCESS !=
		osvrRenderManagerStartPresentRenderBuffers(&presentState))) {
		DebugLog("[OSVR Rendering Plugin] Could not start presenting render buffers.");
		ShutdownRenderManager();
	}
	// create normalized cropping viewports for side-by-side rendering to a single render target
	std::vector<OSVR_ViewportDescription> NVCPs;
	double fraction = 1.0 / s_renderInfo.size();
	for (size_t i = 0; i < s_renderInfo.size(); i++) {
		OSVR_ViewportDescription v;
		v.left = fraction * i;
		v.lower = 0.0;
		v.width = fraction;
		v.height = 1;
		NVCPs.push_back(v);
	}
	OSVR_ViewportDescription fullView;
	fullView.left = fullView.lower = 0;
	fullView.width = fullView.height = 1;
	for (size_t i = 0; i < numRenderInfo; i++) {
		if ((OSVR_RETURN_SUCCESS !=
			osvrRenderManagerPresentRenderBufferD3D11(
			presentState, frameInfo[frame]->renderBuffers[i], s_renderInfo[i],
			fullView))) {
			DebugLog("[OSVR Rendering Plugin] Could not present render buffer ");
			ShutdownRenderManager();
		}
	}

	if ((OSVR_RETURN_SUCCESS !=
		osvrRenderManagerFinishPresentRenderBuffers(
		s_render, presentState, s_renderParams, true))) {
		DebugLog("[OSVR Rendering Plugin] Could not finish presenting render buffers");
		ShutdownRenderManager();
	}

	iterations++;
}

void OsvrD3DRenderer::OnInitializeGraphicsDeviceEvent()
{
	IUnityGraphicsD3D11 *d3d11 =
		s_UnityInterfaces->Get<IUnityGraphicsD3D11>();

	// Put the device and context into a structure to let RenderManager
	// know to use this one rather than creating its own.
	s_libraryD3D.device = d3d11->GetDevice();
	ID3D11DeviceContext *ctx = nullptr;
	s_libraryD3D.device->GetImmediateContext(&ctx);
	s_libraryD3D.context = ctx;
}


void OsvrD3DRenderer::SetFarClipDistance(double distance)
{
	s_farClipDistance = distance;

}

void OsvrD3DRenderer::SetIPD(double ipdMeters)
{
	s_ipd = ipdMeters;

}

void OsvrD3DRenderer::SetNearClipDistance(double distance)
{
	s_nearClipDistance = distance;
}
void OsvrD3DRenderer::ShutdownRenderManager()
{
	DebugLog("[OSVR Rendering Plugin] Shutting down RenderManagerD3D.");
	if (s_render != nullptr) {
		osvrDestroyRenderManager(s_render);
		s_render = nullptr;
		s_leftEyeTexturePtr = nullptr;
		s_leftEyeTexturePtrBuffer2 = nullptr;
		s_rightEyeTexturePtr = nullptr;
		s_rightEyeTexturePtrBuffer2 = nullptr;
		frameInfo.clear();
	}
	s_clientContext = nullptr;

}
#endif



