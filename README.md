# OSVR-Unity-Rendering
Rendering plugin for OSVR-Unity, based on the docs here: http://docs.unity3d.com/Manual/NativePluginInterface.html

This plugin enables OSVR RenderManager features in OSVR-Unity projects. These features include:
- Direct Mode
- Asynchronous Timewarp
- Distortion Mesh

## Requirements
Unity 5.2+ with DX11 API. OpenGL support is on the way. There is a bug in Unity 5.2 that doesn't provide a device type when OpenGL Core graphics API is selected in Unity. Should be fixed in the next Unity release.

The osvrUnityRenderingPlugin.dll library built by this project goes into the Plugins folder of an OSVR-Unity project. It also requires:
- SDL2.dll 
- glew32.dll
- osvrRenderManager.dll
to be present in order to successully load the pluigin.

