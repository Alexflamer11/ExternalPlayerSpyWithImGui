#pragma once

#include <Windows.h>
#include <string>

namespace Utils
{
	// Convert strings from different types
	std::wstring FromUtf8(std::string& str);
	std::string FromUtf16(std::wstring& data);

	// Copy the passed string to the clipboard
	void SetClipboard(std::string& source);

	// Finds a process id and returns it
	DWORD FindProcessId(std::wstring name);

	// Compare and scan from axstin fps unlocker for lazy auto update task scheduler 
	bool Compare(const char* location, const char* aob, const char* mask);
	uint8_t* Scan(const char* aob, const char* mask, uintptr_t start, uintptr_t end);

	// Simple console functions to show and hide the console when needed
	bool IsConsoleVisible();
	void HideConsole();
	void ShowConsole();
}