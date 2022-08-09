#pragma once

#include <string>
#include <memory>
#include <vector>

enum class JOIN_STATUS
{
	NONE,
	JOINED,
	LEFT,
};

namespace PlayerInformer
{
	// Information relating to a player (and cache string counterparts)
	struct PlayerInformation
	{
		JOIN_STATUS JoinStatus; // To know if the player just joined, left, or already was here

		// Cache the string variants to prevent creating them every frame
		std::string Title;
		std::string UniquePopoutName;

		std::string Name;
		std::string LowerName;
		std::string DisplayName;
		std::string LowerDisplayName;

		uint64_t UserId;
		std::string UserIdStr;

		size_t AccountAge;
		std::string AccountAgeStr;
		std::string AccountCreatedStr;

		bool TeleportedIn;
		std::string TeleportedInStr;

		std::shared_ptr<PlayerInformation> CachedFollowUser;
		uint64_t FollowUserId;
		std::string FollowUserIdStr;
		std::string FollowUserIdProfile;

		std::string ProfileLink;

		unsigned int ImageId;
		std::string ProfilePicture;

		bool FetchingProfilePicture; // If it fails, retry on next refresh
		bool WasReferenced; // for cache scans
	};

	// Save the engine data (and cache string counterparts_
	struct EngineData
	{
		size_t PlaceId;
		std::string PlaceIdStr;

		std::string JobId;

		size_t PlayerCount;
		size_t PlayersJoined;
		size_t PlayersLeft;

		std::string PlayerCountCache;
	};

	// Thread safe class to get the player data
	struct PlayerDataReader
	{
		bool unsafe;
		std::vector<std::shared_ptr<PlayerInformation>>& data;
		std::vector<std::shared_ptr<PlayerInformation>>& cache;

		PlayerDataReader(bool unsafe = false);
		~PlayerDataReader();
	};

	// Thread safe class to get engine data
	struct EngineReader
	{
		bool unsafe;
		EngineData& data;

		EngineReader(bool unsafe = false); // Get player data without mutex
		~EngineReader();
	};

	// Request a recache of player data
	void RequestRefresh();

	// Watcher thread to keep player information grabbing and rendering separate
	void StartWatcher();
}