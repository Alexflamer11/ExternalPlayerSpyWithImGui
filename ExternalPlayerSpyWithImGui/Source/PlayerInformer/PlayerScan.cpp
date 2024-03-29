#include "PlayerScan.hpp"

#include "PlayerInformer/PlayerInformer.hpp"

#include "Offsets.hpp"
#include "Utils/Utils.hpp"
#include "PlayerInformer/MemoryReader/MemoryReader.hpp"

#include <stdint.h>

#include <cpr/cpr.h>
#include "nlohmann/json.hpp"

uintptr_t cached_base_address = 0;
uintptr_t cached_task_scheduler = 0;

// Cache player images, and yes copy the vector of players, will be initialized with std::thread
void CacheImages(std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> players_to_get)
{
	// https://thumbnails.roblox.com/docs#!/Avatar/get_v1_users_avatar
	if (players_to_get.size())
	{
		bool failed_to_cache = false;

		// Create a list of all players to get the images of
		std::string url = "https://thumbnails.roblox.com/v1/users/avatar?userIds=";
		for (size_t i = 0; i < players_to_get.size(); i++)
		{
			if (i)
				url += "%2C"; // Separater Roblox has on their API sample

			url += players_to_get[i]->UserIdStr;
		}
		url += "&size=250x250&format=Png&isCircular=false";

		cpr::Response response = cpr::Get(cpr::Url{ url });

		// If the response fails, set all images to not loaded
		//   to force a retry on the next refresh
		if (response.status_code != 200)
			failed_to_cache = true;
		else
		{
			try
			{
				// All the information will be present in json table like so:
				// {
				// 
				nlohmann::json data = nlohmann::json::parse(response.text);

				for (auto res : data["data"])
				{
					uint64_t user_id = res["targetId"].get<uint64_t>();

					std::string state = res["state"].get<std::string>();
					if (state != "Completed")
						printf("[!] Failed to get image link, (%lld), state: %s\n", user_id, state.c_str());
					else
					{
						std::string image_url = res["imageUrl"].get<std::string>();

						cpr::Response image_response = cpr::Get(cpr::Url{ image_url });
						if (image_response.status_code != 200)
							printf("[!] Filed to load image for id %lld, http code %i\n", user_id, image_response.status_code);
						
						// Find and set the player to their respective image
						for (auto& plr : players_to_get)
						{
							if (plr->UserId == user_id)
							{
								if (image_response.status_code == 200)
									plr->ProfilePicture = image_response.text;

								// Set fetching to false after image is set to prevent a rare
								//   race condition when player wants to refresh images to
								//   prevent a double lookup of a user unless needed.
								plr->FetchingProfilePicture = false;

								break;
							}
						}
					}
				}
			}
			catch (nlohmann::json::parse_error& e)
			{
				printf("[!] nlohmann parse error: %s\n", e.what());

				failed_to_cache = true;
			}
			catch (nlohmann::json::type_error& e)
			{
				printf("[!] nlohmann type error: %s\n", e.what());

				failed_to_cache = true;
			}
		}

		// If any (major) part of the request failed, attempt to recache
		//   any missing players on the next refresh
		if (failed_to_cache)
			for (auto plr : players_to_get)
				plr->FetchingProfilePicture = false;
	}
}

class PropertyPtr
{
	HANDLE Process;
	uintptr_t Ptr;

public:

	PropertyPtr(HANDLE process, uintptr_t ptr) : Process(process), Ptr(ptr) {}

	bool IsValidProperty()
	{
		return MemoryReader::Read<uintptr_t>(Process, Ptr + 8);
	}

	// prop->type
	int Type()
	{
		return MemoryReader::Read<int>(Process, Ptr + 16);
	}

	// prop->member->name;
	std::string Name()
	{
		uintptr_t member = MemoryReader::Read<uintptr_t>(Process, Ptr + 8);
		if (!member || Type() != 0)
			return "";

		return MemoryReader::ReadStringPtr(Process, member + Offset::PropertyName);
	}

