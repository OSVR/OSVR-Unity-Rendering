#include "OsvrAndroidRenderer.h"


OsvrAndroidRenderer::OsvrAndroidRenderer() : OsvrUnityRenderer()
{
		
}

OSVR_ReturnCode OsvrAndroidRenderer::ConstructRenderBuffers()
	{
		if (!setupRenderTextures(gRenderManager)) {
			return OSVR_RETURN_FAILURE;
		}
		else
			return OSVR_RETURN_SUCCESS;
	}

OSVR_ReturnCode OsvrAndroidRenderer::CreateRenderManager(OSVR_ClientContext context)
	{
		gClientContext = context;
		if (setupOSVR()) {
			if (setupGraphics(gWidth, gHeight)) {
				if (setupRenderManager()) {
					return OSVR_RETURN_SUCCESS;
				}
				else
					return 3;
			}
			else
				return 2;
		}
		else
			return 1;

		return OSVR_RETURN_SUCCESS;
}

void OsvrAndroidRenderer::SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer)
{
#if UNITY_ANDROID
	if (eye == 0) {
		if (buffer == 0)
		{
			gLeftEyeTextureID = (GLuint)texturePtr;
		}
		else
		{
			gLeftEyeTextureIDBuffer2 = (GLuint)texturePtr;
		}
	}
	else {
		if (buffer == 0)
		{
			gRightEyeTextureID = (GLuint)texturePtr;
		}
		else
		{
			gRightEyeTextureIDBuffer2 = (GLuint)texturePtr;
		}
	}
#endif
}

#if UNITY_ANDROID
inline osvr::renderkit::OSVR_ProjectionMatrix
ConvertProjectionMatrix(::OSVR_ProjectionMatrix matrix) {
	osvr::renderkit::OSVR_ProjectionMatrix ret = { 0 };
	ret.bottom = matrix.bottom;
	ret.top = matrix.top;
	ret.left = matrix.left;
	ret.right = matrix.right;
	ret.nearClip = matrix.nearClip;
	ret.farClip = matrix.farClip;
	return ret;
}

static void checkReturnCode(OSVR_ReturnCode returnCode, const char *msg) {
	if (returnCode != OSVR_RETURN_SUCCESS) {
		// LOGI("[OSVR] OSVR method returned a failure: %s", msg);
		throw std::runtime_error(msg);
	}
}
// RAII wrapper around the RenderManager collection APIs for OpenGL
class RenderInfoCollectionOpenGL {
private:
	OSVR_RenderManager mRenderManager = nullptr;
	OSVR_RenderInfoCollection mRenderInfoCollection = nullptr;
	OSVR_RenderParams mRenderParams = { 0 };

public:
	RenderInfoCollectionOpenGL(OSVR_RenderManager renderManager,
		OSVR_RenderParams renderParams)
		: mRenderManager(renderManager), mRenderParams(renderParams) {
		OSVR_ReturnCode rc;
		rc = osvrRenderManagerGetRenderInfoCollection(
			mRenderManager, mRenderParams, &mRenderInfoCollection);
		checkReturnCode(
			rc, "osvrRenderManagerGetRenderInfoCollection call failed.");
	}

	OSVR_RenderInfoCount getNumRenderInfo() {
		OSVR_RenderInfoCount ret;
		OSVR_ReturnCode rc;
		rc = osvrRenderManagerGetNumRenderInfoInCollection(
			mRenderInfoCollection, &ret);
		checkReturnCode(
			rc, "osvrRenderManagerGetNumRenderInfoInCollection call failed.");
		return ret;
	}

	OSVR_RenderInfoOpenGL getRenderInfo(OSVR_RenderInfoCount index) {
		if (index < 0 || index >= getNumRenderInfo()) {
			const static char *err = "getRenderInfo called with invalid index";
			// LOGE(err);
			throw std::runtime_error(err);
		}
		OSVR_RenderInfoOpenGL ret;
		OSVR_ReturnCode rc;
		rc = osvrRenderManagerGetRenderInfoFromCollectionOpenGL(
			mRenderInfoCollection, index, &ret);
		checkReturnCode(
			rc,
			"osvrRenderManagerGetRenderInfoFromCollectionOpenGL call failed.");
		return ret;
	}

	~RenderInfoCollectionOpenGL() {
		if (mRenderInfoCollection) {
			osvrRenderManagerReleaseRenderInfoCollection(mRenderInfoCollection);
		}
	}
};

