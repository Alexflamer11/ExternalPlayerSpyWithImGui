#include "PlayerInformer.hpp"

#include "Utils/Utils.hpp"
#include "PlayerScan.hpp"
#include "Offsets.hpp"

#include "nlohmann/json.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <psapi.h>


bool PlayerInformer::WroteEngineOffsets = false;

// Condition variables for thread safe reading
std::mutex mtx;
std::condition_variable cv;
bool ready = false;

// Currently rendered players
std::mutex player_data_lock;
std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> player_data;

// Cached player list so players are not created and profiles queried more than once (unless a query fails)
std::vector<std::shared_ptr<PlayerInformer::PlayerInformation>> cached_players;

// Actual engine information
std::mutex engine_data_lock;
PlayerInformer::EngineData engine_data;

// Save process ids to know when to do a cached lookup or a full recache
DWORD cached_proccess_id = 0;
HANDLE cached_process_handle = 0;

PlayerInformer::PlayerDataReader::PlayerDataReader(bool is_unsafe)
	: unsafe(is_unsafe)
	, data(player_data)
	, cache(cached_players)
{
	if (!is_unsafe)
		player_data_lock.lock();
}

PlayerInformer::PlayerDataReader::~PlayerDataReader()
{
	if (!unsafe)
		player_data_lock.unlock();
}

PlayerInformer::EngineReader::EngineReader(bool is_unsafe)
	: unsafe(is_unsafe)
	, data(engine_data)
{
	if (!is_unsafe)
		engine_data_lock.lock();
}

PlayerInformer::EngineReader::~EngineReader()
{
	if (!unsafe)
		engine_data_lock.unlock();
}

std::string PlayerInformer::GetVersionInfo(HANDLE open_process)
{
	// https://stackoverflow.com/questions/1933113/c-windows-how-to-get-process-path-from-its-pid
	// (answer) https://stackoverflow.com/a/1933140
	std::string version = "";

	wchar_t module_file_name[MAX_PATH];
	if (GetModuleFileNameExW(open_process, NULL, module_file_name, MAX_PATH) == 0)
		return version;

	DWORD  verHandle = 0;
	UINT   size = 0;
	LPBYTE lpBuffer = NULL;
	DWORD  verSize = GetFileVersionInfoSizeW(module_file_name, &verHandle);
	if (verSize != NULL)
	{
		LPSTR verData = new char[verSize];
		if (GetFileVersionInfoW(module_file_name, verHandle, verSize, verData))
		{
			if (VerQueryValueW(verData, L"\\", (VOID FAR * FAR*) & lpBuffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
					if (verInfo->dwSignature == 0xfeef04bd)
					{
						// Doesn't matter if you are on 32 bit or 64 bit,
						// DWORD is always 32 bits, so first two revision numbers
						// come from dwFileVersionMS, last two come from dwFileVersionLS
						version += std::to_string((verInfo->dwFileVersionMS >> 16) & 0xffff);
						version += ".";
						version += std::to_string((verInfo->dwFileVersionMS >> 0) & 0xffff);
						version += ".";
						version += std::to_string((verInfo->dwFileVersionLS >> 16) & 0xffff);
						version += ".";
						version += std::to_string((verInfo->dwFileVersionLS >> 0) & 0xffff);
					}
				}
			}
		}
		delete[] verData;
	}

	return version;
}

