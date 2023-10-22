// https://github.com/3gstudent/Inject-dll-by-APC/blob/master/NtCreateThreadEx%20%2B%20LdrLoadDll.cpp
// Wanted LdrLoadDll as the only dll that is currently loaded is ntdll.dll

#include <Windows.h>
#include <iostream>
#include <TlHelp32.h>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

#pragma comment(lib,"Advapi32.lib") 
typedef NTSTATUS(NTAPI* pfnNtCreateThreadEx)
(
	OUT PHANDLE hThread,
	IN ACCESS_MASK DesiredAccess,
	IN PVOID ObjectAttributes,
	IN HANDLE ProcessHandle,
	IN PVOID lpStartAddress,
	IN PVOID lpParameter,
	IN ULONG Flags,
	IN SIZE_T StackZeroBits,
	IN SIZE_T SizeOfStackCommit,
	IN SIZE_T SizeOfStackReserve,
	OUT PVOID lpBytesBuffer);

#define NT_SUCCESS(x) ((x) >= 0)

typedef struct _LSA_UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
} LSA_UNICODE_STRING, * PLSA_UNICODE_STRING, UNICODE_STRING, * PUNICODE_STRING;

typedef NTSTATUS(NTAPI* pRtlInitUnicodeString)(PUNICODE_STRING, PCWSTR);
typedef NTSTATUS(NTAPI* pLdrLoadDll)(PWCHAR, ULONG, PUNICODE_STRING, PHANDLE);
typedef DWORD64(WINAPI* _NtCreateThreadEx64)(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess, LPVOID ObjectAttributes, HANDLE ProcessHandle, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, BOOL CreateSuspended, DWORD64 dwStackSize, DWORD64 dw1, DWORD64 dw2, LPVOID Unknown);

typedef struct _THREAD_DATA
{
	pRtlInitUnicodeString fnRtlInitUnicodeString;
	pLdrLoadDll fnLdrLoadDll;
	UNICODE_STRING UnicodeString;
	WCHAR DllName[260];
	PWCHAR DllPath;
	ULONG Flags;
	HANDLE ModuleHandle;
}THREAD_DATA, * PTHREAD_DATA;

HANDLE WINAPI ThreadProc(PTHREAD_DATA data)
{
	data->fnRtlInitUnicodeString(&data->UnicodeString, data->DllName);
	data->fnLdrLoadDll(data->DllPath, data->Flags, &data->UnicodeString, &data->ModuleHandle);
	return data->ModuleHandle;
}

DWORD WINAPI ThreadProcEnd()
{
	return 0;
}

HANDLE MyCreateRemoteThread(HANDLE hProcess, LPTHREAD_START_ROUTINE pThreadProc, LPVOID pRemoteBuf)
{
	HANDLE hThread = NULL;
	FARPROC pFunc = NULL;

	pFunc = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx");
	if (pFunc == NULL)
	{
		printf("[!]GetProcAddress (\"NtCreateThreadEx\")error\n");
		return NULL;
	}
	((_NtCreateThreadEx64)pFunc)(&hThread, 0x1FFFFF, NULL, hProcess, pThreadProc, pRemoteBuf, FALSE, NULL, NULL, NULL, NULL);
	if (hThread == NULL)
	{
		printf("[!]MyCreateRemoteThread : NtCreateThreadEx error\n");
		return NULL;
	}

	if (WAIT_FAILED == WaitForSingleObject(hThread, INFINITE))
	{
		printf("[!]MyCreateRemoteThread : WaitForSingleObject error\n");
		return NULL;
	}
	return hThread;
}

bool Load()
{
	if (!std::filesystem::exists("ExternalPlayerSpyWithImGui.dll"))
	{
		if (!std::filesystem::exists("..\\x64\\Release\\ExternalPlayerSpyWithImGui.dll"))
		{
			printf("[!] Failed to locate the dll\n");
			system("pause");
			return 1;
		}

		SetCurrentDirectoryW(L"..\\x64\\Release");
	}

	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (!CreateProcessW(L"ToHijack.exe"
		, NULL
		, NULL
		, NULL
		, FALSE
		, CREATE_SUSPENDED
		, NULL
		, NULL
		, &si            // Pointer to STARTUPINFO structure
		, &pi
	))
	{
		printf("failed to create process\n");
		return 1;
	}

	wchar_t full_file_name[MAX_PATH];
	GetFullPathNameW(L"ExternalPlayerSpyWithImGui.dll", MAX_PATH, full_file_name, nullptr);

	THREAD_DATA data;
	HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
	data.fnRtlInitUnicodeString = (pRtlInitUnicodeString)GetProcAddress(hNtdll, "RtlInitUnicodeString");
	data.fnLdrLoadDll = (pLdrLoadDll)GetProcAddress(hNtdll, "LdrLoadDll");
	memcpy(data.DllName, full_file_name, (wcslen(full_file_name) + 1) * sizeof(WCHAR));
	data.DllPath = NULL;
	data.Flags = 0;
	data.ModuleHandle = INVALID_HANDLE_VALUE;

	LPVOID pThreadData = VirtualAllocEx(pi.hProcess, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (pThreadData == NULL)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] VirtualAllocEx error\n");
		return 2;
	}

	BOOL bWriteOK = WriteProcessMemory(pi.hProcess, pThreadData, &data, sizeof(data), NULL);
	if (!bWriteOK)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] WriteProcessMemory error\n");
		return 3;
	}

	DWORD SizeOfCode = (DWORD)ThreadProcEnd - (DWORD)ThreadProc;
	LPVOID pCode = VirtualAllocEx(pi.hProcess, NULL, SizeOfCode, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (pCode == NULL)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] VirtualAllocEx error, %d\n", GetLastError());
		return 4;
	}
	bWriteOK = WriteProcessMemory(pi.hProcess, pCode, (PVOID)ThreadProc, SizeOfCode, NULL);
	if (!bWriteOK)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] WriteProcessMemory error\n");
		return 5;
	}

	HANDLE hThread = MyCreateRemoteThread(pi.hProcess, (LPTHREAD_START_ROUTINE)pCode, pThreadData);
	if (hThread == NULL)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] MyCreateRemoteThread error\n");
		return 6;
	}

	if (WaitForSingleObject(hThread, 3000) != WAIT_OBJECT_0)
	{
		TerminateProcess(pi.hProcess, EXIT_SUCCESS);
		printf("[!] Failed to wait for object\n");
		return 7;
	}

	// Terminate the initial thread, this way we have no suspended threads and no need to disable the entrypoint
	TerminateThread(pi.hThread, EXIT_SUCCESS);
	
	return 0;
}

int main()
{
	int res = Load();
	if (res)
		printf("[!] Failed to load with error code: %i\n", res);
	else
		printf("[*] Successfully loaded the external player spy\n");

	printf("Closing in 3 seconds...\n");
	std::this_thread::sleep_for(std::chrono::seconds(3));

	return res;
}