static void checkGlError(const char *op) {
	std::stringstream ss;
	for (GLint error = glGetError(); error; error = glGetError()) {
		// gluErrorString without glu
		std::string errorString;
		switch (error) {
		case GL_NO_ERROR:
			errorString = "GL_NO_ERROR";
			break;
		case GL_INVALID_ENUM:
			errorString = "GL_INVALID_ENUM";
			break;
		case GL_INVALID_VALUE:
			errorString = "GL_INVALID_VALUE";
			break;
		case GL_INVALID_OPERATION:
			errorString = "GL_INVALID_OPERATION";
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			errorString = "GL_INVALID_FRAMEBUFFER_OPERATION";
			break;
		case GL_OUT_OF_MEMORY:
			errorString = "GL_OUT_OF_MEMORY";
			break;
		default:
			errorString = "(unknown error)";
			break;
		}
		// LOGI("after %s() glError (%s)\n", op, errorString.c_str());
	}
}

class PassThroughOpenGLContextImpl {
	OSVR_OpenGLToolkitFunctions toolkit;
	int mWidth;
	int mHeight;

	static void createImpl(void *data) {}
	static void destroyImpl(void *data) {
		delete ((PassThroughOpenGLContextImpl *)data);
	}
	static OSVR_CBool addOpenGLContextImpl(void *data,
		const OSVR_OpenGLContextParams *p) {
		return ((PassThroughOpenGLContextImpl *)data)->addOpenGLContext(p);
	}
	static OSVR_CBool removeOpenGLContextsImpl(void *data) {
		return ((PassThroughOpenGLContextImpl *)data)->removeOpenGLContexts();
	}
	static OSVR_CBool makeCurrentImpl(void *data, size_t display) {
		return ((PassThroughOpenGLContextImpl *)data)->makeCurrent(display);
	}
	static OSVR_CBool swapBuffersImpl(void *data, size_t display) {
		return ((PassThroughOpenGLContextImpl *)data)->swapBuffers(display);
	}
	static OSVR_CBool setVerticalSyncImpl(void *data, OSVR_CBool verticalSync) {
		return ((PassThroughOpenGLContextImpl *)data)
			->setVerticalSync(verticalSync);
	}
	static OSVR_CBool handleEventsImpl(void *data) {
		return ((PassThroughOpenGLContextImpl *)data)->handleEvents();
	}
	static OSVR_CBool getDisplayFrameBufferImpl(void *data, size_t display,
		GLuint *displayFrameBufferOut) {
		return ((PassThroughOpenGLContextImpl *)data)
			->getDisplayFrameBuffer(display, displayFrameBufferOut);
	}
	static OSVR_CBool getDisplaySizeOverrideImpl(void *data, size_t display,
		int *width, int *height) {
		return ((PassThroughOpenGLContextImpl *)data)
			->getDisplaySizeOverride(display, width, height);
	}

public:
	PassThroughOpenGLContextImpl() {
		memset(&toolkit, 0, sizeof(toolkit));
		toolkit.size = sizeof(toolkit);
		toolkit.data = this;

		toolkit.create = createImpl;
		toolkit.destroy = destroyImpl;
		toolkit.addOpenGLContext = addOpenGLContextImpl;
		toolkit.removeOpenGLContexts = removeOpenGLContextsImpl;
		toolkit.makeCurrent = makeCurrentImpl;
		toolkit.swapBuffers = swapBuffersImpl;
		toolkit.setVerticalSync = setVerticalSyncImpl;
		toolkit.handleEvents = handleEventsImpl;
		toolkit.getDisplaySizeOverride = getDisplaySizeOverrideImpl;
		toolkit.getDisplayFrameBuffer = getDisplayFrameBufferImpl;
	}

	~PassThroughOpenGLContextImpl() {}

	const OSVR_OpenGLToolkitFunctions *getToolkit() const { return &toolkit; }

	bool addOpenGLContext(const OSVR_OpenGLContextParams *p) { return true; }

	bool removeOpenGLContexts() { return true; }

	bool makeCurrent(size_t display) { return true; }

	bool swapBuffers(size_t display) { return true; }

	bool setVerticalSync(bool verticalSync) { return true; }

	bool handleEvents() { return true; }
	bool getDisplayFrameBuffer(size_t display, GLuint *displayFrameBufferOut) {
		*displayFrameBufferOut = gFrameBuffer;
		return true;
	}

	bool getDisplaySizeOverride(size_t display, int *width, int *height) {
		*width = gWidth;
		*height = gHeight;
		return false;
	}
};

static GLuint loadShader(GLenum shaderType, const char *pSource) {
	GLuint shader = glCreateShader(shaderType);
	if (shader) {
		glShaderSource(shader, 1, &pSource, NULL);
		glCompileShader(shader);
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			GLint infoLen = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
			if (infoLen) {
				char *buf = (char *)malloc(infoLen);
				if (buf) {
					glGetShaderInfoLog(shader, infoLen, NULL, buf);
					// LOGE("Could not compile shader %d:\n%s\n",
					// shaderType, buf);
					free(buf);
				}
				glDeleteShader(shader);
				shader = 0;
			}
		}
	}
	return shader;
}

