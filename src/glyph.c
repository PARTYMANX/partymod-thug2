#include <glyph.h>

#include <stdint.h>

#include <config.h>
#include <patch.h>
#include <input.h>

char *buttonsPs2 = "ButtonsPs2";
char *buttonsNgc = "ButtonsNgc";
char *meta_button_map_ps2 = "meta_button_map_ps2";
char *meta_button_map_gamecube = "meta_button_map_gamecube";

// in the pc release, most button tags were changed from meta button mapped values to explicit buttons
// this wrapper re-translates them into rough equivalents for a controller using xbox layout
uint32_t dehexifyDigitWrapper(uint8_t *button) {
	uint32_t (*orig_dehexify)(uint8_t *) = 0x004020e0;

	if (getUsingKeyboard()) {
		return orig_dehexify(button);
	} else {
		uint8_t val = *button;
		switch (val) {
		case 0x4D:
		case 0x6D:
			return 3;
		case 0x4E:
		case 0x6E:
			return 2;
		case 0x4F:
		case 0x6F:
			return 1;
		case 0x50:
		case 0x70:
			return 0;
		case 0x51:
		case 0x71:
			return 14;
		case 0x52:
		case 0x72:
			return 15;
		case 0x53:
		case 0x73:
			return 16;
		case 0x54:
		case 0x74:
			return 17;
		default:
			return orig_dehexify(button);
		}
	}
}

uint8_t __cdecl shouldUseGlyph(uint8_t idx) {
	if (getUsingKeyboard()) {
		return idx >= 0x04 && idx <= 0x0D;
	} else {
		return idx <= 17;
	}
}

uint8_t getGlyphStyleSetting() {
	return getConfigInt(CONFIG_MISC_SECTION, "GlyphStyle", 1);
}

void patchButtonGlyphs() {
	uint8_t glyphStyle = getGlyphStyleSetting();

	// load the appropriate glyph font and meta button map for glyph style (this only affects the basic menu control prompts)
	if (glyphStyle == 1) {
		patchDWord(0x0048d97f + 4, buttonsPs2);
		patchDWord(0x0048d8f7 + 1, meta_button_map_ps2);
	} else if (glyphStyle == 2) {
		patchDWord(0x0048d97f + 4, buttonsNgc);
		patchDWord(0x0048d8f7 + 1, meta_button_map_gamecube);
	}

	// SText::Draw
	// wrap dehexify to convert to correct glyph
	patchCall(0x004cff0c, dehexifyDigitWrapper); 
	
	// use custom logic to determine when to use a key string or glyph
	// PUSH EAX
	patchByte(0x004cff36, 0x67);
	patchByte(0x004cff36 + 1, 0x50);
	
	patchCall(0x004cff36 + 2, shouldUseGlyph);

	// CMP AL, 0x1
	patchByte(0x004cff36 + 7, 0x3c);
	patchByte(0x004cff36 + 8, 0x01);

	// restore EAX with a pop
	patchByte(0x004cff36 + 9, 0x67);
	patchByte(0x004cff36 + 10, 0x58);

	// JZ 004cffa6
	patchByte(0x004cff36 + 11, 0x74);
	patchByte(0x004cff36 + 12, 0x63);

	// NOP the rest
	patchNop(0x004cff36 + 13, 3);

	// SText::QueryString
	// wrap dehexify to convert to correct glyph
	patchCall(0x004ced4b, dehexifyDigitWrapper);

	// use custom logic to determine when to use a key string or glyph
	// PUSH EAX
	patchByte(0x004ced6d, 0x67);
	patchByte(0x004ced6d + 1, 0x50);
	
	patchCall(0x004ced6d + 2, shouldUseGlyph);

	// CMP AL, 0x1
	patchByte(0x004ced6d + 7, 0x3c);
	patchByte(0x004ced6d + 8, 0x01);

	// restore EAX with a pop
	patchByte(0x004ced6d + 9, 0x67);
	patchByte(0x004ced6d + 10, 0x58);

	// JZ 004cedd5
	patchByte(0x004ced6d + 11, 0x74);
	patchByte(0x004ced6d + 12, 0x5b);

	// NOP the rest
	patchNop(0x004ced6d + 13, 3);
}
