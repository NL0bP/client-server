#include <malloc.h>  // _msize
#include <stdlib.h>
#include <string.h>

#include "Library/DLL.h"
#include "Library/WinAPI.h"

#include "CryCommon/CrySystem/ISystem.h"
#include "CryCommon/CrySystem/IConsole.h"

#include "CryMemoryManager.h"
#include <Windows.h>

#define ALLOC_DEBUG
#define ALLOC_ALIGNMENT 128
#define ALLOC_ALIGNMENT_LOW (ALLOC_ALIGNMENT - 1)

namespace
{
	CRITICAL_SECTION cs;
	CryMemoryManager::Statistics stats;

	void* CryMalloc_hook(size_t size, size_t& allocatedSize)
	{
		void* result = nullptr;

		if (size)
		{
			size_t waste = ALLOC_ALIGNMENT_LOW & (ALLOC_ALIGNMENT - (size & ALLOC_ALIGNMENT_LOW));
			size += waste;

#ifdef ALLOC_DEBUG
			EnterCriticalSection(&cs);
			stats.activeAllocations++;
			stats.totalAllocations++;
			stats.totalWaste += waste;
			stats.activeMemory += size;
			LeaveCriticalSection(&cs);
#endif

			result = malloc(size);

			allocatedSize = _msize(result);
		}
		else
		{
			allocatedSize = 0;
		}

		return result;
	}

	void* CryRealloc_hook(void* mem, size_t size, size_t& allocatedSize)
	{
		void* result = nullptr;

		if (size)
		{
			size_t waste = ALLOC_ALIGNMENT_LOW & (ALLOC_ALIGNMENT - (size & ALLOC_ALIGNMENT_LOW));
			size += waste;

#ifdef ALLOC_DEBUG
			EnterCriticalSection(&cs);
			stats.totalAllocations++;
			stats.totalWaste += waste;
			stats.activeMemory += size;
			if (mem) stats.activeMemory -= _msize(mem);
			LeaveCriticalSection(&cs);
#endif
			result = realloc(mem, size);

			allocatedSize = _msize(result);
		}
		else
		{
			if (mem)
			{
#ifdef ALLOC_DEBUG
				EnterCriticalSection(&cs);
				stats.activeMemory -= _msize(mem);
				stats.activeAllocations--;
				LeaveCriticalSection(&cs);
#endif
				free(mem);
			}

			allocatedSize = 0;
		}

		return result;
	}

	size_t CryFree_hook(void* mem)
	{
		size_t size = 0;

		if (mem)
		{
			size = _msize(mem);

#ifdef ALLOC_DEBUG
			EnterCriticalSection(&cs);
			stats.activeMemory -= size;
			stats.activeAllocations--;
			LeaveCriticalSection(&cs);
#endif

			free(mem);
		}

		return size;
	}

	void* CrySystemCrtMalloc_hook(size_t size)
	{
		void* result = nullptr;

		if (size)
		{
			size_t waste = ALLOC_ALIGNMENT_LOW & (ALLOC_ALIGNMENT - (size & ALLOC_ALIGNMENT_LOW));
			size += waste;

#ifdef ALLOC_DEBUG
			EnterCriticalSection(&cs);
			stats.totalAllocations++;
			stats.totalWaste += waste;
			stats.activeMemory += size;
			LeaveCriticalSection(&cs);
#endif
			result = malloc(size);
		}

		return result;
	}

	void CrySystemCrtFree_hook(void* mem)
	{
		if (mem)
		{
#ifdef ALLOC_DEBUG
			EnterCriticalSection(&cs);
			stats.activeMemory -= _msize(mem);
			stats.activeAllocations--;
			LeaveCriticalSection(&cs);
#endif
			free(mem);
		}
	}

	void Hook(void* pFunc, void* pNewFunc)
	{
		if (!pFunc)
		{
			return;
		}

#ifdef BUILD_64BIT
		unsigned char code[] = {
			0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, 0x0
			0xFF, 0xE0                                                   // jmp rax
		};

		memcpy(&code[2], &pNewFunc, 8);
#else
		unsigned char code[] = {
			0xB8, 0x00, 0x00, 0x00, 0x00,  // mov eax, 0x0
			0xFF, 0xE0                     // jmp eax
		};

		memcpy(&code[1], &pNewFunc, 4);
#endif

		WinAPI::FillMem(pFunc, code, sizeof code);
	}

	static void AllocDebugCmd(IConsoleCmdArgs* pArgs)
	{
		auto stats = CryMemoryManager::GetStatistics();
		if (!stats.available) {
			CryLogAlways("$4[alloc] Memory statistics are disabled");
			return;
		}
		CryLogAlways("$3[alloc] Total accumulated waste: $6%.2f kB", stats.totalWaste / 1024.0f);
		CryLogAlways("$3[alloc] Average accumulated waste: $6%.2f kB", stats.totalWaste / stats.totalAllocations / 1024.0f);
		CryLogAlways("$3[alloc] Active memory: $6%.2f kB", stats.activeMemory / 1024.0f);
		CryLogAlways("$3[alloc] Active allocations: $6%llu", stats.activeAllocations);
		CryLogAlways("$3[alloc] Estimated active waste: $6%.2f kB", stats.activeAllocations * stats.totalWaste / stats.totalAllocations / 1024.0f);
	}
}

void CryMemoryManager::Init(const DLL& CrySystem)
{
	ZeroMemory(&cs, sizeof(cs));
	InitializeCriticalSection(&cs);
	Hook(CrySystem.GetSymbolAddress("CryMalloc"), CryMalloc_hook);
	Hook(CrySystem.GetSymbolAddress("CryRealloc"), CryRealloc_hook);
	Hook(CrySystem.GetSymbolAddress("CryFree"), CryFree_hook);
	Hook(CrySystem.GetSymbolAddress("CrySystemCrtMalloc"), CrySystemCrtMalloc_hook);
	Hook(CrySystem.GetSymbolAddress("CrySystemCrtFree"), CrySystemCrtFree_hook);
}

CryMemoryManager::Statistics CryMemoryManager::GetStatistics() 
{
	CryMemoryManager::Statistics statsCopy;
#ifdef ALLOC_DEBUG
	EnterCriticalSection(&cs);
	statsCopy = stats;
	LeaveCriticalSection(&cs);
#else
	statsCopy.available = false;
#endif
	return stats;
}

void CryMemoryManager::RegisterConsoleCommands(IConsole* pConsole)
{
	pConsole->AddCommand("alloc_info", AllocDebugCmd);
}

void CryMemoryManager::ReleaseConsoleCommands(IConsole* pConsole)
{
	pConsole->RemoveCommand("alloc_info");
}