bool LoadOffsets()
{
	std::ifstream file("Offsets.json", std::ios::binary | std::ios::ate);
	if (!file.is_open())
		return false;

	size_t file_size = file.tellg();
	if (!file_size)
		return false;

	file.seekg(0, std::ios::beg);

	auto buffer = std::make_unique<char[]>(file_size);
	if (!buffer)
		return false;

	file.read(buffer.get(), file_size);
	file.close();

	auto result = nlohmann::json::parse(buffer.get(), nullptr, false); // ignore exceptions
	if (result.is_discarded() || result.is_null())
	{
		puts("[!] Failed to parse Offsets.json.");
		return false;
	}

	Offset::CachedFileVersion = result["FileVersion"];

	auto data_model = result["DataModel"];
	if (data_model.is_null())
	{
		puts("[!] Failed to get DataModel when parsing offsets file.");
		return false;
	}

	Offset::DataModel::PlaceId = data_model.value("PlaceId", 0);
	Offset::DataModel::JobId = data_model.value("JobId", 0);

	if (!Offset::DataModel::PlaceId || !Offset::DataModel::JobId)
	{
		puts("[!] Failed to get DataModel offsets when parsing offsets file.");
		return false;
	}

	auto player = result["Player"];
	if (player.is_null())
	{
		puts("[!] Failed to get Player when parsing offsets file.");
		return false;
	}

	Offset::Player::DisplayName = player.value("DisplayName", 0);
	Offset::Player::UserId = player.value("UserId", 0);
	Offset::Player::AccountAge = player.value("AccountAge", 0);
	Offset::Player::FollowUserId = player.value("FollowUserId", 0);
	Offset::Player::TeleportedIn = player.value("TeleportedIn", 0);

	if (!Offset::Player::DisplayName || !Offset::Player::UserId
		|| !Offset::Player::AccountAge || !Offset::Player::FollowUserId)
	{
		puts("[!] Failed to get Player offsets when parsing offsets file.");
		return false;
	}

	// Not explicitly required and can fail decently, just ignore
	if (!Offset::Player::TeleportedIn)
		puts("[*] No teleported in offset was found.");

	printf("[*] Cached data that was loaded\n"
		"  FileVersion: %s\n"
		"  DataModel:\n"
		"    PlaceId: %i (%llX)\n"
		"    JobId: %i (%llX)\n"
		"  Player:\n"
		"    DisplayName: %i (%llX)\n"
		"    UserId: %i (%llX)\n"
		"    AccountAge: %i (%llX)\n"
		"    FollowUserId: %i (%llX)\n"
		"    TeleportedIn: %i (%llX)\n",
		Offset::CachedFileVersion.c_str(),
		Offset::DataModel::PlaceId, Offset::DataModel::PlaceId,
		Offset::DataModel::JobId, Offset::DataModel::JobId,
		Offset::Player::DisplayName, Offset::Player::DisplayName,
		Offset::Player::UserId, Offset::Player::UserId,
		Offset::Player::AccountAge, Offset::Player::AccountAge,
		Offset::Player::FollowUserId, Offset::Player::FollowUserId,
		Offset::Player::TeleportedIn, Offset::Player::TeleportedIn
	);

	return true;
}

bool WriteOffsets()
{
	// Only write offsets if all are found
	if (!Offset::CachedFileVersion.size())
		Offset::CachedFileVersion = PlayerInformer::GetVersionInfo(cached_process_handle);

	if (!Offset::CachedFileVersion.size())
	{
		puts("[!] Failed to cache version data.");
		return false;
	}

	if (!Offset::DataModel::PlaceId || !Offset::DataModel::JobId)
		return false;

	if (!Offset::Player::DisplayName || !Offset::Player::UserId
		|| !Offset::Player::AccountAge || !Offset::Player::FollowUserId)
		return false;

	nlohmann::json result = {
		{ "FileVersion", Offset::CachedFileVersion },
		{ "DataModel", {
			{ "PlaceId", Offset::DataModel::PlaceId },
			{ "JobId", Offset::DataModel::JobId }
		}},

		{ "Player", {
			{ "DisplayName", Offset::Player::DisplayName },
			{ "UserId", Offset::Player::UserId },
			{ "AccountAge", Offset::Player::AccountAge },
			{ "FollowUserId", Offset::Player::FollowUserId },
			{ "TeleportedIn", Offset::Player::TeleportedIn }
		}}
	};

	std::string json_string = result.dump(1, '\t', false, nlohmann::json::error_handler_t::ignore); // ignore exceptions
	if (!json_string.size())
		return false;

	std::ofstream file("Offsets.json", std::ios::binary | std::ios::trunc);
	file.write(json_string.c_str(), json_string.size());
	file.close();

	return true;
}

// Find the most recent process handle
//   and if it was different than the last saved handle,
//   tell the code after to recache all offsets 
bool UpdateProcessHandle()
{
	HANDLE old_handle = cached_process_handle;

	DWORD new_process_id = Utils::FindProcessId(L"RobloxPlayerBeta.exe");
	if (new_process_id == 0)
		puts("[!] Failed to find process.");
	else if (new_process_id != cached_proccess_id)
	{
		cached_proccess_id = new_process_id;

		// Close the handle to the last process if it still exists, or if a new process overwrites this
		if (cached_process_handle)
			CloseHandle(cached_process_handle);

		HANDLE process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, new_process_id);
		if (process_handle)
			cached_process_handle = process_handle;
		else
			puts("[!] Failed to open process.");
	}

	return old_handle != cached_process_handle;
}

