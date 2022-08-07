// Curl sucks ass, follow this guide -> https://stackoverflow.com/questions/53861300/how-do-you-properly-install-libcurl-for-use-in-visual-studio-2017

// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "Utils/Utils.hpp"
#include "PlayerInformer/PlayerInformer.hpp"

#include <vector>
#include <thread>

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
		ImGui::Text("Name: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->Name.c_str()))
			Utils::SetClipboard(plr->Name);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 253, 131, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Display Name: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->DisplayName.c_str()))
			Utils::SetClipboard(plr->DisplayName);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(48, 200, 255, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("User Id: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->UserIdStr.c_str()))
			Utils::SetClipboard(plr->UserIdStr);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 58, 114, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Account Age: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->AccountAgeStr.c_str()))
			Utils::SetClipboard(plr->AccountAgeStr);
	
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
		pos = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(pos + 3);
		ImGui::Text("Created: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
		ImGui::PopStyleColor();
		if (ImGui::Button(plr->AccountCreatedStr.c_str()))
			Utils::SetClipboard(plr->AccountCreatedStr);
	
		if (plr->FollowUserId)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(196, 147, 255, 255));
			pos = ImGui::GetCursorPosY();
			ImGui::SetCursorPosY(pos + 3);
			ImGui::Text("Follow User Id: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
			ImGui::PopStyleColor();
			if (ImGui::Button(plr->FollowUserIdStr.c_str()))
				Utils::SetClipboard(plr->FollowUserIdStr);
	
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 188, 255));
			ImGui::Text("Follow User Id Profile: ");
			ImGui::PopStyleColor();
			if (ImGui::Button(plr->FollowUserIdProfile.c_str()))
				Utils::SetClipboard(plr->FollowUserIdProfile);

			// Find follower
			bool had_player = false;

			auto player_reader = PlayerInformer::PlayerDataReader(true);
			for (auto& other_plr : player_reader.data)
			{
				if (other_plr->UserId == plr->FollowUserId)
				{
					had_player = true;
					if (ImGui::Button("Popout Followed User"))
					{
						PopoutPlayer(other_plr);
						break;
					}
				}
			}

			if (!had_player)
				ImGui::Text("Followed user has left the game.");
		}
	}
		
	ImGui::EndChild();
	
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(253, 105, 186, 255));
	pos = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(pos + 3);
	ImGui::Text("Profile Link: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
	ImGui::PopStyleColor();

	if (ImGui::Button(plr->ProfileLink.c_str()))
		Utils::SetClipboard(plr->ProfileLink);
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

	// Save our list of tabs
	int current_tab_id = 0;
	std::vector<const char*> buttons = { "Search", "Followers", "Extra", "Credits" };

	// Save unique buffers for each search
	char search_players_buffer[64] = {};
	char search_followers_buffer[64] = {};

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

		ImGuiWindowFlags flags = 0;
		flags |= ImGuiWindowFlags_NoTitleBar;
		flags |= ImGuiWindowFlags_NoMove;
		flags |= ImGuiWindowFlags_NoCollapse;
		flags |= ImGuiWindowFlags_NoDocking;
		flags |= ImGuiWindowFlags_NoResize;

		if (informer_open)
		{
			// Chinese Government RBG Gaming Chair with built in Speaskers Player Observation Tool
			ImGui::SetNextWindowSize(ImVec2(707, 555));
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

			if (current_tab_id != 3)
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

				ImGui::BeginChildFrame(current_tab_id + 12, ImVec2(0, -1)); // Fill to bottom

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
						if (ImGui::CollapsingHeader(plr->Title.c_str()))
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

				ImGui::BeginChildFrame(current_tab_id + 12, ImVec2(0, -1)); // Fill to bottom

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
							if (ImGui::CollapsingHeader(plr->Title.c_str()))
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
			case 2: // Extra
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, -1));

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 191, 0, 255));
				ImGui::Text("Server Information:");
				ImGui::PopStyleColor();

				auto engine_reader = PlayerInformer::EngineReader();
				
				float pos = ImGui::GetCursorPosY();
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(225, 58, 114, 255));
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Place Id: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button(engine_reader.data.PlaceIdStr.c_str()))
					Utils::SetClipboard(engine_reader.data.PlaceIdStr);

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(96, 234, 90, 255));
				pos = ImGui::GetCursorPosY();
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Job Id: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
				ImGui::PopStyleColor();
				if (ImGui::Button(engine_reader.data.JobId.c_str()))
					Utils::SetClipboard(engine_reader.data.JobId);

				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 188, 255));
				pos = ImGui::GetCursorPosY();
				ImGui::SetCursorPosY(pos + 3);
				ImGui::Text("Join Script: "); ImGui::SameLine(); ImGui::SetCursorPosY(pos);
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

				ImGui::EndChildFrame();

				break;
			}
			case 3: // Credits
			{
				ImGui::BeginChildFrame(current_tab_id + 11, ImVec2(0, -1));

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
