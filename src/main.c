#include <windows.h>

#include <stdio.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <patch.h>
#include <patchcache.h>
#include <global.h>
#include <input.h>
#include <config.h>
#include <script.h>

#define VERSION_NUMBER_MAJOR 0
#define VERSION_NUMBER_MINOR 0
#define VERSION_NUMBER_FIX 1

void *initAddr = NULL;

void *addr_shuffle1 = 0x004523a7;
void *addr_shuffle2 = 0x004523b4;
void *addr_origrand = 0x00403de0;
uint32_t addr_camera_lookat;

uint8_t get_misc_offsets() {
	uint8_t *shuffleAnchor = NULL;

	uint8_t result = 1;
	result &= patch_cache_pattern("53 e8 ?? ?? ?? ?? 8b 15 ?? ?? ?? ?? 52 8b f8 e8 ?? ?? ?? ?? 83 c4 08", &shuffleAnchor);
	result &= patch_cache_pattern("f3 0f 5c cc f3 0f 5c d5 f3 0f 5c de 68", &addr_camera_lookat);

	if (result) {
		addr_origrand = *(uint32_t *)(shuffleAnchor + 2) + shuffleAnchor + 6;
		addr_shuffle1 = shuffleAnchor + 1;
		addr_shuffle2 = shuffleAnchor + 15;
	} else {
		printf("FAILED TO FIND MISC OFFSETS\n");
	}

	return result;
}

/*void findOffsets() {
	printf("finding offsets...\n");

	uint8_t result = 1;
	result &= get_config_offsets();
	result &= get_script_offsets();
	result &= get_input_offsets();
	result &= get_misc_offsets();
	result;

	if (!result) {
		printf("PATCHES FAILED!!! %d\n", result);
		//SDL_Delay(3000);
	}

	printf("done!\n");
}*/

typedef struct {
	float x;
	float y;
	float z;
} vec3f;

vec3f vec3f_normalize(vec3f v) {
	float len = fabsf(sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z)));

	if (len == 0)
		return (vec3f) {0.0, 0.0, 0.0};

	len = 1.0f / len;

	v.x *= len;
	v.y *= len;
	v.z *= len;

	return v;
}

vec3f vec3f_cross(vec3f a, vec3f b) {
	vec3f result;

	result.x = (a.y * b.z) - (a.z * b.y);
	result.y = (a.z * b.x) - (a.x * b.z);
	result.z = (a.x * b.y) - (a.y * b.x);

	return result;
}