static GLuint createProgram(const char *pVertexSource,
	const char *pFragmentSource) {
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
	if (!vertexShader) {
		return 0;
	}

	GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
	if (!pixelShader) {
		return 0;
	}

	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vertexShader);
		checkGlError("glAttachShader");

		glAttachShader(program, pixelShader);
		checkGlError("glAttachShader");

		glBindAttribLocation(program, 0, "vPosition");
		glBindAttribLocation(program, 1, "vColor");
		glBindAttribLocation(program, 2, "vTexCoordinate");

		glLinkProgram(program);
		GLint linkStatus = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if (linkStatus != GL_TRUE) {
			GLint bufLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
			if (bufLength) {
				char *buf = (char *)malloc(bufLength);
				if (buf) {
					glGetProgramInfoLog(program, bufLength, NULL, buf);
					// LOGE("Could not link program:\n%s\n", buf);
					free(buf);
				}
			}
			glDeleteProgram(program);
			program = 0;
		}
	}
	return program;
}

static GLuint createTexture(GLuint width, GLuint height) {
	GLuint ret;
	glGenTextures(1, &ret);
	checkGlError("glGenTextures");

	glBindTexture(GL_TEXTURE_2D, ret);
	checkGlError("glBindTexture");

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	//    // DEBUG CODE - should be passing null here, but then texture is
	//    always black.
	GLubyte *dummyBuffer = new GLubyte[width * height * 4];
	for (GLuint i = 0; i < width * height * 4; i++) {
		dummyBuffer[i] = (i % 4 ? 100 : 255);
	}

	// This dummy texture successfully makes it into the texture and renders,
	// but subsequent
	// calls to glTexSubImage2D don't appear to do anything.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, dummyBuffer);
	checkGlError("glTexImage2D");
	delete[] dummyBuffer;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	checkGlError("glTexParameteri");

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	checkGlError("glTexParameteri");
	return ret;
}

static void updateTexture(GLuint width, GLuint height, GLubyte *data) {

	glBindTexture(GL_TEXTURE_2D, gTextureID);
	checkGlError("glBindTexture");

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	// @todo use glTexSubImage2D to be faster here, but add check to make sure
	// height/width are the same.
	// glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
	// GL_UNSIGNED_BYTE, data);
	// checkGlError("glTexSubImage2D");
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, data);
	checkGlError("glTexImage2D");
}

static void imagingCallback(void *userdata, const OSVR_TimeValue *timestamp,
	const OSVR_ImagingReport *report) {

	OSVR_ClientContext *ctx = (OSVR_ClientContext *)userdata;

	gReportNumber++;
	GLuint width = report->state.metadata.width;
	GLuint height = report->state.metadata.height;
	gLastFrameWidth = width;
	gLastFrameHeight = height;
	GLuint size = width * height * 4;

	gLastFrame = report->state.data;
}
#if SUPPORT_OPENGL
inline GLuint GetEyeTextureOpenGL(int eye, int buffer = 0) {
	if (buffer == 0)
	{
		return (eye == 0) ? gLeftEyeTextureID : gRightEyeTextureID;

	}
	else
	{
		return (eye == 0) ? gLeftEyeTextureIDBuffer2 : gRightEyeTextureIDBuffer2;

	}
}
#endif
#endif