	// prop->member->get();
	uintptr_t PropertyGetter()
	{
		uintptr_t member = MemoryReader::Read<uintptr_t>(Process, Ptr + 8);
		if (!member || Type() != 0)
			return 0;

		uintptr_t prop_vftable = MemoryReader::Read<uintptr_t>(Process, member + Offset::PropertyVFTable);
		if (prop_vftable)
			return MemoryReader::Read<uintptr_t>(Process, prop_vftable + Offset::PropertyVFTableGet);

		return 0;
	}
};

struct PropertyList
{
	struct Iterator
	{
		using iterator_category = std::forward_iterator_tag;

		Iterator(HANDLE process, uintptr_t ptr) : Process(process), Ptr(ptr) {};

		PropertyPtr operator*() { return PropertyPtr(Process, Ptr); }
		Iterator& operator++() { Ptr += 24; return *this; } // size of class
		friend bool operator== (const Iterator& a, const Iterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator!= (const Iterator& a, const Iterator& b) { return a.Ptr != b.Ptr; };

	private:

		HANDLE Process;
		uintptr_t Ptr;
	};

	Iterator begin() { return Iterator(Process, MemoryReader::Read<uintptr_t>(Process, PropertyData + Offset::PropertyStart)); }
	Iterator end() { return Iterator(Process, MemoryReader::Read<uintptr_t>(Process, PropertyData + Offset::PropertyEnd)); }

	PropertyList(HANDLE process, uintptr_t instance) : Process(process), PropertyData(0)
	{
		PropertyData = MemoryReader::Read<uintptr_t>(process, instance + Offset::PropertyList);
	}

private:

	HANDLE Process;
	uintptr_t PropertyData;
};

struct ChildList
{
	struct Iterator
	{
		using iterator_category = std::forward_iterator_tag;

		Iterator(HANDLE process, uintptr_t ptr) : Process(process), Ptr(ptr) {};

		uintptr_t operator*()
		{
			uintptr_t child = 0;
			MemoryReader::Read(Process, Ptr, &child); // do not throw an error
			return child;
		}
		Iterator& operator++() { Ptr += sizeof(std::shared_ptr<void*>); return *this; } // size of std::shared_ptr
		friend bool operator== (const Iterator& a, const Iterator& b) { return a.Ptr == b.Ptr; };
		friend bool operator!= (const Iterator& a, const Iterator& b) { return a.Ptr != b.Ptr; };

	private:

		HANDLE Process;
		uintptr_t Ptr;
	};

	Iterator begin() { return Iterator(Process, MemoryReader::Read<uintptr_t>(Process, ChildrenList)); }
	Iterator end() { return Iterator(Process, MemoryReader::Read<uintptr_t>(Process, ChildrenList + sizeof(void*))); } // std::vector

	ChildList(HANDLE process, uintptr_t instance) : Process(process), ChildrenList(0)
	{
		ChildrenList = MemoryReader::Read<uintptr_t>(process, instance + Offset::Children);
	}

private:

