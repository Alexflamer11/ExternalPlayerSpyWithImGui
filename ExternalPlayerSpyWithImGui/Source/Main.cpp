// Curl sucks ass, follow this guide -> https://stackoverflow.com/questions/53861300/how-do-you-properly-install-libcurl-for-use-in-visual-studio-2017

// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "Utils/Utils.hpp"
#include "PlayerInformer/PlayerInformer.hpp"
#include "nlohmann/json.hpp"

#include <thread>
#include <stack>
#include <fstream>

#include <gl/glut.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadTextureFromMemory(std::string file, GLuint* out_texture, int* out_width, int* out_height)
{
#define GL_CLAMP_TO_EDGE 0x812F
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load_from_memory((unsigned char*)file.c_str(), file.size(), &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

	// Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	stbi_image_free(image_data);

	*out_texture = image_texture;
	*out_width = image_width;
	*out_height = image_height;

	return true;
}

std::string TimeNow()
{
	// Save time created to the file
	time_t now = time(0);

	char date_and_time[128];
	date_and_time[127] = '\0';
	if (ctime_s(date_and_time, sizeof(date_and_time), &now) == 0)
	{
		// Why does this even need to exist in the first place?
		size_t len = strlen(date_and_time);
		if (len > 0 && date_and_time[len - 1] == '\n')
			date_and_time[len - 1] = '\0';

		return date_and_time;
	}

	return "UNKNOWN";
}

nlohmann::json DumpPlayer(std::shared_ptr<PlayerInformer::PlayerInformation> plr)
{
	nlohmann::json player;

	player["Name"] = plr->Name;
	player["DisplayName"] = plr->DisplayName;
	player["UserId"] = plr->UserId;
	player["AccountAge"] = plr->AccountAge;
	player["TeleportedIn"] = plr->TeleportedIn;
	player["FollowUserId"] = plr->FollowUserId;
	player["Profile_Link"] = plr->ProfileLink;
	
	switch (plr->JoinStatus)
	{
	case JOIN_STATUS::NONE:
		player["Status"] = "Active";
		break;
	case JOIN_STATUS::JOINED:
		player["Status"] = "Joined";
		break;
	case JOIN_STATUS::LEFT:
		player["Status"] = "Left";
		break;
	case JOIN_STATUS::GONE:
		player["Status"] = "Gone";
		break;
	default:
		player["Status"] = "UNKNOWN";
		break;
	}

	return player;
}

nlohmann::json DumpServerInformation(std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>>& player_list, PlayerInformer::EngineData& engine_data)
{
	nlohmann::json result = {};

	result["Log_Created"] = TimeNow();

	// List of cached players that left the game and still have references
	std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> cached_players = {};

	// Server data
	{
		nlohmann::json server_info = {};

		// Server Information
		{
			server_info["PlaceId"] = engine_data.PlaceId;
			server_info["JobId"] = engine_data.JobId;

			// Save a list of all join links we would like to have (just Lua and Browser/Javascript really, idk what else)
			nlohmann::json join_links = {};

			// Browser
			{
				std::string browser_link = "Roblox.GameLauncher.joinGameInstance(";
				browser_link += engine_data.PlaceIdStr;
				browser_link += ", \"";
				browser_link += engine_data.JobId;
				browser_link += "\")";

				join_links["Browser"] = browser_link;
			}

			// Lua
			{
				std::string lua_link = "game:GetService(\"TeleportService\"):TeleportToPlaceInstance(";
				lua_link += engine_data.PlaceIdStr;
				lua_link += ", \"";
				lua_link += engine_data.JobId;
				lua_link += "\")";

				join_links["Lua"] = lua_link;
			}

			server_info["Join_Links"] = join_links;
		}

		result["Server_Information"] = server_info;
	}

	auto GetPlayerCacheList = [&player_list, &cached_players](std::shared_ptr<PlayerInformer::PlayerInformation> plr) -> std::string
	{
		std::string list = "";

		for (auto other_plr : player_list)
		{
			if (other_plr->UserId == plr->UserId)
				return "Players";
		}

		for (auto other_plr : cached_players)
		{
			if (other_plr->UserId == plr->UserId)
				return "Cached_Players";
		}

		return list;
	};

	// Before writing any players, cache all the followers as
	//   this needs to be recrusive to make the rest of the
	//   player writing easier
	std::stack<std::shared_ptr<PlayerInformer::PlayerInformation>> players_to_cache = {};
	for (auto plr : player_list)
	{
		if (plr->FollowUserId && plr->CachedFollowUser.get())
		{
			if (GetPlayerCacheList(plr->CachedFollowUser).empty())
			{
				// If the player is not cached, add them to the cache
				cached_players.push_back(plr->CachedFollowUser);
				players_to_cache.push(plr->CachedFollowUser);
			}
		}
	}

	// Iterate all cached players and cache any 2nd, 3rd+ followers too
	while (players_to_cache.size())
	{
		std::shared_ptr<PlayerInformer::PlayerInformation> plr = players_to_cache.top();
		players_to_cache.pop();

		if (plr->FollowUserId && plr->CachedFollowUser.get())
		{
			if (GetPlayerCacheList(plr->CachedFollowUser).empty())
			{
				cached_players.push_back(plr->CachedFollowUser);
				players_to_cache.push(plr->CachedFollowUser);
			}
		}
	}

	auto WritePlayer = [&GetPlayerCacheList](std::shared_ptr<PlayerInformer::PlayerInformation> plr) -> nlohmann::json
	{
		nlohmann::json player = DumpPlayer(plr);

		if (plr->FollowUserId)
		{
			if (plr->CachedFollowUser.get())
			{
				std::string list = GetPlayerCacheList(plr->CachedFollowUser);
				if (!list.empty())
					player["Followed_Player_List"] = list;
				else
					player["Followed_Player_List"] = nullptr; // null
			}
			else
				player["Followed_Player_List"] = nullptr; // null
		}

		return player;
	};

	// Active player data
	{
		nlohmann::json players = nlohmann::json::value_t::array; // prevent null list

		for (auto plr : player_list)
			players.push_back(WritePlayer(plr));

		result["Players"] = players;
	}

	// Cached version of people that were followed but are no longer in the game
	{
		nlohmann::json cached_players_json = nlohmann::json::value_t::array; // prevent null list

		for (auto plr : cached_players)
			cached_players_json.push_back(WritePlayer(plr));

		result["Cached_Players"] = cached_players_json;
	}
	
	//printf("JSON RESULT: %s\n", result.dump(1, '\t').c_str());

	return result;
}

bool DialogDebounce = false;
void WriteJSONToFileDialog(std::string title, std::string contents)
{
	wchar_t file_name[MAX_PATH];

	std::wstring title_w = Utils::FromUtf8(title);

	OPENFILENAMEW ofn;
	ZeroMemory(&file_name, sizeof(file_name));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = L"JSON File\0.JSON\0Text File\0.TXT\0All Files\0*.*\0\0";
	ofn.lpstrFile = file_name;
	ofn.nMaxFile = MAX_PATH - 6; // for our append
	ofn.lpstrTitle = title_w.c_str();
	ofn.Flags = OFN_DONTADDTORECENT;

	if (GetSaveFileNameW(&ofn))
	{
		file_name[MAX_PATH - 1] = '\0';

		std::wstring file_name_str = file_name; // always null terminated

		auto CheckOrAppendExtension = [&ofn](std::wstring path, std::wstring name) -> std::wstring
		{
			if (path.size() > name.size())
			{
				if (!wcscmp(path.c_str() + path.size() - name.size(), name.c_str()))
					return path;
			}

			return path + name;
		};

		// Our custom filters
		switch (ofn.nFilterIndex)
		{
		case 1:
			file_name_str = CheckOrAppendExtension(file_name_str, L".json");
			break;
		case 2:
			file_name_str = CheckOrAppendExtension(file_name_str, L".txt");
			break;
		default:
			break;
		}

		std::fstream file;
		file.open(file_name_str, std::fstream::out | std::fstream::binary);
		file.write(contents.c_str(), contents.size());
		file.close();
	}
	else
		puts("[!] Failed to information to file.");

	DialogDebounce = false;
}

// Save a list of players that have been popped out
std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> popout_players;

void PopoutPlayer(std::shared_ptr<PlayerInformer::PlayerInformation> plr)
{
	// Check if player exists
	for (auto& existing_plr : popout_players)
	{
		if (existing_plr->UserId == plr->UserId)
			return;
	}

	popout_players.push_back(plr);
}

// A unique child frame count to prevent clashing with other frames
int child_count = 50;
void DrawPlayer(std::shared_ptr<PlayerInformer::PlayerInformation> plr)
{
	if (!plr->ProfilePicture.empty() && !plr->ImageId)
	{
		int width, height;
		if (!LoadTextureFromMemory(plr->ProfilePicture, &plr->ImageId, &width, &height))
		{
			plr->ProfilePicture = "";
			plr->ImageId = 0;
			plr->FetchingProfilePicture = false;
		}
	}

	// Create 2 child frames to render the image side by side
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::BeginChild(child_count++, ImVec2(250, 250));
	ImGui::Image((ImTextureID)plr->ImageId, ImVec2(250, 250));
	ImGui::EndChild();
	ImGui::PopStyleVar();

	//printf("image id: %X\n", plr->ImageId);

	ImGui::SameLine();
	ImGui::BeginChild(child_count++, ImVec2(400, 250));

	float pos = ImGui::GetCursorPosY();
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(96, 234, 90, 255));
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Name:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->Name.c_str()))
			Utils::SetClipboard(plr->Name);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 253, 131, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Display Name:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->DisplayName.c_str()))
			Utils::SetClipboard(plr->DisplayName);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("User Id:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->UserIdStr.c_str()))
			Utils::SetClipboard(plr->UserIdStr);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 58, 114, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Account Age:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->AccountAgeStr.c_str()))
			Utils::SetClipboard(plr->AccountAgeStr);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Created:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->AccountCreatedStr.c_str()))
			Utils::SetClipboard(plr->AccountCreatedStr);

		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 188, 255));
		ImGui::Text("Teleported In:"); ImGui::SameLine();
		ImGui::PopStyleColor();
		ImGui::Text(plr->TeleportedInStr.c_str());

		if (plr->FollowUserId)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 147, 255, 255));
			pos = ImGui::GetCursorPosY();
			ImGui::SetCursorPosY(pos + 3);
			ImGui::Text("Follow User Id:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
			ImGui::PopStyleColor();
			if (ImGui::Button(plr->FollowUserIdStr.c_str()))
				Utils::SetClipboard(plr->FollowUserIdStr);

	
			if (ImGui::Button(plr->FollowUserIdProfile.c_str()))
				Utils::SetClipboard(plr->FollowUserIdProfile);

			// Find follower
			auto player_reader = PlayerInformer::PlayerDataReader(true);
			std::shared_ptr<PlayerInformer::PlayerInformation> cached_follow_user = plr->CachedFollowUser;

			if (!cached_follow_user.get())
				ImGui::Text("Followed user has left the game.");
			else
			{
				if (ImGui::Button("Popout Followed User"))
					PopoutPlayer(cached_follow_user);
			}
		}
	}
		
	ImGui::EndChild();
	
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(253, 105, 186, 255));
	pos = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(pos + 3);
	ImGui::Text("Profile Link:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
	ImGui::PopStyleColor();

	if (ImGui::Button(plr->ProfileLink.c_str()))
		Utils::SetClipboard(plr->ProfileLink);

	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 125, 55, 255));
	pos = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(pos + 3);
	ImGui::Text("Dump Information:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
	ImGui::PopStyleColor();

	if (ImGui::Button("Copy to Clipboard"))
	{
		nlohmann::json info = {};
		info["Log_Created"] = TimeNow();
		info["Player"] = DumpPlayer(plr);

		std::string dumped = info.dump(1, '\t');

		Utils::SetClipboard(dumped);
	}

	ImGui::SameLine(); ImGui::SetCursorPosY(pos); ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4);

	if (ImGui::Button("Dump to File"))
	{
		if (!DialogDebounce)
		{
			DialogDebounce = true;

			nlohmann::json info = {};
			info["Log_Created"] = TimeNow();
			info["Player"] = DumpPlayer(plr);

			std::thread(WriteJSONToFileDialog, "Save Player Information", info.dump(1, '\t')).detach();
		}
	}
}

