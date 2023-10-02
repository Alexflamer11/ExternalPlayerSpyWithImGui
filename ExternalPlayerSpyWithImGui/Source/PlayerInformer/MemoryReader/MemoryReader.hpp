#pragma once

#include "Offsets.hpp"

#include <Windows.h>
#include <Psapi.h>

#include <string>
#include <vector>

struct ProcessInfo
{
	uintptr_t base;
	size_t size;
};

namespace MemoryReader
{
	// These are slightly modified from axstin fps unlocker to prevent duplicate code because they are nice to use
	template<typename T>
	inline bool Read(HANDLE process, uintptr_t address, T* buffer)
	{
		if (!ReadProcessMemory(process, (void*)address, buffer, sizeof(T), NULL))
			return false;
	
		return true;
	}

	template<typename T>
	inline bool Read(HANDLE process, uintptr_t address, T* buffer, size_t buffer_size)
	{
		if (!ReadProcessMemory(process, (void*)address, buffer, buffer_size, NULL))
			return false;

		return true;
	}

	template<typename T>
	inline T Read(HANDLE process, uintptr_t address)
	{
		T result;
		if (!ReadProcessMemory(process, (void*)address, &result, sizeof(T), NULL))
			throw std::exception("failed to read memory");

		return result;
	}

	inline std::string ReadString(HANDLE process, uintptr_t address)
	{
		std::string result;

		size_t buffer_size = Read<size_t>(process, address + 24);
		if (buffer_size >= 16)
		{
			size_t len = Read<size_t>(process, address + 16); // Length of actual characters (null terminated)

			// Safety net to not read too far
			if (len >= 100)
				return result;

			result.resize(len, '\0');

			address = Read<uintptr_t>(process, address);
			Read(process, address, &result[0], len);
		}
		else
			Read(process, address, &result[0], sizeof(std::string)); // Overwrite entire std::string

		return result;
	}

	inline std::string ReadStringPtr(HANDLE process, uintptr_t address)
	{
		uintptr_t raw_string;
		if (!Read<uintptr_t>(process, address, &raw_string) || !raw_string)
			return "";

		return ReadString(process, raw_string);
	}

	inline std::string ClassName(HANDLE process, uintptr_t instance, uintptr_t base_address)
	{
		// So just read the typeinfo instead as thats reliable and the classname offset is not
		uintptr_t vftable = MemoryReader::Read<uintptr_t>(process, instance);
		if (!vftable)
			return "";

		uintptr_t vftable_info = MemoryReader::Read<uintptr_t>(process, vftable - sizeof(void*));
		if (!vftable_info)
			return "";

		uintptr_t type_info = base_address + MemoryReader::Read<uint32_t>(process, vftable_info + 12);

		char buff[0x100];
		if (!MemoryReader::Read(process, type_info + sizeof(void*) * 2, buff, sizeof(buff)))
			return "";

		if (memcmp(buff, ".?AV", 4) != 0)
			return "";

		char* first_at = strchr(buff, '@');

		return std::string(buff + 4, first_at ? first_at - buff - 4 : strlen(buff));
	}
	
	// ScanRegion and ScanProcess from axstin fps unlocker for lazy auto update task scheduler
	void* ScanRegion(HANDLE process, const char* aob, const char* mask, uint8_t* base, size_t size);
	void* ScanProcess(HANDLE process, const char* aob, const char* mask, uint8_t* start, uint8_t* end);

	MODULEINFO GetProcessInfo(HANDLE process);
}