	HANDLE Process;
	uintptr_t ChildrenList;
};

bool ComparePlayerSort(std::shared_ptr<PlayerInformer::PlayerInformation>& i1, std::shared_ptr<PlayerInformer::PlayerInformation>& i2)
{
	return i1->LowerName.compare(i2->LowerName) < 1;
}

bool SuccessSinceUpdate = true; // As force update will not always be present, so make sure joins work properly
bool DisplayedVersionWarning = false; // If a new process is found, and the cached version is not the same, warn the user

uintptr_t CachedDataModel = 0; // Cache DataModel so we can properly handle joins when the player teleports
void PlayerScan::UpdatePlayerList(HANDLE process, bool force_update, bool new_process)
{
	if (new_process)
		DisplayedVersionWarning = false;

	if (force_update)
		SuccessSinceUpdate = false;

	// Rescan for task scheduler
	if (!cached_task_scheduler || force_update)
	{
		if (!cached_task_scheduler)
			puts("[*] No task scheduler cached, rescanning.");
		else
			puts("[*] New process, rescanning for task scheduler.");

		cached_task_scheduler = 0;
		cached_base_address = 0;

		MODULEINFO mod = MemoryReader::GetProcessInfo(process);
		uintptr_t start = (uintptr_t)mod.lpBaseOfDll;
		uintptr_t end = start + mod.SizeOfImage;

		cached_base_address = start;

		// E8 ? ? ? ? F2 0F ? B8 00 01 00 00
		uintptr_t task_scheduler_call = (uintptr_t)MemoryReader::ScanProcess(process, "\xE8\x00\x00\x00\x00\xF2\x0F\x00\xB8\x00\x01\x00\x00", "x????xx?xxxxx", (uint8_t*)start, (uint8_t*)end);
		if (task_scheduler_call)
		{
			// Call = E8 XX XX XX XX
			// Destination = Address + Offset (XX XX XX XX) + sizeof(instr) (5)
			uintptr_t task_scheduler_start = task_scheduler_call + MemoryReader::Read<uint32_t>(process, task_scheduler_call + 1) + 5;

			printf("[*] Task scheduler function at: %llX\n", task_scheduler_start);

			// 48 8B ? ? ? ? ? 48 ? ? ? 5B C3
			// mov eax, SINGLETON
			// add rsp, ?
			// pop ?
			// ret
			const char sig[] = "\x48\x8B\x00\x00\x00\x00\x00\x48\x00\x00\x00\x5B\xC3";
			const char mask[] = "xx?????x???xx";
			
			uint8_t buffer[0x150];
			if (MemoryReader::Read(process, task_scheduler_start, buffer, sizeof(buffer)))
			{
				uint8_t* first = Utils::Scan(sig, mask, (uintptr_t)buffer, (uintptr_t)buffer + sizeof(buffer));
				if (first && first - buffer >= sizeof(sig))
				{
					uintptr_t first_addr = first - buffer + task_scheduler_start + *(uint32_t*)(first + 3) + 7;
					if (MemoryReader::Read<uintptr_t>(process, first_addr))
						cached_task_scheduler = first_addr;
				}

				if (!cached_task_scheduler)
				{
					puts("[*] First scheduler failed, getting second.");

					size_t offset = first - buffer + 1;
					uint8_t* second = Utils::Scan(sig, mask, (uintptr_t)buffer + offset, (uintptr_t)buffer + sizeof(buffer) + offset);
					if (second && second - buffer >= sizeof(sig))
					{
						uintptr_t second_addr = second - buffer + task_scheduler_start + *(uint32_t*)(second + 3) + 7;
						if (MemoryReader::Read<uintptr_t>(process, second_addr))
							cached_task_scheduler = second_addr;
					}
				}
			}
		}

		if (cached_task_scheduler)
			printf("[*] Found task scheduler: %llX\n", MemoryReader::Read<uintptr_t>(process, cached_task_scheduler));
	}

	uintptr_t data_model = 0;

	if (!cached_task_scheduler)
		throw std::exception("task scheduler address was not set");

	// Set all cached entries to 0
	// Would happen on game version changes (if offset change)
	// Or user wants to recache all offsets
	if (!SuccessSinceUpdate)
	{
		Offset::CachedFileVersion = "";

		Offset::DataModel::PlaceId = 0;
		Offset::DataModel::JobId = 0;

		Offset::Player::DisplayName = 0;
		Offset::Player::UserId = 0;
		Offset::Player::AccountAge = 0;
		Offset::Player::FollowUserId = 0;
		Offset::Player::TeleportedIn = 0;
	}

	// Display a warning on new versions
	if (!DisplayedVersionWarning && Offset::CachedFileVersion.size())
	{
		std::string current_version = PlayerInformer::GetVersionInfo(process);
		if (current_version.size())
		{
			if (current_version != Offset::CachedFileVersion)
				printf("[WARNING] File versions are different: %s != %s\n", Offset::CachedFileVersion.c_str(), current_version.c_str());

			DisplayedVersionWarning = true;
		}
	}

	uintptr_t singleton;
	MemoryReader::Read<uintptr_t>(process, cached_task_scheduler, &singleton);
	if (!singleton)
		return; // Silent return if task scheduler does not exist

	// Task scheduler is needed to find the WaitingHybridScriptsJob
	//   to get ScriptContext, whose parent if DataModel.
	uintptr_t list_start = MemoryReader::Read<size_t>(process, singleton + Offset::SingletonListStart);
	uintptr_t list_end = MemoryReader::Read<size_t>(process, singleton + Offset::SingletonListEnd);

	for (uintptr_t i = list_start; i != list_end; i += sizeof(std::shared_ptr<void*>)) // sizeof(shared_ptr<>())
	{
		uintptr_t ptr = MemoryReader::Read<uintptr_t>(process, i);
		if (ptr)
		{
			std::string job_name = MemoryReader::ReadString(process, ptr + Offset::JobName);
			if (job_name == "WaitingHybridScriptsJob")
			{
				uintptr_t script_context = MemoryReader::Read<uintptr_t>(process, ptr + Offset::ScriptContextFromJob);
				data_model = MemoryReader::Read<uintptr_t>(process, script_context + Offset::Parent);
				
				break;
			}
		}
	}

	if (!data_model)
	{
		puts("[!] Failed to get DataModel.");
		return;
	}
	else
	{
		if (CachedDataModel && CachedDataModel != data_model)
			puts("[!] Teleported to a new server.");

		// In case of a new instance, get all the DataModel offsets on the first sweep
		if (!Offset::DataModel::PlaceId || !Offset::DataModel::JobId)
		{
			PropertyList test(process, data_model);
			for (PropertyPtr ptr : test)
			{
				if (ptr.IsValidProperty() && ptr.Type() == 0)
				{
					uintptr_t getter = ptr.PropertyGetter();
					std::string prop_name = ptr.Name();
					
					if (prop_name == "PlaceId")
						Offset::DataModel::PlaceId = MemoryReader::Read<uint32_t>(process, getter + 3);
					else if (prop_name == "JobId")
						Offset::DataModel::JobId = MemoryReader::Read<uint32_t>(process, getter + 12);
				}
			}

			printf("[*] DataModel Offsets\n"
				"  PlaceId: %i (0x%llX)\n"
				"  JobId: %i (0x%llX)\n",
				Offset::DataModel::PlaceId, Offset::DataModel::PlaceId,
				Offset::DataModel::JobId, Offset::DataModel::JobId
			);
		}

		// Would only fail in the case of a read during teleport
		//   or a major update shifting the function offsets
		if (!Offset::DataModel::PlaceId || !Offset::DataModel::JobId)
		{
			puts("[!] Failed to get DataModel offsets.");
			return;
		}

		// Get engine information
		auto engine_info = PlayerInformer::EngineReader();
	
		// Keep trying to get the server information until all of it is successful
		if (engine_info.data.JobId == "UNKNOWN" || !SuccessSinceUpdate || CachedDataModel != data_model)
		{
			// If any of the reads fail, set them to unknown to
			//   prevent ImGui from crashing.
			engine_info.data.PlaceId = 0;
			engine_info.data.PlaceIdStr = "UNKNOWN";
			engine_info.data.JobId = "UNKNOWN";
			
			uintptr_t place_id_addr = MemoryReader::Read<size_t>(process, data_model - 320 + Offset::DataModel::PlaceId);
			
			uint32_t addr32 = place_id_addr;
			uint32_t place_id_encrypted_lower = MemoryReader::Read<uint32_t>(process, place_id_addr);
			uint32_t place_id_encrypted_upper = MemoryReader::Read<uint32_t>(process, place_id_addr + 4);
			
			size_t place_id = (uintptr_t)(place_id_encrypted_lower - addr32) | ((uintptr_t)(place_id_encrypted_upper - addr32) << 32);
			
			std::string job_id = MemoryReader::ReadString(process, data_model - 320 + Offset::DataModel::JobId);

			engine_info.data.PlaceId = place_id;
			engine_info.data.PlaceIdStr = std::to_string(place_id);

			// Only overwite unknown if a job id was obtained
			if (!job_id.empty())
				engine_info.data.JobId = job_id;

			printf("[*] Game Info: %s - %s\n", engine_info.data.PlaceIdStr.c_str(), job_id.c_str());
		}

		int player_frame_count = 0; // Id for rendering player frames

		std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> player_images_to_cache = {};

		ChildList data_model_children(process, data_model);
		for (uintptr_t data_model_child : data_model_children)
		{
			if (data_model_child && MemoryReader::ClassName(process, data_model_child, cached_base_address) == "Players")
			{
				auto player_reader = PlayerInformer::PlayerDataReader();
				player_reader.data.clear();

				// Refresh player count
				if (!SuccessSinceUpdate || CachedDataModel != data_model)
				{
					engine_info.data.PlayerCount = 0;
					engine_info.data.PlayersJoined = 0;
					engine_info.data.PlayersLeft = 0;

					engine_info.data.PlayerCountCache = "0 Players | 0 Joined | 0 Left";

					// In the case of a teleport, clear all cached data
					player_reader.data.clear();
					player_reader.cache.clear();
				}

				// Keep track on how many players joined the game since the last lookup
				size_t new_player_count = 0;

				// Start gc sweep
				// Any players not referenced at the end will be removed
				for (auto plr : player_reader.cache)
					plr->WasReferenced = false;

				ChildList players(process, data_model_child);
				for (uintptr_t plr : players)
				{
					if (plr && MemoryReader::ClassName(process, plr, cached_base_address) == "Player")
					{
						if (!Offset::Player::DisplayName || !Offset::Player::UserId
							|| !Offset::Player::AccountAge || !Offset::Player::FollowUserId)
						{
							PropertyList prop_list(process, plr);
							for (PropertyPtr ptr : prop_list)
							{
								if (ptr.IsValidProperty() && ptr.Type() == 0)
								{
									uintptr_t getter = ptr.PropertyGetter();
									std::string prop_name = ptr.Name();

									if (prop_name == "DisplayName")
										Offset::Player::DisplayName = MemoryReader::Read<uint32_t>(process, getter + 12);
									else if (prop_name == "UserId")
										Offset::Player::UserId = MemoryReader::Read<uint32_t>(process, getter + 9);
									else if (prop_name == "AccountAge")
										Offset::Player::AccountAge = MemoryReader::Read<uint32_t>(process, getter + 2);
									else if (prop_name == "FollowUserId")
										Offset::Player::FollowUserId = MemoryReader::Read<uint32_t>(process, getter + 3);
									else if (prop_name == "TeleportedIn")
										Offset::Player::TeleportedIn = MemoryReader::Read<uint32_t>(process, getter + 3);
								}
							}

							printf("[*] Offsets\n"
								"  DisplayName: %i (0x%llX)\n"
								"  UserId: %i (0x%llX)\n"
								"  AccountAge: %i (0x%llX)\n"
								"  FollowUserId: %i (0x%llX)\n"
								"  TeleportedIn: %i (0x%llX)\n",
								Offset::Player::DisplayName, Offset::Player::DisplayName,
								Offset::Player::UserId, Offset::Player::UserId,
								Offset::Player::AccountAge, Offset::Player::AccountAge,
								Offset::Player::FollowUserId, Offset::Player::FollowUserId,
								Offset::Player::TeleportedIn, Offset::Player::TeleportedIn
							);
						}

						// Properties will only fail to get in the case of a teleport
						//   or a major update causing offsets to shift.
						// LocalPlayer is always the first (and thus a safe) entry.
						if (!Offset::Player::DisplayName || !Offset::Player::UserId
							|| !Offset::Player::AccountAge || !Offset::Player::FollowUserId
							|| !Offset::Player::TeleportedIn)
						{
							puts("[!] Failed to get Player offsets.");
							return;
						}

						// If a single player errors, still try to read the rest
						try
						{
							uint64_t user_id = MemoryReader::Read<uint64_t>(process, plr + Offset::Player::UserId);

							// Check if the player exists, if so we will get the cached version
							std::shared_ptr<PlayerInformer::PlayerInformation> player;
							for (auto& cached_plr : player_reader.cache)
							{
								if (cached_plr->UserId == user_id)
								{
									player = cached_plr;

									// If a player was in the process of leaving, and they
									//   end up rejoining the server, mark them as green again,
									//   otherwise mark as none and move on.
									if (player->JoinStatus == JOIN_STATUS::LEFT)
										player->JoinStatus = JOIN_STATUS::JOINED;
									else
										player->JoinStatus = JOIN_STATUS::NONE;
									
									break;
								}
							}

							// Player was not cached, create and add the player to the cache
							if (!player.get())
							{
								// A special note for this function is, all player information
								//   is stringified for ImGui use to prevent these strings
								//   from being created every frame only to be immediately
								//   destroyed.
								// Additionally, nothihng that is displayed by ImGui is empty
								//   to prevent random crashes on failed information grabbing.

								player = std::make_shared<PlayerInformer::PlayerInformation>();

								// If this is the first scan of a server, set the join status to none
								player->JoinStatus = (!SuccessSinceUpdate || CachedDataModel != data_model) ? JOIN_STATUS::NONE : JOIN_STATUS::JOINED;

								// Name
								player->Name = MemoryReader::ReadStringPtr(process, plr + Offset::Name);
								if (player->Name.empty())
									player->Name = "UNKNOWN";
								player->LowerName = player->Name;
								std::transform(player->LowerName.begin(), player->LowerName.end(), player->LowerName.begin(),
									[](unsigned char c) { return std::tolower(c); });

								// DisplayName
								player->DisplayName = MemoryReader::ReadString(process, plr + Offset::Player::DisplayName);
								if (player->DisplayName.empty())
									player->DisplayName = "UNKNOWN";
								player->LowerDisplayName = player->DisplayName;
								std::transform(player->LowerDisplayName.begin(), player->LowerDisplayName.end(), player->LowerDisplayName.begin(),
									[](unsigned char c) { return std::tolower(c); });

								// UserId
								player->UserId = user_id;
								player->UserIdStr = std::to_string(player->UserId);

								// Set cached frame title
								player->Title = player->Name;
								player->Title += " - ";
								player->Title += player->DisplayName;
								player->Title += " - ";
								player->Title += player->UserIdStr;
								player->Title += "##PLAYER_"; // Unique player lookup
								player->Title += std::to_string(player_frame_count);

								// Cache a unique player popout name too
								player->UniquePopoutName = "Popout Player Information";
								player->UniquePopoutName += "##";
								player->UniquePopoutName += std::to_string(player_frame_count++);

								// AccountAge
								player->AccountAge = MemoryReader::Read<uint32_t>(process, plr + Offset::Player::AccountAge);
								player->AccountAgeStr = std::to_string(player->AccountAge) + (player->AccountAge == 1 ? " day" : " days");

								size_t years = player->AccountAge / 365;
								size_t months = (player->AccountAge % 364) / 30;
								size_t days = (player->AccountAge % 364) % 30;

								// Prettify the account age results, only show results if not 0.
								//   (days will always show, even if 0 days ago)
								player->AccountCreatedStr = "Created ";

								if (years)
								{
									player->AccountCreatedStr += std::to_string(years);
									if (years == 1)
										player->AccountCreatedStr += " year, ";
									else
										player->AccountCreatedStr += " years, ";
								}

								if (months)
								{
									player->AccountCreatedStr += std::to_string(months);
									if (months == 1)
										player->AccountCreatedStr += " month, ";
									else
										player->AccountCreatedStr += " months, ";
								}

								player->AccountCreatedStr += std::to_string(days) + (days == 1 ? " day ago" : " days ago");

								// TeleportedIn
								if (Offset::Player::TeleportedIn)
								{
									player->TeleportedIn = MemoryReader::Read<bool>(process, plr + Offset::Player::TeleportedIn);
									player->TeleportedInStr = player->TeleportedIn ? "true" : "false";
								}
								else
								{
									player->TeleportedIn = false;
									player->TeleportedInStr = "UNKNWON";
								}

								// FollowUserId
								player->CachedFollowUser = 0; // Set in draw
								player->FollowUserId = MemoryReader::Read<uint64_t>(process, plr + Offset::Player::FollowUserId);
								player->FollowUserIdStr = std::to_string(player->FollowUserId);
								player->FollowUserIdProfile = "https://www.roblox.com/users/";
								player->FollowUserIdProfile += std::to_string(player->FollowUserId);
								player->FollowUserIdProfile += "/profile";

								// ProfileLink
								player->ProfileLink = "https://www.roblox.com/users/";
								player->ProfileLink += std::to_string(player->UserId);
								player->ProfileLink += "/profile";

								// Profile image cache
								player->ImageId = 0;
								player->ProfilePicture = "";
								player->FetchingProfilePicture = false;

								player_reader.cache.push_back(player); // Save for the gc cache

								new_player_count++; // Save our player count
							}

							// If the image was not loaded (or failed to load), attempt to load the image
							if (player->ProfilePicture.empty() && !player->FetchingProfilePicture)
							{
								player->FetchingProfilePicture = true;
								player_images_to_cache.push_back(player);
							}

							// Prevent player from being deleted during the gc sweep
							player->WasReferenced = true;

							// Add player to the final list
							player_reader.data.push_back(player);
						}
						catch (std::exception& e)
						{
							printf("[!] Error trying to read a player: %s\n", e.what());
						}
					}
				}

				// Find the followed user if they are in the server or rejoined
				//    the server and then cache their info for future lookups.
				// This will keep players from being deleted, and then subsiquently
				//    duplicated if they do rejoin, but that is not a concern at all
				//    as popout player already handles duplicate user id's.
				for (auto plr : player_reader.data)
				{
					if (!plr->CachedFollowUser.get())
					{
						for (auto& other_plr : player_reader.data)
						{
							if (other_plr->UserId == plr->FollowUserId)
							{
								plr->CachedFollowUser = other_plr;
								break;
							}
						}
					}
				}

				// End of gc sweep, any unreferenced players will be freed
				int to_remove_index = 0;
				std::vector<int> player_cache_to_remove = {};
				for (auto plr : player_reader.cache)
				{
					if (!plr->WasReferenced)
					{
						// Keep players for at least one more sweep to know
						//    who left the game since the last recache.
						if (plr->JoinStatus == JOIN_STATUS::LEFT)
						{
							plr->JoinStatus = JOIN_STATUS::GONE; // A player may have them still cached
							player_cache_to_remove.push_back(to_remove_index);
						}
						else
							plr->JoinStatus = JOIN_STATUS::LEFT;
					}

					to_remove_index++;
				}

				// Finally delete the players that left
				for (auto i = player_cache_to_remove.rbegin(); i != player_cache_to_remove.rend(); i++)
					player_reader.cache.erase(player_reader.cache.begin() + *i);

				// Reset all info
				engine_info.data.PlayerCount = 0;
				engine_info.data.PlayersJoined = 0;
				engine_info.data.PlayersLeft = 0;

				// Loop through and find the players that fit in each category
				for (auto plr : player_reader.cache)
				{
					// Increase player count on existing players or joining players
					if (plr->JoinStatus == JOIN_STATUS::NONE || plr->JoinStatus == JOIN_STATUS::JOINED)
						engine_info.data.PlayerCount++;

					if (plr->JoinStatus == JOIN_STATUS::JOINED)
						engine_info.data.PlayersJoined++;
					else if (plr->JoinStatus == JOIN_STATUS::LEFT)
					{
						engine_info.data.PlayersLeft++;

						// Make sure to re-add the player to the player list again
						//   as they no longer exist in the server but we still want
						//   to display them one last time.
						player_reader.data.push_back(plr);
					}
				}

				// Set the new cached string
				engine_info.data.PlayerCountCache = std::to_string(engine_info.data.PlayerCount);
				engine_info.data.PlayerCountCache += engine_info.data.PlayerCount == 1 ? " Player | " : " Players | ";
				engine_info.data.PlayerCountCache += std::to_string(engine_info.data.PlayersJoined);
				engine_info.data.PlayerCountCache += " Joined | ";
				engine_info.data.PlayerCountCache += std::to_string(engine_info.data.PlayersLeft);
				engine_info.data.PlayerCountCache += " Left";

				// Sort player list
				std::sort(player_reader.data.begin(), player_reader.data.end(), ComparePlayerSort);

				break;
			}
		}

		// Make a request to get cache player images
		std::thread(CacheImages, player_images_to_cache).detach();

		// Update our custom flags
		SuccessSinceUpdate = true;
		CachedDataModel = data_model;
	}
}