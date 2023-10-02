#pragma once

#include <cstdint>
#include <string>

// Constant offsets
namespace Offset
{
	extern std::string CachedFileVersion;

	const size_t SingletonListStart = 376;
	const size_t SingletonListEnd = SingletonListStart + sizeof(void*);
	const size_t JobName = 152;
	const size_t ScriptContextFromJob = 504;

	// const size_t ClassName = 40; // no longer used because bad
	const size_t Children = 80;
	const size_t Name = 72;
	const size_t Parent = 96;

	const size_t PropertyList = 24;
	const size_t PropertyStart = 936;
	const size_t PropertyEnd = 944;
	const size_t PropertyName = 8;
	const size_t PropertyVFTable = 96; // Not all properties are here, but the ones wanted are
	const size_t PropertyVFTableGet = 8;

	namespace DataModel
	{
		extern size_t PlaceId;
		extern size_t JobId;
	}

	namespace Player
	{
		extern size_t DisplayName;
		extern size_t UserId;
		extern size_t AccountAge;
		extern size_t FollowUserId;
		extern size_t TeleportedIn;
	}
}