bool OsvrAndroidRenderer::setupRenderTextures(OSVR_RenderManager renderManager) {
#if UNITY_ANDROID
	try {
		OSVR_ReturnCode rc;
		rc = osvrRenderManagerGetDefaultRenderParams(&gRenderParams);
		checkReturnCode(rc,
			"osvrRenderManagerGetDefaultRenderParams call failed.");

		gRenderParams.farClipDistanceMeters = 1000000.0f;
		gRenderParams.nearClipDistanceMeters = 0.0000001f;
		RenderInfoCollectionOpenGL renderInfo(renderManager, gRenderParams);

		OSVR_RenderManagerRegisterBufferState state;
		rc = osvrRenderManagerStartRegisterRenderBuffers(&state);
		checkReturnCode(
			rc, "osvrRenderManagerStartRegisterRenderBuffers call failed.");

		for (int j = 0; j < numBuffers; j++){
			FrameInfoOpenGL* f = new FrameInfoOpenGL();
			f->renderBuffers.clear();
			for (OSVR_RenderInfoCount i = 0; i < renderInfo.getNumRenderInfo();
				i++) {
				OSVR_RenderInfoOpenGL currentRenderInfo =
					renderInfo.getRenderInfo(i);

				// Determine the appropriate size for the frame buffer to be used
				// for
				// all eyes when placed horizontally size by side.
				int width = static_cast<int>(currentRenderInfo.viewport.width);
				int height = static_cast<int>(currentRenderInfo.viewport.height);

				GLuint frameBufferName = 0;
				glGenFramebuffers(1, &frameBufferName);
				glBindFramebuffer(GL_FRAMEBUFFER, frameBufferName);

				GLuint renderBufferName = 0;
				glGenRenderbuffers(1, &renderBufferName);

				GLuint colorBufferName = GetEyeTextureOpenGL(i, j);
				rc = osvrRenderManagerCreateColorBufferOpenGL(
					width, height, GL_RGBA, &colorBufferName);
				checkReturnCode(
					rc, "osvrRenderManagerCreateColorBufferOpenGL call failed.");

				// bind it to our framebuffer
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, colorBufferName, 0);

				// The depth buffer
				GLuint depthBuffer;
				rc = osvrRenderManagerCreateDepthBufferOpenGL(width, height,
					&depthBuffer);
				checkReturnCode(
					rc, "osvrRenderManagerCreateDepthBufferOpenGL call failed.");

				glGenRenderbuffers(1, &depthBuffer);
				glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width,
					height);

				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER, depthBuffer);

				glBindRenderbuffer(GL_RENDERBUFFER, renderBufferName);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width,
					height);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_2D, colorBufferName, 0);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER, renderBufferName);

				// unbind the framebuffer
				glBindTexture(GL_TEXTURE_2D, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
				glBindFramebuffer(GL_FRAMEBUFFER, gFrameBuffer);

				OSVR_RenderBufferOpenGL buffer = { 0 };
				buffer.colorBufferName = colorBufferName;
				buffer.depthStencilBufferName = depthBuffer;
				rc = osvrRenderManagerRegisterRenderBufferOpenGL(state, buffer);
				checkReturnCode(
					rc, "osvrRenderManagerRegisterRenderBufferOpenGL call failed.");

				OSVR_RenderTargetInfoOpenGL renderTarget = { 0 };
				renderTarget.frameBufferName = frameBufferName;
				renderTarget.renderBufferName = renderBufferName;
				renderTarget.colorBufferName = colorBufferName;
				renderTarget.depthBufferName = depthBuffer;
				f->renderBuffers.push_back(renderTarget);
				//gRenderTargets.push_back(renderTarget);
			}
			frameInfoOGL.push_back(f);
		}
		rc = osvrRenderManagerFinishRegisterRenderBuffers(renderManager, state,
			true);
		checkReturnCode(
			rc, "osvrRenderManagerFinishRegisterRenderBuffers call failed.");
	}
	catch (...) {
		// LOGE("Error durring render target creation.");
		return false;
	}
	return true;
#endif
	return true;
}

bool OsvrAndroidRenderer::setupOSVR() {
		if (gOSVRInitialized) {
			return true;
		}
		OSVR_ReturnCode rc = 0;
		try {
			// On Android, the current working directory is added to the default
			// plugin search path.
			// it also helps the server find its configuration and display files.
			//            boost::filesystem::current_path("/data/data/com.osvr.android.gles2sample/files");
			//            auto workingDirectory = boost::filesystem::current_path();
			//            //LOGI("[OSVR] Current working directory: %s",
			//            workingDirectory.string().c_str());

			// auto-start the server
			osvrClientAttemptServerAutoStart();

			if (!gClientContext) {
				// LOGI("[OSVR] Creating ClientContext...");
				gClientContext =
					osvrClientInit("com.osvr.android.examples.OSVROpenGL", 0);
				if (!gClientContext) {
					// LOGI("[OSVR] could not create client context");
					return false;
				}

				// temporary workaround to DisplayConfig issue,
				// display sometimes fails waiting for the tree from the server.
				// LOGI("[OSVR] Calling update a few times...");
				for (int i = 0; i < 10000; i++) {
					rc = osvrClientUpdate(gClientContext);
					if (rc != OSVR_RETURN_SUCCESS) {
						// LOGI("[OSVR] Error while updating client context.");
						return false;
					}
				}

				rc = osvrClientCheckStatus(gClientContext);
				if (rc != OSVR_RETURN_SUCCESS) {
					// LOGI("[OSVR] Client context reported bad status.");
					return false;
				}
				else {
					// LOGI("[OSVR] Client context reported good status.");
				}

				//                if (OSVR_RETURN_SUCCESS !=
				//                    osvrClientGetInterface(gClientContext,
				//                    "/camera", &gCamera)) {
				//                    //LOGI("Error, could not get the camera
				//                    interface at /camera.");
				//                    return false;
				//                }
				//
				//                // Register the imaging callback.
				//                if (OSVR_RETURN_SUCCESS !=
				//                    osvrRegisterImagingCallback(gCamera,
				//                    &imagingCallback, &gClientContext)) {
				//                    //LOGI("Error, could not register image
				//                    callback.");
				//                    return false;
				//                }
			}

			gOSVRInitialized = true;
			return true;
		}
		catch (const std::runtime_error &ex) {
			// LOGI("[OSVR] OSVR initialization failed: %s", ex.what());
			return false;
		}
	}

	// Idempotent call to setup render manager
