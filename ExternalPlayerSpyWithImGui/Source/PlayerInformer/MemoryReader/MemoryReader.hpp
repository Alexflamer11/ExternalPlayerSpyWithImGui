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

		size_t buffer_size = Read<size_t>(process, address + 20);
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
			Read(process, address, &result[0], 24); // Overwrite entire std::string

		return result;
	}

	inline std::string ReadStringPtr(HANDLE process, uintptr_t address)
	{
		uintptr_t raw_string;
		if (!Read<uintptr_t>(process, address, &raw_string) || !raw_string)
			return "";

		return ReadString(process, raw_string);
	}

	inline std::string ClassName(HANDLE process, uintptr_t instance)
	{
		// This function will not error asides from actual read errors unless a major exception occurs, this is on purpose
		uintptr_t instance_vftable;
		if (!Read<uintptr_t>(process, instance, &instance_vftable) || !instance_vftable)
			return "";

		uintptr_t instance_class_name_function;
		if (!Read<uintptr_t>(process, instance_vftable + Offset::ClassName, &instance_class_name_function) || !instance_class_name_function)
			return "";

		uint8_t move_instruction;
		if (!Read<uint8_t>(process, instance_class_name_function, &move_instruction) || move_instruction != 0xA1) // mov eax, XXXXXXXX
			return "";

		uintptr_t class_name_raw;
		if (!Read<uintptr_t>(process, instance_class_name_function + 1, &class_name_raw) || !class_name_raw)
			return "";

		std::string class_name = ReadStringPtr(process, class_name_raw);
		if (class_name.empty())
		{
			// Idiotic path, name not initialized, I refuse to create a thread
			uint8_t call_instruction;
			if (!Read<uint8_t>(process, instance_class_name_function + 9, &call_instruction) || call_instruction != 0xE8) // call sub_XXXXXXXX
				return "";

			intptr_t setter_call;
			if (!Read<intptr_t>(process, instance_class_name_function + 10, &setter_call) || !setter_call) // call destination
				return "";

			// Get the actual destination of the call
			uintptr_t class_name_initializer = instance_class_name_function + 14 + setter_call; // call + destination + 5

			// Check the entry to make sure it is a function
			uint8_t entry_instruction;
			if (!Read<uint8_t>(process, class_name_initializer, &entry_instruction) || entry_instruction != 0x53) // push ebx
				return "";

			// Create an instruction buffer to scan for push STRING
			uint8_t buffer[256];
			if (!Read<uint8_t>(process, class_name_initializer, buffer, sizeof(buffer)))
				return "";

			for (uint8_t* i = buffer; i < buffer + sizeof(buffer); i++)
			{
				// Found push string
				if (i[0] == 0x6A && i[2] == 0x6A && i[4] == 0x6A && i[6] == 0x68)
				{
					uintptr_t string_address = *reinterpret_cast<uintptr_t*>(i + 7);

					char raw_class_name[128];

					// This will read over the buffer, but since there are always bytes after this, it is not an issue
					if (!Read<char>(process, string_address, raw_class_name, sizeof(raw_class_name)))
						return "";

					raw_class_name[127] = '\0'; // 100% make sure it is null terminated

					class_name = raw_class_name;
				}
			}
		}

		return class_name;
	}
	
	// ScanRegion and ScanProcess from axstin fps unlocker for lazy auto update task scheduler
	void* ScanRegion(HANDLE process, const char* aob, const char* mask, uint8_t* base, size_t size);
	void* ScanProcess(HANDLE process, const char* aob, const char* mask, uint8_t* start, uint8_t* end);

	MODULEINFO GetProcessInfo(HANDLE process);
}