float vec3f_dot(vec3f a, vec3f b) {
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

void _stdcall fixedcamera(float *mat_out, vec3f *eye, vec3f *forward, vec3f *up) {
	// lookat function that expects a preprepared forward vector
	// the original function was taking the forward vector, adding the camera position, and going back again which ruined the camera's angle's precision
	// this makes the game move much more smoothly in far edges of the world, like oil rig

	vec3f f = *forward;	//f - front
	vec3f r = vec3f_normalize(vec3f_cross(*up, f));	//r - right
	vec3f u = vec3f_cross(f, r);	//u - up

	// output is in column-major order
	mat_out[0] = r.x;
	mat_out[4] = r.y;
	mat_out[8] = r.z;
	mat_out[12] = -vec3f_dot(r, *eye);
	mat_out[1] = u.x;
	mat_out[5] = u.y;
	mat_out[9] = u.z;
	mat_out[13] = -vec3f_dot(u, *eye);
	mat_out[2] = f.x;
	mat_out[6] = f.y;
	mat_out[10] = f.z;
	mat_out[14] = -vec3f_dot(f, *eye);
	mat_out[3] = 0.0f;
	mat_out[7] = 0.0f;
	mat_out[11] = 0.0f;
	mat_out[15] = 1.0f;
}

#include <d3d9.h>

void _stdcall depthbiaswrapper(IDirect3DDevice9 *device, uint32_t state, float value) {
	HRESULT (_fastcall *setrenderstate)(IDirect3DDevice9 *device, void *pad, IDirect3DDevice9 *alsodevice, uint32_t state, float value) = (void *)device->lpVtbl->SetRenderState;
	float *origdepthbias = 0x00859168;

	value = value;

	printf("DEPTH BIAS: %f (0x%08x), %f\n", *origdepthbias, *origdepthbias, value);

	D3DRS_FOGCOLOR;
	//D3DRS_FOG

	//IDirect3DDevice9_SetRenderState(device, state, value);
	setrenderstate(device, NULL, device, state, value);
}

void patchCamera() {
	// remove camera math
	patchNop(addr_camera_lookat, 12);

	// pass in plain forward vector
	patchByte(addr_camera_lookat + 49 + 3, 0x64);
	patchByte(addr_camera_lookat + 55 + 3, 0x6c);
	patchByte(addr_camera_lookat + 61 + 3, 0x74);

	patchCall(addr_camera_lookat + 67, fixedcamera);
	
	// depth bias tests
	//patchNop(0x0053808d, 6);
	//patchCall(0x0053808d, depthbiaswrapper);

	// fog tests
	//patchByte(0x00527d88 + 1, 1);
	//patchNop(0x00527d8a, 5);

	//patchNop(0x0052812c, 5);
	//patchNop(0x0053225b, 5);
	//patchNop(0x0059b5e0, 5);

	//patchNop(0x00509244, 5);
}

struct flashVertex {
	float x, y, z, w;
	uint32_t color;
	float u, v;
};

// vertices are passed into the render function in the wrong order when drawing screen flashes; reorder them before passing to draw
void __fastcall reorder_flash_vertices(uint32_t *d3dDevice, uint32_t *maybedeviceagain, uint32_t *alsodevice, uint32_t prim, uint32_t count, struct flashVertex *vertices, uint32_t stride){
	void(__fastcall *drawPrimitiveUP)(void *, void *, void *, uint32_t, uint32_t, struct flashVertex *, uint32_t) = maybedeviceagain[83];

	struct flashVertex tmp;

	tmp = vertices[0];
	vertices[0] = vertices[1];
	vertices[1] = vertices[2];
	vertices[2] = tmp;

	drawPrimitiveUP(d3dDevice, maybedeviceagain, alsodevice, prim, count, vertices, stride);
}

void patchScreenFlash() {
	patchNop(0x004b91e7, 6);
	patchCall(0x004b91e7, reorder_flash_vertices);
}

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

void installPatches() {
	patchWindow();
	patchInput();
	patchPlaylistShuffle();
	patchCamera();
	printf("installing script patches\n");
	patchScriptHook();
	printf("done\n");
}

/*uint32_t __fastcall calcAvailableSpaceFudged(uint8_t *param1) {
	uint32_t *pTotalSpace = 0x0068ec38;
	if (*(param1 + 0xc)) {
		uint64_t freeSpace = 0;
		uint64_t totalSpace = 0;
		GetDiskFreeSpaceEx(NULL, &freeSpace, &totalSpace, NULL);


	} else {
		return 0;
	}
}*/

char *allocOnlineServiceString(char *fmt, char *url) {
	size_t outsize = strlen(fmt) + strlen(url) + 1;	// most likely oversized but it doesn't matter
	char *result = malloc(outsize);

	sprintf(result, fmt, url);

	printf("ALLOC'D STRING %s\n", result);

	return result;
}

void patchOnlineService(char *configFile) {
	// NOTE: these will leak and that's a-okay
	char *url[256];
	GetPrivateProfileString("Miscellaneous", "OnlineDomain", "openspy.net", url, 256, configFile);

	// gamestats.gamespy.com
	char *gamestats = allocOnlineServiceString("gamestats.%s", url);
	patchDWord(0x0054dd13 + 1, gamestats);
	patchDWord(0x006025d8 + 1, gamestats);
	patchDWord(0x00602618 + 1, gamestats);
	patchDWord(0x00602650 + 2, gamestats);

	// %s.available.gamespy.com
	char *available = allocOnlineServiceString("%%s.available.%s", url);
	patchDWord(0x005f86dd + 1, available);

	// %s.master.gamespy.com
	char *master = allocOnlineServiceString("%%s.master.%s", url);
	patchDWord(0x005fb03b + 1, master);

	// natneg2.gamespy.com
	char *natneg2 = allocOnlineServiceString("natneg2.%s", url);
	patchDWord(0x0068e380, natneg2);

	// natneg1.gamespy.com
	char *natneg1 = allocOnlineServiceString("natneg1.%s", url);
	patchDWord(0x0068e37c, natneg1);

	// http://motd.gamespy.com/motd/motd.asp?userid=%d&productid=%d&versionuniqueid=%s&distid=%d&uniqueid=%s&gamename=%s
	char *motd = allocOnlineServiceString("http://motd.%s/motd/motd.asp?userid=%%d&productid=%%d&versionuniqueid=%%s&distid=%%d&uniqueid=%%s&gamename=%%s", url);
	patchDWord(0x005ffd05 + 1, motd);

	// peerchat.gamespy.com
	char *peerchat = allocOnlineServiceString("peerchat.%s", url);
	patchDWord(0x00605a4c + 1, peerchat);
	patchDWord(0x00605aac + 1, peerchat);
	patchDWord(0x00605b0e + 1, peerchat);

	// %s.ms%d.gamespy.com
	char *ms = allocOnlineServiceString("%%s.ms%%d.%s", url);
	patchDWord(0x00612085 + 1, ms);

	// gpcm.gamespy.com
	char *gpcm = allocOnlineServiceString("gpcm.%s", url);
	patchDWord(0x00617339 + 1, gpcm);

	// gpsp.gamespy.com
	char *gpsp = allocOnlineServiceString("gpsp.%s", url);
	patchDWord(0x006182bd + 1, gpsp);
}

uint32_t rng_seed = 0;

void initPatch() {
	GetModuleFileName(NULL, &executableDirectory, filePathBufLen);

	// find last slash
	char *exe = strrchr(executableDirectory, '\\');
	if (exe) {
		*(exe + 1) = '\0';
	}

	char configFile[1024];
	sprintf(configFile, "%s%s", executableDirectory, CONFIG_FILE_NAME);

	int isDebug = getIniBool("Miscellaneous", "Debug", 0, configFile);

	if (isDebug) {
		AllocConsole();

		FILE *fDummy;
		freopen_s(&fDummy, "CONIN$", "r", stdin);
		freopen_s(&fDummy, "CONOUT$", "w", stderr);
		freopen_s(&fDummy, "CONOUT$", "w", stdout);
	}
	printf("PARTYMOD for THUG2 %d.%d.%d\n", VERSION_NUMBER_MAJOR, VERSION_NUMBER_MINOR, VERSION_NUMBER_FIX);

	printf("DIRECTORY: %s\n", executableDirectory);

	//init_patch_cache();

	//findOffsets();
	//installPatches();

	//initScriptPatches();

	// get some source of entropy for the music randomizer
	rng_seed = time(NULL) & 0xffffffff;
	srand(rng_seed);

	printf("Patch Initialized\n");

	patchOnlineService(configFile);

	printf("BASE ADDR: 0x%08x, LEN: %d\n", base_addr, mod_size);
	printf("FOUND ADDR: 0x%08x\n", initAddr);
}

void patchHwType() {
	patchByte(0x006283a5 + 6, 0x05);
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

			patchWindow();
			patchInput();
			patchScriptHook();
			patchScreenFlash();
			patchPlaylistShuffle();

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