bool OsvrAndroidRenderer::setupRenderManager() {
#if UNITY_ANDROID
		if (!gOSVRInitialized || !gGraphicsInitializedOnce) {
			return false;
		}
		if (gRenderManagerInitialized) {
			return true;
		}
		try {
			PassThroughOpenGLContextImpl *glContextImpl =
				new PassThroughOpenGLContextImpl();
			gGraphicsLibrary.toolkit = glContextImpl->getToolkit();

			if (OSVR_RETURN_SUCCESS !=
				osvrCreateRenderManagerOpenGL(gClientContext, "OpenGL",
				gGraphicsLibrary, &gRenderManager,
				&gRenderManagerOGL)) {
				std::cerr << "Could not create the RenderManager" << std::endl;
				return false;
			}

			// Open the display and make sure this worked
			OSVR_OpenResultsOpenGL openResults;
			if (OSVR_RETURN_SUCCESS != osvrRenderManagerOpenDisplayOpenGL(
				gRenderManagerOGL, &openResults) ||
				(openResults.status == OSVR_OPEN_STATUS_FAILURE)) {
				std::cerr << "Could not open display" << std::endl;
				osvrDestroyRenderManager(gRenderManager);
				gRenderManager = gRenderManagerOGL = nullptr;
				return false;
			}

			gRenderManagerInitialized = true;
			return true;
		}
		catch (const std::runtime_error &ex) {
			// LOGI("[OSVR] RenderManager initialization failed: %s", ex.what());
			return false;
		}
	}
	static const GLfloat gTriangleColors[] = {
		// white
		1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,

		// green
		0.0f, 0.75f, 0.0f, 1.0f, 0.0f, 0.75f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 0.75f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,

		// blue
		0.0f, 0.0f, 0.75f, 1.0f, 0.0f, 0.0f, 0.75f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 0.75f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,

		// green/purple
		0.0f, 0.75f, 0.75f, 1.0f, 0.0f, 0.75f, 0.75f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		0.0f, 0.75f, 0.75f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,

		// red/green
		0.75f, 0.75f, 0.0f, 1.0f, 0.75f, 0.75f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
		0.75f, 0.75f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,

		// red/blue
		0.75f, 0.0f, 0.75f, 1.0f, 0.75f, 0.0f, 0.75f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
		0.75f, 0.0f, 0.75f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

	static const GLfloat gTriangleTexCoordinates[] = {
		// A cube face (letters are unique vertices)
		// A--B
		// |  |
		// D--C

		// As two triangles (clockwise)
		// A B D
		// B C D

		// white
		1.0f, 0.0f, // A
		1.0f, 1.0f, // B
		0.0f, 0.0f, // D
		1.0f, 1.0f, // B
		0.0f, 1.0f, // C
		0.0f, 0.0f, // D

		// green
		1.0f, 0.0f, // A
		1.0f, 1.0f, // B
		0.0f, 0.0f, // D
		1.0f, 1.0f, // B
		0.0f, 1.0f, // C
		0.0f, 0.0f, // D

		// blue
		1.0f, 1.0f, // A
		0.0f, 1.0f, // B
		1.0f, 0.0f, // D
		0.0f, 1.0f, // B
		0.0f, 0.0f, // C
		1.0f, 0.0f, // D

		// blue-green
		1.0f, 0.0f, // A
		1.0f, 1.0f, // B
		0.0f, 0.0f, // D
		1.0f, 1.0f, // B
		0.0f, 1.0f, // C
		0.0f, 0.0f, // D

		// yellow
		0.0f, 0.0f, // A
		1.0f, 0.0f, // B
		0.0f, 1.0f, // D
		1.0f, 0.0f, // B
		1.0f, 1.0f, // C
		0.0f, 1.0f, // D

		// purple/magenta
		1.0f, 1.0f, // A
		0.0f, 1.0f, // B
		1.0f, 0.0f, // D
		0.0f, 1.0f, // B
		0.0f, 0.0f, // C
		1.0f, 0.0f, // D
	};

	static const GLfloat gTriangleVertices[] = {
		// A cube face (letters are unique vertices)
		// A--B
		// |  |
		// D--C

		// As two triangles (clockwise)
		// A B D
		// B C D

		// glNormal3f(0.0, 0.0, -1.0);
		1.0f, 1.0f, -1.0f,   // A
		1.0f, -1.0f, -1.0f,  // B
		-1.0f, 1.0f, -1.0f,  // D
		1.0f, -1.0f, -1.0f,  // B
		-1.0f, -1.0f, -1.0f, // C
		-1.0f, 1.0f, -1.0f,  // D

		// glNormal3f(0.0, 0.0, 1.0);
		-1.0f, 1.0f, 1.0f,  // A
		-1.0f, -1.0f, 1.0f, // B
		1.0f, 1.0f, 1.0f,   // D
		-1.0f, -1.0f, 1.0f, // B
		1.0f, -1.0f, 1.0f,  // C
		1.0f, 1.0f, 1.0f,   // D

		//        glNormal3f(0.0, -1.0, 0.0);
		1.0f, -1.0f, 1.0f,   // A
		-1.0f, -1.0f, 1.0f,  // B
		1.0f, -1.0f, -1.0f,  // D
		-1.0f, -1.0f, 1.0f,  // B
		-1.0f, -1.0f, -1.0f, // C
		1.0f, -1.0f, -1.0f,  // D

		//        glNormal3f(0.0, 1.0, 0.0);
		1.0f, 1.0f, 1.0f,   // A
		1.0f, 1.0f, -1.0f,  // B
		-1.0f, 1.0f, 1.0f,  // D
		1.0f, 1.0f, -1.0f,  // B
		-1.0f, 1.0f, -1.0f, // C
		-1.0f, 1.0f, 1.0f,  // D

		//        glNormal3f(-1.0, 0.0, 0.0);
		-1.0f, 1.0f, 1.0f,   // A
		-1.0f, 1.0f, -1.0f,  // B
		-1.0f, -1.0f, 1.0f,  // D
		-1.0f, 1.0f, -1.0f,  // B
		-1.0f, -1.0f, -1.0f, // C
		-1.0f, -1.0f, 1.0f,  // D

		//        glNormal3f(1.0, 0.0, 0.0);
		1.0f, -1.0f, 1.0f,  // A
		1.0f, -1.0f, -1.0f, // B
		1.0f, 1.0f, 1.0f,   // D
		1.0f, -1.0f, -1.0f, // B
		1.0f, 1.0f, -1.0f,  // C
		1.0f, 1.0f, 1.0f    // D
#endif
		return true;
};

bool OsvrAndroidRenderer::setupGraphics(int width, int height) {
#if UNITY_ANDROID
		// initializeGLES2Ext();
		GLint frameBuffer;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &frameBuffer);
		gFrameBuffer = (GLuint)frameBuffer;
		// LOGI("Window GL_FRAMEBUFFER_BINDING: %d", gFrameBuffer);

		// LOGI("setupGraphics(%d, %d)", width, height);
		// gWidth = width;
		// gHeight = height;

		// bool osvrSetupSuccess = setupOSVR();

		gProgram = createProgram(gVertexShader, gFragmentShader);
		if (!gProgram) {
			// LOGE("Could not create program.");
			osvrJniWrapperClass = jniEnvironment->FindClass(
				"org/osvr/osvrunityjni/OsvrJNIWrapper"); // try to find the class
			if (osvrJniWrapperClass == nullptr) {
				return false;
			}
			else { // if class found, continue

				jmethodID androidDebugLogMethodID = jniEnvironment->GetStaticMethodID(
					osvrJniWrapperClass, "logMsg",
					"(Ljava/lang/String;)V"); // find method
				std::string stringy =
					"[OSVR-Unity-Android]  Could not create program.";
				jstring jstr2 = jniEnvironment->NewStringUTF(stringy.c_str());
				jniEnvironment->CallStaticVoidMethod(osvrJniWrapperClass, androidDebugLogMethodID,
					jstr2);
			}
			return false;
		}
		gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
		checkGlError("glGetAttribLocation");
		// LOGI("glGetAttribLocation(\"vPosition\") = %d\n", gvPositionHandle);

		gvColorHandle = glGetAttribLocation(gProgram, "vColor");
		checkGlError("glGetAttribLocation");
		// LOGI("glGetAttribLocation(\"vColor\") = %d\n", gvColorHandle);

		gvTexCoordinateHandle = glGetAttribLocation(gProgram, "vTexCoordinate");
		checkGlError("glGetAttribLocation");
		// LOGI("glGetAttribLocation(\"vTexCoordinate\") = %d\n",
		// gvTexCoordinateHandle);

		gvProjectionUniformId = glGetUniformLocation(gProgram, "projection");
		gvViewUniformId = glGetUniformLocation(gProgram, "view");
		gvModelUniformId = glGetUniformLocation(gProgram, "model");
		guTextureUniformId = glGetUniformLocation(gProgram, "uTexture");

		glViewport(0, 0, width, height);
		checkGlError("glViewport");

		glDisable(GL_CULL_FACE);

		// @todo can we resize the texture after it has been created?
		// if not, we may have to delete the dummy one and create a new one after
		// the first imaging report.
		// LOGI("Creating texture... here we go!");

		gTextureID = createTexture(width, height);

		// return osvrSetupSuccess;
		gGraphicsInitializedOnce = true;
#endif
		return true;
	}