void Watcher()
{
	PlayerInformer::WroteEngineOffsets = LoadOffsets();

	// What happens here is when the program first loads,
	//   if the place information has not yet loaded, wait
	//   until the game loads, and then proceed to wait ~5
	//   seconds to let all players have a chance to load.
	//   This may have issues on the first cache for low
	//   end computers, but overall is a decent solution.

	size_t exception_count = 0; // Break after 3 exceptions
	bool need_long_sleep = false; // Wait a few seconds before getting players if game is loading

	// This loop will run until we populate the player
	//   information once. If the server information has
	//   not yet been loaded, wait ~5 seconds after server
	//   information has been grabbed before trying to
	//   cache player information.
	while (true)
	{
		bool failed = false; // Do we need to wait a few seconds before getting player data?

		bool new_process = UpdateProcessHandle();
		if (cached_process_handle)
		{
			// Reset search parameters in case of a new process
			if (new_process)
			{
				failed = false;
				need_long_sleep = false;
			}

			try
			{
				PlayerScan::UpdatePlayerList(cached_process_handle, !PlayerInformer::WroteEngineOffsets, new_process);

				if (engine_data.PlaceId)
				{
					// In the case of the server information not being fully
					//   loaded, signal await a short time before retrying.
					if (engine_data.JobId == "UNKNOWN")
						failed = true;
					else if (need_long_sleep)
						std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait a few seconds before retrying
				}
				else
					failed = true;

				// No more fails?
				if (!failed)
					break;
			}
			catch (std::exception& e)
			{
				printf("[!] Error updating player list: %s\n", e.what());

				// If there are >10 (read) exceptions, just cancel
				//   the loop and the player can get the player
				//   information when everything has been loaded.
				exception_count++;
				if (exception_count > 10)
					break;

				failed = true;
			}
		}

		// If any part fails, chances are the server has not
		//   yet fully loaded and thus tell the watcher
		//   to wait ~5 seconds.
		if (failed)
			need_long_sleep = true;

		// If anything failed or we failed to find a process,
		//   wait a second before trying again to let more
		//   information (or the process) load.
		if (failed || !cached_process_handle)
			std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	if (exception_count > 10)
		puts("[!] Too many exceptions while trying to auto read on startup.");
	else
	{
		// No errors, hide console
		// In the case the information did not load on the
		//   first run, keep the console open for any
		//   possible error handling information.
		std::thread([]() -> void
			{
				puts("[*] Auto hiding console in 3 seconds...");
				std::this_thread::sleep_for(std::chrono::seconds(3));
				Utils::HideConsole();
			}
		).detach();
	}

	// Once the player data has been cached for the first time,
	//   create the main watcher loop that waits for any requests
	//   to recache the data.
	while (true)
	{
		// Write offsets whenever the player would like to force cache
		if (!PlayerInformer::WroteEngineOffsets && cached_process_handle)
			PlayerInformer::WroteEngineOffsets = WriteOffsets();

		std::unique_lock<std::mutex> lck(mtx);
		while (!ready)
			cv.wait(lck);
		
		// Engine offsets are only updated when the user requests a recache now
		bool new_process = UpdateProcessHandle();
		if (cached_process_handle)
		{
			try
			{
				PlayerScan::UpdatePlayerList(cached_process_handle, !PlayerInformer::WroteEngineOffsets, new_process);
			}
			catch (std::exception& e)
			{
				printf("[!] Error was thrown during cache update: %s\n", e.what());
			}
		}

		ready = false;
	}
}

void PlayerInformer::RequestRefresh()
{
	std::unique_lock<std::mutex> lck(mtx);

	if (!ready)
	{
		ready = true;

		player_data_lock.lock();
		player_data.clear();
		player_data_lock.unlock();

		cv.notify_all();
	}
}

void PlayerInformer::StartWatcher()
{
	// Engine information needs to be set to a non empty
	//   string or ImGui will crash when trying to read.
	engine_data.JobId = "UNKNOWN";
	engine_data.PlaceId = 0;
	engine_data.PlaceIdStr = "UNKNOWN";

	engine_data.PlayerCount = 0;
	engine_data.PlayersJoined = 0;
	engine_data.PlayersLeft = 0;
	engine_data.PlayerCountCache = "0 Players | 0 Joined | 0 Left"; // Cannot be "" or ImGui will crash

	std::thread(Watcher).detach();
}