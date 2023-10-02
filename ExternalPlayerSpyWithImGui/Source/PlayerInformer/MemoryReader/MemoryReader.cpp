#include "MemoryReader.hpp"

#include "Utils/Utils.hpp"

#include <TlHelp32.h>

void* MemoryReader::ScanRegion(HANDLE process, const char* aob, const char* mask, uint8_t* base, size_t size)
{
#define READ_LIMIT (1024 * 1024 * 2) // 2 MB
	std::vector<uint8_t> buffer;
	buffer.resize(READ_LIMIT);

	size_t aob_len = strlen(mask);

	while (size >= aob_len)
	{
		size_t bytes_read = 0;

		if (ReadProcessMemory(process, base, buffer.data(), size < buffer.size() ? size : buffer.size(), (SIZE_T*)&bytes_read) && bytes_read >= aob_len)
		{
			if (uint8_t* result = Utils::Scan(aob, mask, (uintptr_t)buffer.data(), (uintptr_t)buffer.data() + bytes_read))
			{
				return (uint8_t*)base + (result - buffer.data());
			}
		}

		if (bytes_read > aob_len) bytes_read -= aob_len;

		size -= bytes_read;
		base += bytes_read;
	}

	return 0;
}

void* MemoryReader::ScanProcess(HANDLE process, const char* aob, const char* mask, uint8_t* start, uint8_t* end)
{
#define PAGE_READABLE (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)
	auto i = start;

	while (i < end)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQueryEx(process, i, &mbi, sizeof(mbi)))
		{
			return nullptr;
		}

		size_t size = mbi.RegionSize - (i - (const uint8_t*)mbi.BaseAddress);
		if (i + size >= end) size = end - i;

		if (mbi.State & MEM_COMMIT && mbi.Protect & PAGE_READABLE && !(mbi.Protect & PAGE_GUARD))
		{
			if (void* result = ScanRegion(process, aob, mask, i, size))
			{
				return result;
			}
		}

		i += size;
	}

	return nullptr;
}

HANDLE last_process_handle = 0;
MODULEINFO cached_info{};
MODULEINFO MemoryReader::GetProcessInfo(HANDLE process)
{
	if (!last_process_handle || process != last_process_handle)
	{
		last_process_handle = process;

		// Enum process modules does not immediately get the offset we want
		HANDLE hsnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(process));
		MODULEENTRY32W me32;
		me32.dwSize = sizeof(MODULEENTRY32W);
		Module32FirstW(hsnapshot, &me32);
		cached_info = { me32.modBaseAddr, me32.modBaseSize, 0 }; // Too lazy to change
		CloseHandle(hsnapshot);
	}

	return cached_info;
}