OSVR_Pose3 OsvrAndroidRenderer::GetEyePose(std::uint8_t eye)
{
	OSVR_RenderInfoOpenGL currentRenderInfo;
#if UNITY_ANDROID
	OSVR_RenderParams renderParams;
	OSVR_ReturnCode rc = osvrRenderManagerGetDefaultRenderParams(&renderParams);
	checkReturnCode(rc, "osvrRenderManagerGetDefaultRenderParams call failed.");
	RenderInfoCollectionOpenGL renderInfoCollection(gRenderManager,
		renderParams);
	currentRenderInfo = renderInfoCollection.getRenderInfo(eye);
#endif
	return currentRenderInfo.pose;
}

OSVR_ProjectionMatrix OsvrAndroidRenderer::GetProjectionMatrix(std::uint8_t eye)
{
	OSVR_ProjectionMatrix proj;

#if UNITY_ANDROID

	OSVR_RenderParams renderParams;
	OSVR_ReturnCode rc = osvrRenderManagerGetDefaultRenderParams(&renderParams);
	checkReturnCode(rc, "osvrRenderManagerGetDefaultRenderParams call failed.");
	RenderInfoCollectionOpenGL renderInfoCollection(gRenderManager,
		renderParams);
	OSVR_RenderInfoOpenGL currentRenderInfo =
		renderInfoCollection.getRenderInfo(eye);
	proj.left = currentRenderInfo.projection.left;
	proj.right = currentRenderInfo.projection.right;
	proj.top = currentRenderInfo.projection.top;
	proj.bottom = currentRenderInfo.projection.bottom;
	proj.nearClip = currentRenderInfo.projection.nearClip;
	proj.farClip = currentRenderInfo.projection.farClip;
#endif
	return proj;
}

