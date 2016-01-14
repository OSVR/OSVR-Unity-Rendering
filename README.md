# OSVR-Unity-Rendering
Rendering plugin for OSVR-Unity, based on the docs here: http://docs.unity3d.com/Manual/NativePluginInterface.html

This plugin enables OSVR RenderManager features in OSVR-Unity projects. These features include:
* Direct Mode
* Timewarp
* Distortion Correction for HDK 1.3

The osvrUnityRenderingPlugin.dll library built by this project goes into the Plugins folder of an OSVR-Unity project. It also requires:
* SDL2.dll 
* glew32.dll
* osvrRenderManager.dll

to be present in order to successully load the plugin in Unity.

## RenderManager Requirements
* Unity 5.2+ with DX11 Graphics API. OpenGL support is coming soon.
* Updated NVIDIA drivers
* Compatible NVIDIA GPU (we've seen DirectMode working on cards as low as 560m)
* Latest RenderManager installer available from: http://osvr.github.io/using/

After installing RenderManager, you can run **EnableOSVRDirectMode.exe** and the HDK will be removed as a Windows display. 
**DisableOSVRDirectMode.exe** does the opposite. 

![Enable-Disable](https://github.com/OSVR/OSVR-Unity-Rendering/blob/rendermanager_docs/images/enable_disable_directmode.png?raw=true)

From the Windows Start Menu, run one of the RenderManager demos such as *RenderManagerD3DExample3D.exe*. If it works, you should scene that looks like:

![Cube Room](https://github.com/OSVR/OSVR-Unity-Rendering/blob/rendermanager_docs/images/cube_room.png?raw=true)

An external RenderManager configuration file is referenced in the *osvr_server_config.json* file:

![OSVR server config snippet](https://github.com/OSVR/OSVR-Unity-Rendering/blob/rendermanager_docs/images/osvr_server_config_rm_snippet.png?raw=true)

and will look something like:

![RenderManager config](https://github.com/OSVR/OSVR-Unity-Rendering/blob/rendermanager_docs/images/osvr_server_config_rm.png?raw=true)

If **directModeEnabled** is set to false, the game will render in a window. Setting it to true displays the game on the HDK, with an option in the Unity plugin for mirroring the game on a monitor. “Mirror mode” will soon be a RenderManager feature rather than a Unity plugin feature.

**numBuffers** controls whether single or double buffering (or more) is used.

**renderOverfillFactor** expands the size of the render window, adding more pixels around the border, so that there is margin to be rendered when using distortion (which pulls in pixels from outside the boundary). The larger this factor, the less likely we'll see clamped images at the border but the more work taken during rendering. A factor of 1.0 means render at standard size, 2.0 would render 4x as many pixels (2x in both X and Y). 

**renderOversampleFactor** controls the density of the render texture. Increasing the value will add more pixels within the texture, so that when it is rendered into the final buffer with distortion correction it can be expanded by the distortion without making fat pixels. Alternatively, it can be reduced to make rendering faster at the expense if visible pixel resolution. A factor of 1.0 means render at standard size, 2.0 would render 4x as many pixels (2x in both X and Y) and 0.5 would render 1/4 as many pixels.

**xPosition** and **yPosition** control the position of the game window when not in DirectMode.

**maxMsBeforeVsync** controls when we read tracker reports before vsync.

**asynchronous timewarp** is coming soon.


