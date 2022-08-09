#pragma once

#include <cstdint>

// Constant offsets
namespace Offset
{
	const size_t SingletonListStart = 308;
	const size_t SingletonListEnd = SingletonListStart + 4;
	const size_t JobName = 16;
	const size_t ScriptContextFromJob = 304;

	const size_t ClassName = 20;
	const size_t Children = 40;
	const size_t Name = 36;
	const size_t Parent = 48;

	const size_t PropertyList = 12;
	const size_t PropertyStart = 536;
	const size_t PropertyEnd = 540;
	const size_t PropertyName = 4;
	const size_t PropertyVFTable = 48;
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