OSVR_ViewportDescription OsvrAndroidRenderer::GetViewport(std::uint8_t eye)
{
	OSVR_ViewportDescription viewDesc;

#if UNITY_ANDROID
	OSVR_RenderParams renderParams;
	OSVR_ReturnCode rc = osvrRenderManagerGetDefaultRenderParams(&renderParams);
	checkReturnCode(rc, "osvrRenderManagerGetDefaultRenderParams call failed.");
	RenderInfoCollectionOpenGL renderInfoCollection(gRenderManager,
		renderParams);
	OSVR_RenderInfoOpenGL currentRenderInfo =
		renderInfoCollection.getRenderInfo(eye);
	viewDesc.width = currentRenderInfo.viewport.width;
	viewDesc.height = currentRenderInfo.viewport.height;
	viewDesc.left = currentRenderInfo.viewport.left;
	viewDesc.lower = currentRenderInfo.viewport.lower;
#endif
	return viewDesc;
}

void OsvrAndroidRenderer::OnRenderEvent()
{
#if UNITY_ANDROID
	if (!gOSVRInitialized) {
		// @todo implement some logging/error handling?
		return;
	}

	// this call is idempotent, so we can make it every frame.
	// have to ensure render manager is setup from the rendering thread with
	// a current GLES context, so this is a lazy setup call
	if (!setupRenderManager()) {
		// @todo implement some logging/error handling?
		return;
	}

	OSVR_ReturnCode rc;

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	checkGlError("glClearColor");
	glViewport(0, 0, gWidth, gHeight);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	checkGlError("glClear");

	if (gRenderManager && gClientContext) {
		osvrClientUpdate(gClientContext);
		if (gLastFrame != nullptr) {
			updateTexture(gLastFrameWidth, gLastFrameHeight, gLastFrame);
			osvrClientFreeImage(gClientContext, gLastFrame);
			gLastFrame = nullptr;
		}

		OSVR_RenderParams renderParams;
		rc = osvrRenderManagerGetDefaultRenderParams(&renderParams);
		checkReturnCode(rc,
			"osvrRenderManagerGetDefaultRenderParams call failed.");

		RenderInfoCollectionOpenGL renderInfoCollection(gRenderManager,
			renderParams);

		// Get the present started
		OSVR_RenderManagerPresentState presentState;
		rc = osvrRenderManagerStartPresentRenderBuffers(&presentState);
		checkReturnCode(
			rc, "osvrRenderManagerStartPresentRenderBuffers call failed.");

		int frame = iterations % numBuffers;

		for (OSVR_RenderInfoCount renderInfoCount = 0;
			renderInfoCount < renderInfoCollection.getNumRenderInfo();
			renderInfoCount++) {

			// get the current render info
			OSVR_RenderInfoOpenGL currentRenderInfo =
				renderInfoCollection.getRenderInfo(renderInfoCount);
			// Set color and depth buffers for the frame buffer
			OSVR_RenderTargetInfoOpenGL renderTargetInfo = frameInfoOGL[frame]->renderBuffers[renderInfoCount];
			//gRenderTargets[renderInfoCount];

			// present this render target (deferred until the finish call below)
			OSVR_ViewportDescription normalizedViewport = { 0 };
			normalizedViewport.left = 0.0f;
			normalizedViewport.lower = 0.0f;
			normalizedViewport.width = 1.0f;
			normalizedViewport.height = 1.0f;
			OSVR_RenderBufferOpenGL buffer = { 0 };
			buffer.colorBufferName = GetEyeTextureOpenGL(renderInfoCount, frame);
			buffer.depthStencilBufferName = renderTargetInfo.depthBufferName;

			rc = osvrRenderManagerPresentRenderBufferOpenGL(
				presentState, buffer, currentRenderInfo, normalizedViewport);
			checkReturnCode(
				rc, "osvrRenderManagerPresentRenderBufferOpenGL call failed.");
		}

		iterations++;
		// actually kick off the present
		rc = osvrRenderManagerFinishPresentRenderBuffers(
			gRenderManager, presentState, renderParams, false);
		checkReturnCode(
			rc, "osvrRenderManagerFinishPresentRenderBuffers call failed.");
	}
#endif
}

