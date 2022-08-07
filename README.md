# ExternalPlayerSpyWithImGui
Cool external player tool I made now with a gui

pls don't share, idk if I want to make this public or just share the compiled exe.

If you would rather have a clean ImGui topbar, go to imgui.cpp and set `ConfigViewportsNoDecoration` to true, I keep it as the ugly window topbar because I have 2 different dpi monitors and ImGui does not handle that (at all) while the windowed version does.

To build the project GLUT has to be installed:
Download the files from here (https://www.opengl.org/resources/libraries/glut/glutdlls37beta.zip) or from their site (https://www.opengl.org/resources/libraries/glut/glut_downloads.php) under Windows `If you want just the GLUT header file, the .LIB, and .DLL files all pre-compiled for Intel platforms, you can simply download the glutdlls37beta.zip file (149 kilobytes).`

Put `glut.h` in `C:\Program Files\Microsoft Visual Studio\VS_VERSION\Include\GL`, you may have to create a `GL` folder.
Put `glut32.lib` in `C:\Program Files (x86)\Microsoft Visual Studio\VS_VERSION\Community\VC\Auxiliary\VS\lib\x86`
Put `glut64.lib` in `C:\Program Files (x86)\Microsoft Visual Studio\VS_VERSION\Community\VC\Auxiliary\VS\lib\x64`
