/* game.c -- Bully: Anniversary Edition (libBully.so): hooks. From bully_vita
 * (Andy Nguyen, Rinnegatamante; MIT): no TouchSense, the archive index cache,
 * Continue loads the newest save, Exit quits. Its Vita-only rendering work
 * (precompiled shader replacements, vitaGL tuning) and video player are left
 * out: the game's own GLSL ES shaders run on Mali-400 as on Android; the
 * story video plays through movie.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "android.h"
#include "so_util.h"
#include "util.h"

const char *game_so_name = "libBully.so";
const char *const game_required_files[] = { "libBully.so", "Android/main.obb", "Android/patch.obb", NULL };

const JavaMethod game_java_methods[] = {
	COMMON_JAVA_METHODS,
	{ "ShareText", java_ignore },
	{ "ShareImage", java_ignore },
	{ NULL, NULL },
};

static void *TouchSense__TouchSense(void *this)
{
	return this;
}

/* ---- the OBBs stay zips; the game sorts each one's entries once and keeps
 * an index next to it (game/Android/main.obb.idx, ...) ---- */

typedef struct {
	uint32_t dataOffset;
	uint32_t dataSize;
	uint16_t nameLength;
	char name[0];
} __attribute__((__packed__)) IDXEntry;

typedef struct {
	uint32_t dataOffset;
	uint32_t dataSize;
	char *name;
} ZIPEntry;

typedef struct {
	void *vtable;
	uint32_t unk4;
	uint32_t numEntries;
	ZIPEntry *entries;
	char file[256];
	uint32_t unk110;
	uint32_t unk114;
} ZIPFile;

static int (*ZIPFile__EntryCompare)(const void *a, const void *b);

static void ZIPFile__SortEntries(ZIPFile *this)
{
	if (this->numEntries > 1) {
		for (uint32_t i = 0; i < this->numEntries - 1; i++) {
			if (ZIPFile__EntryCompare(&this->entries[i], &this->entries[i + 1]) > 0) {
				qsort(this->entries, this->numEntries, sizeof(ZIPEntry), ZIPFile__EntryCompare);
				break;
			}
		}
	}

	char path[sizeof(fs_root) + sizeof(this->file) + 8];
	snprintf(path, sizeof(path), "%s%s.idx", fs_root, this->file);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(&this->numEntries, 1, sizeof(uint32_t), f);
	for (uint32_t i = 0; i < this->numEntries; i++) {
		IDXEntry e;
		e.dataOffset = this->entries[i].dataOffset;
		e.dataSize = this->entries[i].dataSize;
		e.nameLength = strlen(this->entries[i].name);
		fwrite(&e, 1, sizeof(IDXEntry), f);
		fwrite(this->entries[i].name, 1, e.nameLength, f);
	}
	fclose(f);
}

/* ---- Continue loads the newest of the five saves ---- */

static void (*FadeLoadScene__Loading)(int result);
static int (*cMemCard__HasSave)(int save);
static int (*BullyApplication__OrigLoadSlot)(void *this, int slot);
static uint64_t (*OS_FileGetDate)(int area, const char *path);

static int BullyApplication__OrigContinue(void *this, int a2, int a3, int a4)
{
	FadeLoadScene__Loading(*((uintptr_t *)this + 29));
	uint64_t latest = 0;
	int slot = 0;
	if (!*((char *)this + 140)) {
		for (int i = 0; i < 5; i++) {
			if (!cMemCard__HasSave(i))
				continue;
			char filename[16];
			snprintf(filename, sizeof(filename), "BullyFile%d", i);
			uint64_t date = OS_FileGetDate(1, filename);
			if (latest < date) {
				latest = date;
				slot = i;
			}
		}
	}
	return BullyApplication__OrigLoadSlot(this, slot);
}

static int Application__Exit(void *this)
{
	exit_game(0);
}

/* NvFEOF and OS_FileRead read the end-of-file flag straight out of the FILE
 * as bionic lays it out (flags at +12, __SEOF 0x20); the FILE is glibc's
 * here, so a short last read counted as an error, not the end of the file
 * (in GTA III / Vice City, same engine code, CFileMgr::ReadLine then dropped
 * every loose file's last partial chunk). Other handles (the APK's) keep
 * the game's own check. */
static int (*NvAPKEOF)(void *h);

static int NvFEOF(const uintptr_t *f)
{
	return f[0] == 1 ? feof((FILE *)f[1]) != 0 : NvAPKEOF((void *)f[1]);
}

/* ---- the pause menu's map: MenuMap::InputEvent looks up its touch help
 * button ("main.help") and, if it isn't there, goes on with NULL and reads
 * its visible flag (ldrb.w r1, [fp, #40] at +0x7a8d06): a crash on opening
 * the pause menu. That one instruction becomes a call to menumap_help:
 * with the button it does the load and returns; without it, it goes on
 * past the button's touch test (+0x7a8d4c), with r5 as the code there
 * expects it (the input state, *GOT). The call reaches our code through a
 * jump in TouchSense::playBuiltinEffect, which is never run (hooked). ---- */