void OsvrAndroidRenderer::OnInitializeGraphicsDeviceEvent()
{
#if UNITY_ANDROID
	osvrJniWrapperClass = jniEnvironment->FindClass(
		OSVR_JNI_CLASS_PATH); // try to find the class
	if (osvrJniWrapperClass == nullptr) {
		return;
	}
	else { // if osvrJniWrapperClass found, continue

		// get the Android logger method ID
		androidDebugLogMethodID = jniEnvironment->GetStaticMethodID(
			osvrJniWrapperClass, OSVR_JNI_LOG_METHOD_NAME,
			"(Ljava/lang/String;)V"); // find method
		// get the method ID for setting the GL context
		jmethodID setGlContextId = jniEnvironment->GetStaticMethodID(
			osvrJniWrapperClass, "setUnityMainContext",
			"()J"); // find method
		if (setGlContextId == nullptr)
			return;
		else {
			jlong currentEglContextHandle =
				jniEnvironment->CallStaticLongMethod(
				osvrJniWrapperClass, setGlContextId); // call mathod
			// example code for logging the context ID
			/*long myLongValue = (long)currentEglContextHandle;
			std::string stringy = "[OSVR-Unity-Android]  setCurrentContext with handle : " + std::to_string(myLongValue);
			jstring jstr2 = jniEnvironment->NewStringUTF(stringy.c_str());
			jniEnvironment->CallStaticVoidMethod(osvrJniWrapperClass,
			androidDebugLogMethodID, jstr2);*/
			contextSet = true;
		}
		// get the display width and height via JNI
		jmethodID getWidthMID = jniEnvironment->GetStaticMethodID(
			osvrJniWrapperClass, "getDisplayWidth", "()I"); // find method
		jmethodID getHeightMID = jniEnvironment->GetStaticMethodID(
			osvrJniWrapperClass, "getDisplayHeight", "()I"); // find method
		if (getWidthMID == nullptr || getHeightMID == nullptr)
			return;
		else {
			jint displayWidth = jniEnvironment->CallStaticIntMethod(
				osvrJniWrapperClass, getWidthMID); // call method
			jint displayHeight = jniEnvironment->CallStaticIntMethod(
				osvrJniWrapperClass, getHeightMID); // call method
			gWidth = (int)displayWidth;
			gHeight = (int)displayHeight;
		}
	}
#endif
}


void OsvrAndroidRenderer::SetFarClipDistance(double distance)
{
	s_farClipDistance = distance;

}

void OsvrAndroidRenderer::SetIPD(double ipdMeters)
{
	s_ipd = ipdMeters;

}

void OsvrAndroidRenderer::SetNearClipDistance(double distance)
{
	s_nearClipDistance = distance;
}
void OsvrAndroidRenderer::ShutdownRenderManager()
{

	if (gRenderManager) {
		osvrDestroyRenderManager(gRenderManager);
		gRenderManager = gRenderManagerOGL = nullptr;
	}

	// is this needed? Maybe not. the display config manages the lifetime.
	if (gClientContext != nullptr) {
		osvrClientShutdown(gClientContext);
		gClientContext = nullptr;
	}

	osvrClientReleaseAutoStartedServer();
	contextSet = false;
}

void OsvrAndroidRenderer::UpdateRenderInfo()
{
}




