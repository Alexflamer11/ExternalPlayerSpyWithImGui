#include "PlayerInformer.hpp"

#include "Utils/Utils.hpp"
#include "PlayerScan.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>

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
				PlayerScan::UpdatePlayerList(cached_process_handle, new_process);

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
		std::unique_lock<std::mutex> lck(mtx);
		while (!ready)
			cv.wait(lck);

		// In the case of a new process, signal the reader
		//   to recache all offsets.
		bool new_process = UpdateProcessHandle();
		if (cached_process_handle)
		{
			try
			{
				PlayerScan::UpdatePlayerList(cached_process_handle, new_process);
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