bool CompareStringStart(std::string& to_compare, char* buffer, size_t buffer_len)
{
	if (to_compare.size() >= buffer_len)
		return !memcmp(to_compare.c_str(), buffer, buffer_len);

	return false;
}

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

int main()
{
	SetConsoleTitleA("Chinese Government Player Observation Tool MKII - Gui Edition");
	Utils::ShowConsole(); // If console is hidden

	// Init our own custom player data
	PlayerInformer::StartWatcher();

	// Setup window
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100
	const char* glsl_version = "#version 100";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
	const char* glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);			// Required on Mac
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);			// 3.0+ only
#endif

	// house main window
	// Create window with graphics context
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(1, 1, "Chinese Government Player Observation Tool MKII - Gui Edition", NULL, NULL);
	if (window == NULL)
		return 1;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;	   // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;	  // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;		   // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;		 // Enable Multi-Viewport / Platform Windows
	io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;

	ImGuiWindowFlags flags = 0;
	flags |= ImGuiWindowFlags_NoTitleBar;
	flags |= ImGuiWindowFlags_NoMove;
	flags |= ImGuiWindowFlags_NoCollapse;
	flags |= ImGuiWindowFlags_NoDocking;
	flags |= ImGuiWindowFlags_NoResize;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	//// Our state
	//bool show_demo_window = true;
	//bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	bool informer_open = true;

	ImFont* arial_18 = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f);
	
	if (!arial_18 )
	{
		puts("[!] Failed to load font. Closing in 5 seconds...");

		std::this_thread::sleep_for(std::chrono::seconds(5));
		exit(EXIT_SUCCESS);
	}

	// Save information for our tab searching
	bool search_name = true;
	bool search_display_name = true;
	bool search_user_id = true;

	bool follower_search_name = true;
	bool follower_search_display_name = true;
	bool follower_search_user_id = true;

	bool teleporter_search_name = true;
	bool teleporter_search_display_name = true;
	bool teleporter_search_user_id = true;

	// Save our list of tabs
	int current_tab_id = 0;
	std::vector<const char*> buttons = { "Search", "Followers", "Teleporters", "Extra", "Credits" };

	// Save unique buffers for each search
	char search_players_buffer[64] = {};
	char search_followers_buffer[64] = {};
	char search_teleporters_buffer[64] = {};

	// Main loop
	while (!glfwWindowShouldClose(window))
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		//if (show_demo_window)
		//	ImGui::ShowDemoWindow(&show_demo_window);

		//if (show_another_window)
		//{
		//	ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		//	ImGui::Text("Hello from another window!");
		//	if (ImGui::Button("Close Me"))
		//		show_another_window = false;
		//	ImGui::End();
		//}

		if (informer_open)
		{
			// Chinese Government RBG Gaming Chair with built in Speaskers Player Observation Tool
			ImGui::SetNextWindowSize(ImVec2(707, 556));
			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(11, 15, 26, 255));
			ImGui::Begin("Chinese Government Player Observation Tool MKII - Gui Edition", &informer_open, flags);
			ImGui::PopStyleColor();

			ImGui::PushFont(arial_18);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

			ImVec2 start;
			ImVec2 end;

			for (size_t i = 0; i < buttons.size(); i++)
			{
				if (i == current_tab_id)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(18, 25, 42, 255));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(18, 25, 42, 255));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(18, 25, 42, 255));

					start = ImGui::GetCursorScreenPos();
					start.y += 28;
					end = ImGui::GetCursorScreenPos();
					end.x += 100;
					end.y += 28;

					if (ImGui::Button(buttons[i], ImVec2(100, 30)))
						current_tab_id = i;

					ImGui::PopStyleColor(3);
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(43, 72, 128, 255));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(43, 72, 128, 255));
					//ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(43, 72, 128, 255));

					if (ImGui::Button(buttons[i], ImVec2(100, 30)))
						current_tab_id = i;

					ImGui::PopStyleColor(2);
				}

				if (i != buttons.size())
					ImGui::SameLine(0, 0);
			}

			ImGui::PopStyleVar();
			ImGui::PopFont();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

			if (current_tab_id != 4)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 83); // Padding of 8 is default
				if (ImGui::Button("Refresh", ImVec2(75, 30)))
					PlayerInformer::RequestRefresh();
			}
			else
				ImGui::NewLine();

			ImGui::PopStyleVar();

			ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(18, 25, 42, 255));

			switch (current_tab_id)
			{
			case 0: // Search
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, 30));

				ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(5, 7, 15, 255));
				ImGui::PushItemWidth(300);
				ImGui::InputTextWithHint("##Player Search Input", "Search players...", search_players_buffer, 64); ImGui::SameLine();
				ImGui::PopItemWidth();
				ImGui::Checkbox("Name", &search_name); ImGui::SameLine();
				ImGui::Checkbox("Display Name", &search_display_name); ImGui::SameLine();
				ImGui::Checkbox("User Id", &search_user_id);
				ImGui::PopStyleColor(1);

				// Style var before adding padding to the new line
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::EndChildFrame();
				ImGui::PopStyleVar();

				ImGui::BeginChildFrame(current_tab_id + 12, ImVec2(0, -23)); // Fill to bottom

				size_t buffer_len = strlen(search_players_buffer);
				uint64_t possible_id_search = 0;
				bool id_successful = false;
				if (buffer_len)
				{
					for (size_t i = 0; i < buffer_len; i++)
					{
						if (search_players_buffer[i] >= '0' && search_players_buffer[i] <= '9')
							id_successful = true;
					}
				}

				child_count = 50; // refresh child frame count
				auto players = PlayerInformer::PlayerDataReader();
				for (auto plr : players.data)
				{
					if (!buffer_len
						|| (search_name && CompareStringStart(plr->LowerName, search_players_buffer, buffer_len))
						|| (search_display_name && CompareStringStart(plr->LowerDisplayName, search_players_buffer, buffer_len))
						|| (search_user_id && CompareStringStart(plr->UserIdStr, search_players_buffer, buffer_len)))
					{
						//printf("player stuff: %s\n", plr->Title.c_str());

						if (plr->JoinStatus == JOIN_STATUS::JOINED)
						{
							ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(44, 120, 75, 255));
							ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(103, 175, 116, 255));
						}
						else if (plr->JoinStatus == JOIN_STATUS::LEFT)
						{
							ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(170, 30, 30, 255));
							ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(240, 70, 70, 255));
						}
						
						bool open_header = ImGui::CollapsingHeader(plr->Title.c_str());

						if (plr->JoinStatus == JOIN_STATUS::JOINED || plr->JoinStatus == JOIN_STATUS::LEFT)
							ImGui::PopStyleColor(2);

						if (open_header)
						{
							DrawPlayer(plr);

							ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(49, 119, 170, 255));
							if (ImGui::Button(plr->UniquePopoutName.c_str(), ImVec2(-1, 0)))
							{
								ImGui::PopStyleColor();
								PopoutPlayer(plr);
							}
							else
								ImGui::PopStyleColor();
						}
					}
				}

				ImGui::EndChildFrame();

				break;
			}
			case 1: // Followers
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, 30));

				ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(5, 7, 15, 255));
				ImGui::PushItemWidth(300);
				ImGui::InputTextWithHint("##Follower Search Input", "Search followers...", search_followers_buffer, 64); ImGui::SameLine();
				ImGui::PopItemWidth();
				ImGui::Checkbox("Name", &follower_search_name); ImGui::SameLine();
				ImGui::Checkbox("Display Name", &follower_search_display_name); ImGui::SameLine();
				ImGui::Checkbox("Followed User Id", &follower_search_user_id);
				ImGui::PopStyleColor();

				// Style var before adding padding to the new line
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::EndChildFrame();
				ImGui::PopStyleVar();

				ImGui::BeginChildFrame(current_tab_id + 12, ImVec2(0, -23)); // Fill to bottom

				size_t buffer_len = strlen(search_followers_buffer);
				uint64_t possible_id_search = 0;
				bool id_successful = false;
				if (buffer_len)
				{
					for (size_t i = 0; i < buffer_len; i++)
					{
						if (search_followers_buffer[i] >= '0' && search_followers_buffer[i] <= '9')
							id_successful = true;
					}
				}

				child_count = 50; // refresh child frame count
				auto players = PlayerInformer::PlayerDataReader();
				for (auto plr : players.data)
				{
					if (plr->FollowUserId)
					{
						if (!buffer_len
							|| (follower_search_name && CompareStringStart(plr->LowerName, search_followers_buffer, buffer_len))
							|| (follower_search_display_name && CompareStringStart(plr->LowerDisplayName, search_followers_buffer, buffer_len))
							|| (follower_search_user_id && CompareStringStart(plr->FollowUserIdStr, search_followers_buffer, buffer_len)))
						{
							//printf("player stuff: %s\n", plr->Title.c_str());
							
							if (plr->JoinStatus == JOIN_STATUS::JOINED)
							{
								ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(44, 120, 75, 255));
								ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(103, 175, 116, 255));
							}
							else if (plr->JoinStatus == JOIN_STATUS::LEFT)
							{
								ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(170, 30, 30, 255));
								ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(240, 70, 70, 255));
							}

							bool open_header = ImGui::CollapsingHeader(plr->Title.c_str());

							if (plr->JoinStatus == JOIN_STATUS::JOINED || plr->JoinStatus == JOIN_STATUS::LEFT)
								ImGui::PopStyleColor(2);

							if (open_header)
							{
								DrawPlayer(plr);

								if (ImGui::Button(plr->UniquePopoutName.c_str(), ImVec2(-1, 0)))
									PopoutPlayer(plr);
							}
						}
					}
				}

				ImGui::EndChildFrame();

				break;
			}
			case 2: // Teleporters
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, 30));

				ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(5, 7, 15, 255));
				ImGui::PushItemWidth(300);
				ImGui::InputTextWithHint("##Teleporters Search Input", "Search teleporters...", search_teleporters_buffer, 64); ImGui::SameLine();
				ImGui::PopItemWidth();
				ImGui::Checkbox("Name", &teleporter_search_name); ImGui::SameLine();
				ImGui::Checkbox("Display Name", &teleporter_search_display_name); ImGui::SameLine();
				ImGui::Checkbox("User Id", &teleporter_search_user_id);
				ImGui::PopStyleColor();

				// Style var before adding padding to the new line
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::EndChildFrame();
				ImGui::PopStyleVar();

				ImGui::BeginChildFrame(current_tab_id + 12, ImVec2(0, -23)); // Fill to bottom

				size_t buffer_len = strlen(search_teleporters_buffer);
				uint64_t possible_id_search = 0;
				bool id_successful = false;
				if (buffer_len)
				{
					for (size_t i = 0; i < buffer_len; i++)
					{
						if (search_teleporters_buffer[i] >= '0' && search_teleporters_buffer[i] <= '9')
							id_successful = true;
					}
				}

				child_count = 50; // refresh child frame count
				auto players = PlayerInformer::PlayerDataReader();
				for (auto plr : players.data)
				{
					if (plr->TeleportedIn)
					{
						if (!buffer_len
							|| (teleporter_search_name && CompareStringStart(plr->LowerName, search_teleporters_buffer, buffer_len))
							|| (teleporter_search_display_name && CompareStringStart(plr->LowerDisplayName, search_teleporters_buffer, buffer_len))
							|| (teleporter_search_user_id && CompareStringStart(plr->UserIdStr, search_teleporters_buffer, buffer_len)))
						{
							//printf("player stuff: %s\n", plr->Title.c_str());
							
							if (plr->JoinStatus == JOIN_STATUS::JOINED)
							{
								ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(44, 120, 75, 255));
								ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(103, 175, 116, 255));
							}
							else if (plr->JoinStatus == JOIN_STATUS::LEFT)
							{
								ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(170, 30, 30, 255));
								ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(240, 70, 70, 255));
							}

							bool open_header = ImGui::CollapsingHeader(plr->Title.c_str());

							if (plr->JoinStatus == JOIN_STATUS::JOINED || plr->JoinStatus == JOIN_STATUS::LEFT)
								ImGui::PopStyleColor(2);

							if (open_header)
							{
								DrawPlayer(plr);

								if (ImGui::Button(plr->UniquePopoutName.c_str(), ImVec2(-1, 0)))
									PopoutPlayer(plr);
							}
						}
					}
				}

				ImGui::EndChildFrame();

				break;
			}
			case 3: // Extra
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, -23));

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("Server Information:");
				ImGui::PopStyleColor();

				auto engine_reader = PlayerInformer::EngineReader();
				auto player_reader = PlayerInformer::PlayerDataReader();
				
				float pos = ImGui::GetCursorPosY();
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 58, 114, 255));
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Place Id:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button(engine_reader.data.PlaceIdStr.c_str()))
					Utils::SetClipboard(engine_reader.data.PlaceIdStr);

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(96, 234, 90, 255));
				pos = ImGui::GetCursorPosY();
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Job Id:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button(engine_reader.data.JobId.c_str()))
					Utils::SetClipboard(engine_reader.data.JobId);

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 188, 255));
				pos = ImGui::GetCursorPosY();
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Join Script:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button("Browser"))
				{
					std::string browser_link = "Roblox.GameLauncher.joinGameInstance(";
					browser_link += engine_reader.data.PlaceIdStr;
					browser_link += ", \"";
					browser_link += engine_reader.data.JobId;
					browser_link += "\")";

					Utils::SetClipboard(browser_link);
				}

				ImGui::SameLine(); ImGui::SetCursorPosY(pos); ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4);
				if (ImGui::Button("Lua"))
				{
					std::string lua_link = "game:GetService(\"TeleportService\"):TeleportToPlaceInstance(";
					lua_link += engine_reader.data.PlaceIdStr;
					lua_link += ", \"";
					lua_link += engine_reader.data.JobId;
					lua_link += "\")";

					Utils::SetClipboard(lua_link);
				}

				ImGui::NewLine();

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("General Settings:");
				ImGui::PopStyleColor();

				if (ImGui::Button("Show Console"))
					Utils::ShowConsole();

				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4);

				if (ImGui::Button("Hide Console"))
					Utils::HideConsole();

				ImGui::NewLine();

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("Extra:");
				ImGui::PopStyleColor();

				pos = ImGui::GetCursorPosY();
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Dump all Player Information:"); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button("Copy to Clipboard"))
				{
					nlohmann::json info = DumpServerInformation(player_reader.data, engine_reader.data);
					std::string dumped = info.dump(1, '\t');
					Utils::SetClipboard(dumped);
				}

				ImGui::SameLine(); ImGui::SetCursorPosY(pos); ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4);

				if (ImGui::Button("Dump to File"))
				{
					if (!DialogDebounce)
					{
						DialogDebounce = true;

						nlohmann::json info = DumpServerInformation(player_reader.data, engine_reader.data);
						std::thread(WriteJSONToFileDialog, "Save Player Information", info.dump(1, '\t')).detach();
					}
				}

				ImGui::EndChildFrame();

				break;
			}
			case 4: // Credits
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, -23));

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("Creator:");
				ImGui::PopStyleColor();

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(96, 234, 90, 255));
				ImGui::Text("Alex the Great#9740");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - Making the entire project");
				ImGui::PopStyleVar();

				ImGui::NewLine();

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("External Code:");
				ImGui::PopStyleColor();

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 58, 114, 255));
				ImGui::Text("Austin (axstin @ github)");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - Easy auto updating task scheduler (from fpsunlocker)");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_axstin = "https://github.com/axstin/rbxfpsunlocker";
				if (ImGui::Button(link_axstin.c_str()))
					Utils::SetClipboard(link_axstin);

				ImGui::NewLine();

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("Libraries:");
				ImGui::PopStyleColor();

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("cpr");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - Makes http(s) requests bearable");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_cpr = "https://github.com/libcpr/cpr";
				if (ImGui::Button(link_cpr.c_str()))
					Utils::SetClipboard(link_cpr);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("curl");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - Http(s) requests to fetch player images");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_curl = "https://github.com/curl/curl";
				if (ImGui::Button(link_curl.c_str()))
					Utils::SetClipboard(link_curl);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("glfw");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - OpenGL rendering backend library");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_glfw = "https://github.com/glfw/glfw";
				if (ImGui::Button(link_glfw.c_str()))
					Utils::SetClipboard(link_glfw);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("imgui");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - User interface (docking branch)");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_imgui = "https://github.com/ocornut/imgui";
				if (ImGui::Button(link_imgui.c_str()))
					Utils::SetClipboard(link_imgui);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("nlohmann");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - JSON tool to parse Roblox player image results");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_nlohmann = "https://github.com/nlohmann/json";
				if (ImGui::Button(link_nlohmann.c_str()))
					Utils::SetClipboard(link_nlohmann);

				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
				ImGui::Text("stb (image)");
				ImGui::PopStyleColor();
				ImGui::SameLine(); ImGui::Text(" - Convert images to a ready to use memory block for rendering");
				ImGui::PopStyleVar();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				std::string link_std_image = "https://github.com/nothings/stb/blob/master/stb_image.h";
				if (ImGui::Button(link_std_image.c_str()))
					Utils::SetClipboard(link_std_image);

				ImGui::EndChildFrame();

				break;
			}
			default:
				break;
			}

			// Bottom of every frame
			auto engine_reader = PlayerInformer::EngineReader();
			ImGui::Text(engine_reader.data.PlayerCountCache.c_str());
			
			ImGui::PopStyleColor();

			ImGui::GetWindowDrawList()->AddLine(start, end, IM_COL32(43, 72, 128, 255), 3);

			ImGui::End();
		}
		else
			exit(EXIT_SUCCESS);

		// Render all players that are popped out
		int popout_count = 0;
		std::vector<int> popouts_to_remove = {};
		for (auto& plr : popout_players)
		{
			bool open = true;

			ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(11, 15, 26, 255));
			ImGui::SetNextWindowSize(ImVec2(0, 0));
			ImGui::Begin(plr->Title.c_str(), &open, flags);
			ImGui::PopStyleColor();
			DrawPlayer(plr);
			ImGui::End();

			if (!open)
				popouts_to_remove.push_back(popout_count);

			popout_count++;
		}

		// Remove any players that are no longer needing to be popped out
		for (auto i = popouts_to_remove.rbegin(); i != popouts_to_remove.rend(); i++)
			popout_players.erase(popout_players.begin() + *i);

		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		// (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
		//  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
