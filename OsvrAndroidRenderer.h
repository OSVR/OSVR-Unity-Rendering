#if UNITY_ANDROID
#include <dlfcn.h>
#include <jni.h>
#include <GL/glew.h>
#include <GL/gl.h>
#endif
#include "OsvrUnityRenderer.h"

#include <osvr/ClientKit/ContextC.h>
#include <osvr/ClientKit/DisplayC.h>
#include <osvr/ClientKit/ImagingC.h>
#include <osvr/ClientKit/InterfaceC.h>
#include <osvr/ClientKit/InterfaceCallbackC.h>
#include <osvr/ClientKit/InterfaceStateC.h>
#include <osvr/ClientKit/ServerAutoStartC.h>
#include <chrono>

#include <iostream>
#include <osvr/RenderKit/RenderManagerOpenGLC.h>
#include <sstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <osvr/RenderKit/GraphicsLibraryOpenGL.h>
class OsvrAndroidRenderer : public OsvrUnityRenderer{
public:
	OsvrAndroidRenderer();
	~OsvrAndroidRenderer();
	virtual OSVR_ReturnCode ConstructRenderBuffers();
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
	virtual void SetColorBuffer(void *texturePtr, std::uint8_t eye, std::uint8_t buffer);


#if UNITY_ANDROID
	 JNIEnv *jniEnvironment = 0;
	 jclass osvrJniWrapperClass;
	 jmethodID logMsgId;
	 jobject unityActivityClassInstance;
	 const char* OSVR_JNI_CLASS_PATH = "org/osvr/osvrunityjni/OsvrJNIWrapper";
	 const char* OSVR_JNI_LOG_METHOD_NAME = "logMsg";
	 
#endif
	// JNI hook
	//@todo look into JNI version. Should this match in JNI plugin?
#if UNITY_ANDROID
	 jmethodID androidDebugLogMethodID = nullptr;

	// this OnLoad gets called automatically
	jint JNI_OnLoad(JavaVM *vm, void *reserved) {
		jniEnvironment = 0;
		vm->AttachCurrentThread(&jniEnvironment, 0);
		return JNI_VERSION_1_6;
	}

	const char gVertexShader[] =
		"uniform mat4 model;\n"
		"uniform mat4 view;\n"
		"uniform mat4 projection;\n"
		"attribute vec4 vPosition;\n"
		"attribute vec4 vColor;\n"
		"attribute vec2 vTexCoordinate;\n"
		"varying vec2 texCoordinate;\n"
		"varying vec4 fragmentColor;\n"
		"void main() {\n"
		"  gl_Position = projection * view * model * vPosition;\n"
		"  fragmentColor = vColor;\n"
		"  texCoordinate = vTexCoordinate;\n"
		"}\n";

	const char gFragmentShader[] =
		"precision mediump float;\n"
		"uniform sampler2D uTexture;\n"
		"varying vec2 texCoordinate;\n"
		"varying vec4 fragmentColor;\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = fragmentColor * texture2D(uTexture, texCoordinate);\n"
		//"    gl_FragColor = texture2D(uTexture, texCoordinate);\n"
		"}\n";
#endif

	//@todo keep refactoring into generic base class
	private:
		bool setupOSVR();
		bool setupRenderManager();
		bool setupGraphics(int width, int height);
		bool setupRenderTextures(OSVR_RenderManager renderManager);
		OSVR_ClientContext gClientContext = NULL;
		OSVR_RenderManager gRenderManager = nullptr;
		OSVR_RenderManagerOpenGL gRenderManagerOGL = nullptr;

		bool gGraphicsInitializedOnce =
			false; // if setupGraphics has been called at least once
		bool gOSVRInitialized = false;
		bool gRenderManagerInitialized = false;
		int gWidth = 0;
		int gHeight = 0;
		bool contextSet = false;

#if UNITY_ANDROID

		GLuint gvPositionHandle;
		GLuint gvColorHandle;
		GLuint gvTexCoordinateHandle;
		GLuint guTextureUniformId;
		GLuint gvProjectionUniformId;
		GLuint gvViewUniformId;
		GLuint gvModelUniformId;
		GLuint gFrameBuffer;
		GLuint gTextureID;
		GLuint gLeftEyeTextureID;
		GLuint gLeftEyeTextureIDBuffer2;
		GLuint gRightEyeTextureID;
		GLuint gRightEyeTextureIDBuffer2;
		GLuint gProgram;

		typedef struct OSVR_RenderTargetInfoOpenGL {
			GLuint colorBufferName;
			GLuint depthBufferName;
			GLuint frameBufferName;
			GLuint renderBufferName; // @todo - do we need this?
		} OSVR_RenderTargetInfoOpenGL;
		OSVR_ClientInterface gCamera = NULL;
		OSVR_ClientInterface gHead = NULL;
		int gReportNumber = 0;
		OSVR_ImageBufferElement *gLastFrame = nullptr;
		GLuint gLastFrameWidth = 0;
		GLuint gLastFrameHeight = 0;
		GLubyte *gTextureBuffer = nullptr;

		OSVR_GraphicsLibraryOpenGL gGraphicsLibrary = { 0 };
		OSVR_RenderParams gRenderParams = { 0 };

		// std::vector<OSVR_RenderBufferOpenGL> buffers;
		// std::vector<OSVR_RenderTargetInfoOpenGL> gRenderTargets;
		struct FrameInfoOpenGL {
			// Set up the vector of textures to render to and any framebuffer
			// we need to group them.
			std::vector<OSVR_RenderTargetInfoOpenGL> renderBuffers;
			FrameInfoOpenGL() : renderBuffers(2)
			{
			}

		};
		std::vector<FrameInfoOpenGL*> frameInfoOGL;
#endif

};