# ExternalPlayerSpyWithImGui
Cool external player tool I made now with a gui

pls don't share, idk if I want to make this public or just share the compiled exe.

EXTRA CUSTOMIZABILITY:
If you would rather have a clean ImGui topbar, go to imgui.cpp and set `ConfigViewportsNoDecoration` to true, I keep it as the ugly window topbar because I have 2 different dpi monitors and ImGui does not handle that (at all) while the windowed version does.

Also then change the window flags in `Main.cpp` to comment out `ImGuiWindowFlags_NoTitleBar`, `ImGuiWindowFlags_NoMove`, and optionally `ImGuiWindowFlags_NoCollapse`.
