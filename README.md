# ExternalPlayerSpyWithImGui
Cool external player tool I made now with a gui

Updated for Nemi because it is a fun and cool project :D

**EXTRA CUSTOMIZABILITY**: If you would rather have a clean ImGui topbar, go to imgui.cpp and set `ConfigViewportsNoDecoration` to true, I keep it as the ugly window topbar because I have 2 different dpi monitors and ImGui does not handle that (at all) while the windowed version does. Also then change the window flags in `Main.cpp` to comment out `ImGuiWindowFlags_NoTitleBar`, `ImGuiWindowFlags_NoMove`, and optionally `ImGuiWindowFlags_NoCollapse`.

# Using the program
1. Compile the program as is and add a signed 64 bit executable (such as Notepad++.exe) to the directory containing `ExternalPlayerSpy.dll` and `Loader.exe`.
2. Rename the signed executable as `ToHijack.exe`.
3. Run the loader and it will work as is.


Should look like this:<br>
![directory showcase]([https://github.com/[username]/[reponame]/blob/[branch]/image.jpg](https://github.com/Alexflamer11/ExternalPlayerSpyWithImGui/blob/master/DirectoryShowcase.jpg)?raw=true)

### Alternative
1. Change the project settings to compile `ExternalPlayerSpy` as an exe.
2. Sign the player spy with a valid cert (this is required or you will get kicked as only signed processes are allowed to have open handles).
3. Load the player spy and it will work as is.

## Caching the offsets
1. Go to this game: https://www.roblox.com/games/14933634231/Taxi-Gamers
2. Load the player spy and offsets will be cached if you see your player in the list.

Make sure to recache offsets if an update breaks the player spy, the console displays a warning if a new version is found from what was cached.

### Alternative
1. Add this script to your game and do the above steps
```lua
while wait() do
	list = {
		game.PlaceId,
		game.JobId,
		game.Players.LocalPlayer.UserId,
		game.Players.LocalPlayer.AccountAge,
		game.Players.LocalPlayer.DisplayName,
		game.Players.LocalPlayer.FollowUserId,
		-- TeleportedIn is a locked property, maybe one day I will change permissions if it fails to get it
		pcall(function() return game.Players.LocalPlayer.TeleportedIn end)
	}
end
```


# How to get or compile the libraries yourself

Libraries/glut/lib/glut.lib:<br>
-> Go to https://www.opengl.org/resources/libraries/glut/glut_downloads.php and find `glutdlls37beta.zip`<be>
-> Alternatively just download the files directly at `https://www.opengl.org/resources/libraries/glut/glutdlls37beta.zip`<br>
Yes I am using the 20-year-old outdated OpenGL libraries, their site, and all basic searches lead to it what do you want me to do?<br><br>
An additional note with the library, if the header file is copied, make sure to comment out line `51` of `Libraries/glut/include/GL/glut.h` (comments out the pragma include for `glut32.lib`)<br>
-> Should look like like this `//#pragma comment (lib, "glut32.lib")    /* link with Win32 GLUT lib */`

Libraries/curl/lib/libcurl.lib<be>
-> With vcpkg installed run `vcpkg install curl:x64-windows-static`<br>
-> You may need to first run `vcpkg install zlib:x64-windows-static` (should not need to happen but it might)
