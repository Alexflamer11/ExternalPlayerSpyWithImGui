#include "Utils.hpp"

#include <TlHelp32.h>

std::wstring Utils::FromUtf8(std::string& str)
{
	if (str.empty()) return L"";

	size_t ConvertLength = str.length();

	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], ConvertLength, NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], ConvertLength, &wstrTo[0], size_needed);

	return wstrTo;
}

std::string Utils::FromUtf16(std::wstring& data)
{
	if (data.empty()) return "";

	size_t ConvertLength = data.length(); // Do not want the \0

	int SizeNeeded = WideCharToMultiByte(CP_UTF8, 0, &data[0], ConvertLength, NULL, 0, NULL, NULL);
	std::string Result(SizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, NULL, &data[0], ConvertLength, &Result[0], SizeNeeded, NULL, NULL);

	return Result;
}

void Utils::SetClipboard(std::string& source)
{
	if (OpenClipboard(NULL))
	{
		std::wstring converted = FromUtf8(source);

		HGLOBAL G = GlobalAlloc(GMEM_MOVEABLE, (converted.size() + 1) * sizeof(wchar_t));
		EmptyClipboard();

		wchar_t* lock = (wchar_t*)GlobalLock(G);
		if (lock)
		{
			wcscpy_s(lock, converted.size() + 1, converted.c_str());

			GlobalUnlock(G);
			SetClipboardData(CF_UNICODETEXT, G);
		}

		CloseClipboard();
		GlobalFree(G);
	}
	else
		puts("[!] Failed to open clipboard");
}

DWORD Utils::FindProcessId(std::wstring processName)
{
	PROCESSENTRY32 processInfo;
	processInfo.dwSize = sizeof(processInfo);

	HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (processSnapshot == INVALID_HANDLE_VALUE)
		return 0;

	Process32First(processSnapshot, &processInfo);
	if (!processName.compare(processInfo.szExeFile))
	{
		if (HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processInfo.th32ProcessID))
		{
			BOOL debugged = FALSE;
			CheckRemoteDebuggerPresent(process, &debugged);
			CloseHandle(process);
			if (!debugged)
			{
				CloseHandle(processSnapshot);
				return processInfo.th32ProcessID;
			}
		}
	}

	while (Process32Next(processSnapshot, &processInfo))
	{
		if (!processName.compare(processInfo.szExeFile))
		{
			if (HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processInfo.th32ProcessID))
			{
				BOOL debugged = FALSE;
				CheckRemoteDebuggerPresent(process, &debugged);
				CloseHandle(process);
				if (!debugged)
				{
					CloseHandle(processSnapshot);
					return processInfo.th32ProcessID;
				}
			}
		}
	}

	CloseHandle(processSnapshot);
	return 0;
}

bool Utils::Compare(const char* location, const char* aob, const char* mask)
{
	for (; *mask; ++aob, ++mask, ++location)
	{
		if (*mask == 'x' && *location != *aob)
		{
			return false;
		}
	}

	return true;
}

uint8_t* Utils::Scan(const char* aob, const char* mask, uintptr_t start, uintptr_t end)
{
	if (start <= end)
	{
		for (; start < end - strlen(mask); ++start)
		{
			if (Compare((char*)start, (char*)aob, mask))
			{
				return (uint8_t*)start;
			}
		}
	}

	return 0;
};

bool Utils::IsConsoleVisible()
{
	return IsWindowVisible(GetConsoleWindow()) != FALSE;
}

void Utils::HideConsole()
{
	if (IsConsoleVisible())
		ShowWindow(GetConsoleWindow(), SW_HIDE);
}

void Utils::ShowConsole()
{
	if (!IsConsoleVisible())
		ShowWindow(GetConsoleWindow(), SW_SHOW);
}