#pragma once

#include <Windows.h>

namespace PlayerScan
{
	// Update the player list (and also server information) with an optional force recache of all offsets
	void UpdatePlayerList(HANDLE open_process, bool fresh_process);
}