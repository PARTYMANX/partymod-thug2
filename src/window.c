#include <config.h>

#include <windows.h>

#include <stdio.h>
#include <stdint.h>

#include <event.h>
#include <log.h>
#include <patch.h>
#include <input.h>
#include <global.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

// there has got to be a better way to write this
uint8_t *isFullscreen = 0x00858da7;
HWND *hwnd = 0x007d6a28;
int *isFocused = 0x007ccbe4;
int *other_isFocused = 0x0072e850;
uint32_t *resolution_setting = 0x007d643c;
uint8_t *addr_recreatedevice = 0x00786ab4;

uint8_t resbuffer[100000];	// buffer needed for high resolutions

uint8_t isWindowed = 0;
uint8_t borderless;

int resX;
int resY;
float aspect_ratio;

SDL_Window *window;

void enforceMaxResolution() {
	DEVMODE deviceMode;
	int i = 0;
	uint8_t isValidX = 0;
	uint8_t isValidY = 0;

	while (EnumDisplaySettings(NULL, i, &deviceMode)) {
		if (deviceMode.dmPelsWidth >= resX) {
			isValidX = 1;
		}
		if (deviceMode.dmPelsHeight >= resY) {
			isValidY = 1;
		}

		i++;
	}

	if (!isValidX || !isValidY) {
		resX = 0;
		resY = 0;
	}
}

void loadWindowSettings();

void handleWindowEvent(SDL_Event *e) {
	int *recreateDevice = (int *)addr_recreatedevice;

	switch (e->type) {
		case SDL_WINDOWEVENT:
			if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				*recreateDevice = 0;

				*isFocused = 0;
			} else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				*recreateDevice = 1;

				*isFocused = 1;
			}
			return;
		default:
			return;
	}
}

void createSDLWindow() {
	loadWindowSettings();

	registerEventHandler(handleWindowEvent);

	SDL_Init(SDL_INIT_VIDEO);
	//isWindowed = 1;

	SDL_WindowFlags flags = isWindowed ? SDL_WINDOW_SHOWN : SDL_WINDOW_FULLSCREEN;

	if (borderless && isWindowed) {
		flags |= SDL_WINDOW_BORDERLESS;
	}

	//*isFullscreen = !isWindowed;
	if (isWindowed) {
		patchByte(0x004d871f + 1, 0x05);	// set fullscreen to 0
	}

	enforceMaxResolution();

	resolution_setting = 0;

	if (resX == 0 || resY == 0) {
		SDL_DisplayMode displayMode;
		SDL_GetDesktopDisplayMode(0, &displayMode);
		resX = displayMode.w;
		resY = displayMode.h;
	}
		
	if (resX < 640) {
		resX = 640;
	}
	if (resY < 480) {
		resY = 480;
	}

	window = SDL_CreateWindow("THUG2 - PARTYMOD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, resX, resY, flags);   // TODO: fullscreen

	if (!window) {
		log_printf(LL_ERROR, "Failed to create window! Error: %s\n", SDL_GetError());
	}

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);
	*hwnd = wmInfo.info.win.window;

	*isFocused = 1;

	// patch resolution setting
	patchDWord(0x004d8772 + 4, resX);
	patchDWord(0x004d878e + 4, resY);
	
	SDL_ShowCursor(0);
}

void getWindowDimensions(uint32_t *width, uint32_t *height) {
	if (width) {
		*width = resX;
	}

	if (height) {
		*height = resY;
	}
}

void patchWindow() {
	// replace the window with an SDL2 window.  this kind of straddles the line between input and config
	patchCall(0x004e2767, createSDLWindow);
	
	patchDWord(0x004b6c63 + 1, &resbuffer);

	patchNop(0x004d8891, 14);	// don't move window to corner

	patchByte(0x004d8989, 0xeb);	// don't hide cursor

	patchByte(0x004d869c, 0xeb);	// don't call showwindow
	patchNop(0x004e277d, 20);	// don't call showwindow
}

void loadWindowSettings() {
	resX = getConfigInt(CONFIG_GRAPHICS_SECTION, "ResolutionX", 0);
	resY = getConfigInt(CONFIG_GRAPHICS_SECTION, "ResolutionY", 0);
	if (!isWindowed) {
		isWindowed = getConfigBool(CONFIG_GRAPHICS_SECTION, "Windowed", 0);
	}

	borderless = getConfigBool(CONFIG_GRAPHICS_SECTION, "Borderless", 0);
}