#define MENUMAP_HELP_LOAD 0x7A8D06
#define MENUMAP_AFTER_HELP 0x7A8D4C
#define MENUMAP_R5_LITERAL 0x7A918C /* + pc at 0x7a8d0a = the GOT slot */
#define MENUMAP_R5_PC 0x7A8D0E
#define MENUMAP_JUMP 0x973290 /* inside playBuiltinEffect, past its hook */

/* in this order: the stub reads both from the first one's address */
struct { uintptr_t r5_slot, resume; } menumap __attribute__((used));
#define menumap_r5_slot menumap.r5_slot
#define menumap_resume menumap.resume

__attribute__((naked, target("thumb"))) static void menumap_help(void)
{
	__asm__ volatile(
		"cmp fp, #0\n"
		"beq 1f\n"
		"ldrb r1, [fp, #40]\n"
		"bx lr\n"
		"1:\n"
		"ldr r5, 3f\n" /* pc-relative: no text relocation */
		"2: add r5, pc\n"
		"ldr ip, [r5, #4]\n" /* menumap_resume */
		"ldr r5, [r5]\n" /* menumap_r5_slot */
		"ldr r5, [r5]\n"
		"bx ip\n"
		".balign 4\n"
		"3: .word menumap - (2b + 4)\n");
}

/* Thumb-2 BL from (so offset) to (so offset) */
static void patch_bl_to(uintptr_t from, uintptr_t to)
{
	int32_t off = (int32_t)(to - (from + 4));
	uint32_t s = (off >> 24) & 1, i1 = (off >> 23) & 1, i2 = (off >> 22) & 1;
	uint32_t j1 = !(i1 ^ s), j2 = !(i2 ^ s);
	uint16_t hw[2] = {
		(uint16_t)(0xF000 | (s << 10) | ((off >> 12) & 0x3FF)),
		(uint16_t)(0xD000 | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FF)),
	};
	so_patch(from, hw, 4);
}

static void patch_menumap(void)
{
	static const uint8_t ldrb_help[4] = { 0x9b, 0xf8, 0x28, 0x10 };
	if (memcmp((void *)(so_base + MENUMAP_HELP_LOAD), ldrb_help, 4) != 0) {
		log_printf("game: MenuMap help patch not applied\n");
		return;
	}
	menumap_r5_slot = so_base + MENUMAP_R5_PC + *(uint32_t *)(so_base + MENUMAP_R5_LITERAL);
	menumap_resume = (so_base + MENUMAP_AFTER_HELP) | 1;
	hook_addr((so_base + MENUMAP_JUMP) | 1, (uintptr_t)menumap_help);
	patch_bl_to(MENUMAP_HELP_LOAD, MENUMAP_JUMP);
}

/* videos: movie.c plays the converted ones (tools/movies.sh: the logo,
 * Rockstar.m4v, and the opening, AttractMode.mp4), loose in game/ or else in
 * place from the patch OBB, drawn by the render thread while the game, on
 * its own thread, waits on AND_IsMoviePlaying as it does for Android's
 * player. */
static SOFTFP int OS_MoviePlay(const char *file, int a2, int a3, float a4)
{
	movie_play(file);
	return 0;
}

static int AND_IsMoviePlaying(void) { return movie_playing(); }

/* the game's own OS_MovieIsPlaying also stores a movie state that only its
 * OS_MoviePlay sets (hooked above): left to the game, the main thread spun
 * at start-up. Answered here, the state left alone as before. */
static int OS_MovieIsPlaying(int *state) { return movie_playing(); }
static void AND_StopMovie(void) { movie_stop(); }

/* While a video plays, the game thread waits in OrigNewGame / TheGame and
 * hands the render thread no frames; that thread idles in
 * GameRenderer::ConcurrentRenderThread (CheckPauseState, SleepThread, ...)
 * holding the screen, so the video's pictures are drawn there: its one
 * SleepThread call (+0x89417e) goes through render_idle, which shows a new
 * picture when there is one and then sleeps as asked. Reached through a jump
 * in TouchSense::playBuiltinEffectInternal, which is never run (hooked). */
#define RENDER_IDLE_SLEEP 0x89417E
#define RENDER_IDLE_JUMP 0x9733C8 /* inside playBuiltinEffectInternal, past its hook */

static void (*SleepThread)(unsigned ms);

static void render_idle(unsigned ms)
{
	movie_present(); /* a video on: its newest picture */
	SleepThread(ms);
}

