#include <windows.h>

#include <stdio.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <patch.h>
#include <global.h>
#include <event.h>
#include <input.h>
#include <config.h>
#include <script.h>
#include <net.h>
#include <gfx.h>
#include <window.h>
#include <glyph.h>
#include <log.h>

#define VERSION_NUMBER_MAJOR 1
#define VERSION_NUMBER_MINOR 0
#define VERSION_NUMBER_FIX 1

void *addr_shuffle1 = 0x004523a7;
void *addr_shuffle2 = 0x004523b4;
void *addr_origrand = 0x00403de0;
uint8_t *addr_procevents = 0x005f56e0;
uint8_t *shouldQuit = 0x007d6a2c;

void our_random(int out_of) {
	// first, call the original random so that we consume a value.  
	// juuust in case someone wants actual 100% identical behavior between partymod and the original game
	void (__cdecl *their_random)(int) = addr_origrand;

	their_random(out_of);

	return rand() % out_of;
}

void patchPlaylistShuffle() {
	patchCall(addr_shuffle1, our_random);
	patchCall(addr_shuffle2, our_random);
}

int32_t __fastcall calcAvailableSpaceFudged(uint8_t *param1) {
	if (*(param1 + 0xc)) {
		ULARGE_INTEGER freeSpace;
		ULARGE_INTEGER totalSpace;
		uint8_t result = GetDiskFreeSpaceEx(NULL, &freeSpace, &totalSpace, NULL);

		if (result) {
			uint64_t space = freeSpace.QuadPart >> 14;

			if (space > INT32_MAX - (UINT16_MAX * 2)) {
				space = INT32_MAX - (UINT16_MAX * 2);	// clamp to a number that should fit anything and allow a bit of math (number from ClownJob'd release notes)
			}

			return space;
		}
	}

	return 0;
}

void patchCalcAvailableSpace() {
	patchCall(0x005a6e7e, calcAvailableSpaceFudged);
}

uint32_t rng_seed = 0;

void handleQuitEvent(SDL_Event *e) {
	switch (e->type) {
		case SDL_QUIT: {
			
			*shouldQuit = 1;
			return;
		}
		default:
			return;
	}
}

void initPatch() {
	GetModuleFileName(NULL, &executableDirectory, filePathBufLen);

	// find last slash
	char *exe = strrchr(executableDirectory, '\\');
	if (exe) {
		*(exe + 1) = '\0';
	}

	initConfig();

	int isDebug = getConfigBool(CONFIG_MISC_SECTION, "Debug", 0);

	configureLogging(isDebug);

	if (isDebug) {
		AllocConsole();

		FILE *fDummy;
		freopen_s(&fDummy, "CONIN$", "r", stdin);
		freopen_s(&fDummy, "CONOUT$", "w", stderr);
		freopen_s(&fDummy, "CONOUT$", "w", stdout);
	}
	log_printf(LL_INFO, "PARTYMOD for THUG2 %d.%d.%d\n", VERSION_NUMBER_MAJOR, VERSION_NUMBER_MINOR, VERSION_NUMBER_FIX);

	log_printf(LL_INFO, "DIRECTORY: %s\n", executableDirectory);

	//init_patch_cache();

	//findOffsets();
	//installPatches();

	initScriptPatches();
	initEvents();
	registerEventHandler(handleQuitEvent);

	// get some source of entropy for the music randomizer
	rng_seed = time(NULL) & 0xffffffff;
	srand(rng_seed);

	log_printf(LL_INFO, "Patch Initialized\n");

	patchOnlineService();
	patchButtonGlyphs();

	loadGfxSettings();

	//printf("BASE ADDR: 0x%08x, LEN: %d\n", base_addr, mod_size);
}

void patchEventHandler() {
	patchJmp(addr_procevents, handleEvents);
}

void getModuleInfo() {
	void *mod = GetModuleHandle(NULL);
	//mod = 0x400000;

	base_addr = mod;
	void *end_addr = NULL;

	PIMAGE_DOS_HEADER dos_header = mod;
	PIMAGE_NT_HEADERS nt_headers = (uint8_t *)mod + dos_header->e_lfanew;

	for (int i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
		PIMAGE_SECTION_HEADER section = (uint8_t *)nt_headers->OptionalHeader.DataDirectory + nt_headers->OptionalHeader.NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY) + i * sizeof(IMAGE_SECTION_HEADER);

		uint32_t section_size;
		if (section->SizeOfRawData != 0) {
			section_size = section->SizeOfRawData;
		} else {
			section_size = section->Misc.VirtualSize;
		}

		if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
			end_addr = (uint8_t *)base_addr + section->VirtualAddress + section_size;
		}

		if ((i == nt_headers->FileHeader.NumberOfSections - 1) && end_addr == NULL) {
			end_addr = (uint32_t)base_addr + section->PointerToRawData + section_size;
		}
	}

	mod_size = (uint32_t)end_addr - (uint32_t)base_addr;
}

void fastExit() {
	exit(0);
}

// we don't care about cleanup, the OS can handle that. so patch the function immediately after the main loop to call exit to skip cleanup
void patchFastExit() {
	patchCall(0x004e2492, fastExit);
}

// make movies check for input more often than every five frames, makes skipping much more consistent and faster
void patchMovieSkip() {
	patchNop(0x00450121, 6);
}

void networkParamsWrapper(void *args) {
	void (__cdecl *orig_networkParams)(void *) = 0x004e1220;

	initPatch();

	orig_networkParams(args);
}

__declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	// Perform actions based on the reason for calling.
	switch(fdwReason) { 
		case DLL_PROCESS_ATTACH:
			// Initialize once for each new process.
			// Return FALSE to fail DLL load.

			getModuleInfo();

			//findInitOffset();

			// install patches
			patchCall((void *)0x004e26dc, &(networkParamsWrapper));

			patchEventHandler();
			patchWindow();
			patchInput();
			patchGfx();
			patchScriptHook();
			patchPlaylistShuffle();
			patchCalcAvailableSpace();
			patchFastExit();
			patchMovieSkip();

			break;

		case DLL_THREAD_ATTACH:
			// Do thread-specific initialization.
			break;

		case DLL_THREAD_DETACH:
			// Do thread-specific cleanup.
			break;

		case DLL_PROCESS_DETACH:
			// Perform any necessary cleanup.
			break;
	}
	return TRUE;
}