static void patch_render_idle(void)
{
	static const uint8_t bl_sleep[4] = { 0x58, 0xf0, 0xfb, 0xf8 };
	SleepThread = (void *)so_find_addr("_Z11SleepThreadj");
	if (!SleepThread || memcmp((void *)(so_base + RENDER_IDLE_SLEEP), bl_sleep, 4) != 0) {
		log_printf("game: render idle patch not applied (videos stay black)\n");
		return;
	}
	hook_addr((so_base + RENDER_IDLE_JUMP) | 1, (uintptr_t)render_idle);
	patch_bl_to(RENDER_IDLE_SLEEP, RENDER_IDLE_JUMP);
}

/* the game's per-frame event poll; GAME_MOVIE_TEST starts its video here */
static int ProcessEventsBully(void)
{
	const char *test = movie_test_due();
	if (test)
		OS_MoviePlay(test, 0, 0, 1.0f);
	return ProcessEvents();
}

/* the videos stay in the patch OBB (stored, not compressed): movie.c reads
 * them in place */
static const char *const movie_obbs[] = { "Android/patch.obb", "Android/main.obb", NULL };

void patch_game(void)
{
	movie_archives(movie_obbs);
	/* NVThreadGetCurrentJNIEnv stays the game's: it keeps the env in a
	 * pthread key (glibc's here) that NVThreadSpawnJNIThread's threads read
	 * back; the Vita loader hooks it because its keys are stubs */
	hook_sym("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority", OS_ThreadLaunch);
	hook_sym("_Z13OS_ThreadWaitPv", OS_ThreadWait);
	hook_sym("_Z17OS_ScreenGetWidthv", OS_ScreenGetWidth);
	hook_sym("_Z18OS_ScreenGetHeightv", OS_ScreenGetHeight);
	hook_sym("_Z20AND_SystemInitializev", ret0);
	hook_sym("_Z13ProcessEventsb", ProcessEventsBully);

	hook_sym("_ZN10TouchSenseC2Ev", TouchSense__TouchSense);
	hook_sym("_ZN10TouchSense20stopContinuousEffectEv", ret0);
	hook_sym("_ZN10TouchSense14stopAllEffectsEv", ret0);
	hook_sym("_ZN10TouchSense28startContinuousBuiltinEffectEiiii", ret0);
	hook_sym("_ZN10TouchSense25playBuiltinEffectInternalEii", ret0);
	hook_sym("_ZN10TouchSense17playBuiltinEffectEiiii", ret0);
	patch_menumap(); /* after the hook above: it uses that function's body */
	patch_render_idle(); /* likewise, in playBuiltinEffectInternal's */

	ZIPFile__EntryCompare = (void *)so_find_addr("_ZN7ZIPFile12EntryCompareEPKvS1_");
	hook_sym("_ZN7ZIPFile11SortEntriesEv", ZIPFile__SortEntries);

	hook_sym("_ZN11Application4ExitEv", Application__Exit);

	FadeLoadScene__Loading = (void *)so_find_addr("_ZN13FadeLoadScene7LoadingEv");
	cMemCard__HasSave = (void *)so_find_addr("_ZN8cMemCard7HasSaveE11MemCardSlot");
	BullyApplication__OrigLoadSlot = (void *)so_find_addr("_ZN16BullyApplication12OrigLoadSlotE11MemCardSlot");
	OS_FileGetDate = (void *)so_find_addr("_Z14OS_FileGetDate14OSFileDataAreaPKc");
	hook_sym("_ZN16BullyApplication12OrigContinueEv", BullyApplication__OrigContinue);

	NvAPKEOF = (void *)so_find_addr("_Z8NvAPKEOFPv");
	hook_sym("_Z6NvFEOFPv", NvFEOF);
	/* OS_FileRead's own copy of the check, on a raw FILE: a short read is
	 * the end of the file (ldrh r3, [r3, #12] -> b to its return 2) */
	static const uint8_t ldrh_flags[2] = { 0x9b, 0x89 }, b_eof[2] = { 0xeb, 0xe7 };
	if (memcmp((void *)(so_base + 0x974A38), ldrh_flags, 2) == 0)
		so_patch(0x974A38, b_eof, 2);
	else
		log_printf("game: OS_FileRead end-of-file patch not applied\n");

	/* OS_MovieSetSkippable and OS_MovieStop are 4-byte branches: a hook
	 * (8 bytes) would overwrite the next function, so their targets */
	hook_sym("_Z12OS_MoviePlayPKcbbf", OS_MoviePlay);
	hook_sym("_Z20AND_MovieIsSkippableb", ret0);
	hook_sym("_Z13AND_StopMoviev", AND_StopMovie);
	hook_sym("_Z17OS_MovieIsPlayingPi", OS_MovieIsPlaying);
	hook_sym("_Z18AND_IsMoviePlayingv", AND_IsMoviePlaying);
}
