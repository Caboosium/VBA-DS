#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdarg.h>
#include <string.h>
#include "Cartridge.h"
#include "Display.h"
#include "GBA.h"
#include "CPU.h"
#include "MMU.h"
#include "Globals.h"
#include "Gfx.h"
#include "CartridgeRTC.h"
#include "Savestate.h"
#include "Sound.h"
#include "../common/Util.h"
#include "../common/Port.h"
#include "../common/Loader.h"
#include "../common/Settings.h"

#ifdef LINK_EMULATION
#include "Link.h"
#endif

#ifdef __GNUC__
#define _stricmp strcasecmp
#endif

static int IRQTicks = 0;

static int layerEnableDelay = 0;
static int cpuDmaTicksToUpdate = 0;

static bool cpuBreakLoop = false;
int cpuNextEvent = 0;

static bool intState = false;
bool stopState = false;
gboolean holdState = false;

int cpuTotalTicks = 0;

static int lcdTicks = 1008;
static u8 timerOnOffDelay = 0;
u16 timer0Value = 0;
bool timer0On = false;
int timer0Ticks = 0;
int timer0Reload = 0;
int timer0ClockReload  = 0;
u16 timer1Value = 0;
bool timer1On = false;
int timer1Ticks = 0;
int timer1Reload = 0;
int timer1ClockReload  = 0;
u16 timer2Value = 0;
bool timer2On = false;
int timer2Ticks = 0;
int timer2Reload = 0;
int timer2ClockReload  = 0;
u16 timer3Value = 0;
bool timer3On = false;
int timer3Ticks = 0;
int timer3Reload = 0;
int timer3ClockReload  = 0;
static u32 dma0Source = 0;
static u32 dma0Dest = 0;
static u32 dma1Source = 0;
static u32 dma1Dest = 0;
static u32 dma2Source = 0;
static u32 dma2Dest = 0;
static u32 dma3Source = 0;
static u32 dma3Dest = 0;
static gint64 lastTime = 0;
static guint speed = 0;
static int count = 0;

static InputDriver *inputDriver = NULL;

static const int TIMER_TICKS[4] =
{
	0,
	6,
	8,
	10
};

static const u8 gamepakRamWaitState[4] = { 4, 3, 2, 8 };
static const u8 gamepakWaitState[4] =  { 4, 3, 2, 8 };
static const u8 gamepakWaitState0[2] = { 2, 1 };
static const u8 gamepakWaitState1[2] = { 4, 1 };
static const u8 gamepakWaitState2[2] = { 8, 1 };

u8 memoryWait[16] =
    { 0, 0, 2, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 0 };
u8 memoryWait32[16] =
    { 0, 0, 5, 0, 0, 1, 1, 0, 7, 7, 9, 9, 13, 13, 4, 0 };
u8 memoryWaitSeq[16] =
    { 0, 0, 2, 0, 0, 0, 0, 0, 2, 2, 4, 4, 8, 8, 4, 0 };
u8 memoryWaitSeq32[16] =
    { 0, 0, 5, 0, 0, 1, 1, 0, 5, 5, 9, 9, 17, 17, 4, 0 };

// The videoMemoryWait constants are used to add some waitstates
// if the opcode access video memory data outside of vblank/hblank
// It seems to happen on only one ticks for each pixel.
// Not used for now (too problematic with current code).
//const u8 videoMemoryWait[16] =
//  {0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};


u8 biosProtected[4];

static variable_desc saveGameStruct[] =
{
	{ &DISPCNT  , sizeof(u16) },
	{ &DISPSTAT , sizeof(u16) },
	{ &VCOUNT   , sizeof(u16) },
	{ &BG0CNT   , sizeof(u16) },
	{ &BG1CNT   , sizeof(u16) },
	{ &BG2CNT   , sizeof(u16) },
	{ &BG3CNT   , sizeof(u16) },
	{ &BG0HOFS  , sizeof(u16) },
	{ &BG0VOFS  , sizeof(u16) },
	{ &BG1HOFS  , sizeof(u16) },
	{ &BG1VOFS  , sizeof(u16) },
	{ &BG2HOFS  , sizeof(u16) },
	{ &BG2VOFS  , sizeof(u16) },
	{ &BG3HOFS  , sizeof(u16) },
	{ &BG3VOFS  , sizeof(u16) },
	{ &BG2PA    , sizeof(u16) },
	{ &BG2PB    , sizeof(u16) },
	{ &BG2PC    , sizeof(u16) },
	{ &BG2PD    , sizeof(u16) },
	{ &BG2X_L   , sizeof(u16) },
	{ &BG2X_H   , sizeof(u16) },
	{ &BG2Y_L   , sizeof(u16) },
	{ &BG2Y_H   , sizeof(u16) },
	{ &BG3PA    , sizeof(u16) },
	{ &BG3PB    , sizeof(u16) },
	{ &BG3PC    , sizeof(u16) },
	{ &BG3PD    , sizeof(u16) },
	{ &BG3X_L   , sizeof(u16) },
	{ &BG3X_H   , sizeof(u16) },
	{ &BG3Y_L   , sizeof(u16) },
	{ &BG3Y_H   , sizeof(u16) },
	{ &WIN0H    , sizeof(u16) },
	{ &WIN1H    , sizeof(u16) },
	{ &WIN0V    , sizeof(u16) },
	{ &WIN1V    , sizeof(u16) },
	{ &WININ    , sizeof(u16) },
	{ &WINOUT   , sizeof(u16) },
	{ &MOSAIC   , sizeof(u16) },
	{ &BLDMOD   , sizeof(u16) },
	{ &COLEV    , sizeof(u16) },
	{ &COLY     , sizeof(u16) },
	{ &DM0SAD_L , sizeof(u16) },
	{ &DM0SAD_H , sizeof(u16) },
	{ &DM0DAD_L , sizeof(u16) },
	{ &DM0DAD_H , sizeof(u16) },
	{ &DM0CNT_L , sizeof(u16) },
	{ &DM0CNT_H , sizeof(u16) },
	{ &DM1SAD_L , sizeof(u16) },
	{ &DM1SAD_H , sizeof(u16) },
	{ &DM1DAD_L , sizeof(u16) },
	{ &DM1DAD_H , sizeof(u16) },
	{ &DM1CNT_L , sizeof(u16) },
	{ &DM1CNT_H , sizeof(u16) },
	{ &DM2SAD_L , sizeof(u16) },
	{ &DM2SAD_H , sizeof(u16) },
	{ &DM2DAD_L , sizeof(u16) },
	{ &DM2DAD_H , sizeof(u16) },
	{ &DM2CNT_L , sizeof(u16) },
	{ &DM2CNT_H , sizeof(u16) },
	{ &DM3SAD_L , sizeof(u16) },
	{ &DM3SAD_H , sizeof(u16) },
	{ &DM3DAD_L , sizeof(u16) },
	{ &DM3DAD_H , sizeof(u16) },
	{ &DM3CNT_L , sizeof(u16) },
	{ &DM3CNT_H , sizeof(u16) },
	{ &TM0D     , sizeof(u16) },
	{ &TM0CNT   , sizeof(u16) },
	{ &TM1D     , sizeof(u16) },
	{ &TM1CNT   , sizeof(u16) },
	{ &TM2D     , sizeof(u16) },
	{ &TM2CNT   , sizeof(u16) },
	{ &TM3D     , sizeof(u16) },
	{ &TM3CNT   , sizeof(u16) },
	{ &P1       , sizeof(u16) },
	{ &IE       , sizeof(u16) },
	{ &IF       , sizeof(u16) },
	{ &IME      , sizeof(u16) },
	{ &holdState, sizeof(bool) },
	{ &lcdTicks, sizeof(int) },
	{ &timer0On , sizeof(bool) },
	{ &timer0Ticks , sizeof(int) },
	{ &timer0Reload , sizeof(int) },
	{ &timer0ClockReload  , sizeof(int) },
	{ &timer1On , sizeof(bool) },
	{ &timer1Ticks , sizeof(int) },
	{ &timer1Reload , sizeof(int) },
	{ &timer1ClockReload  , sizeof(int) },
	{ &timer2On , sizeof(bool) },
	{ &timer2Ticks , sizeof(int) },
	{ &timer2Reload , sizeof(int) },
	{ &timer2ClockReload  , sizeof(int) },
	{ &timer3On , sizeof(bool) },
	{ &timer3Ticks , sizeof(int) },
	{ &timer3Reload , sizeof(int) },
	{ &timer3ClockReload  , sizeof(int) },
	{ &dma0Source , sizeof(u32) },
	{ &dma0Dest , sizeof(u32) },
	{ &dma1Source , sizeof(u32) },
	{ &dma1Dest , sizeof(u32) },
	{ &dma2Source , sizeof(u32) },
	{ &dma2Dest , sizeof(u32) },
	{ &dma3Source , sizeof(u32) },
	{ &dma3Dest , sizeof(u32) },
	{ &CPU::N_FLAG , sizeof(bool) },
	{ &CPU::C_FLAG , sizeof(bool) },
	{ &CPU::Z_FLAG , sizeof(bool) },
	{ &CPU::V_FLAG , sizeof(bool) },
	{ &CPU::armState , sizeof(bool) },
	{ &CPU::armIrqEnable , sizeof(bool) },
	{ &CPU::armNextPC , sizeof(u32) },
	{ &CPU::armMode , sizeof(int) },
	{ &stopState , sizeof(bool) },
	{ &IRQTicks , sizeof(int) },
	{ NULL, 0 }
};

static inline void UPDATE_REG(u32 address, u16 value)
{
	WRITE16LE(((u16 *)&ioMem[address]), value);
}

static inline int CPUUpdateTicks()
{
	int cpuLoopTicks = lcdTicks;

	if (soundTicks < cpuLoopTicks)
		cpuLoopTicks = soundTicks;

	if (timer0On && (timer0Ticks < cpuLoopTicks))
	{
		cpuLoopTicks = timer0Ticks;
	}
	if (timer1On && !(TM1CNT & 4) && (timer1Ticks < cpuLoopTicks))
	{
		cpuLoopTicks = timer1Ticks;
	}
	if (timer2On && !(TM2CNT & 4) && (timer2Ticks < cpuLoopTicks))
	{
		cpuLoopTicks = timer2Ticks;
	}
	if (timer3On && !(TM3CNT & 4) && (timer3Ticks < cpuLoopTicks))
	{
		cpuLoopTicks = timer3Ticks;
	}

	if (IRQTicks)
	{
		if (IRQTicks < cpuLoopTicks)
			cpuLoopTicks = IRQTicks;
	}

	return cpuLoopTicks;
}

void CPUWriteState(gzFile gzFile)
{
	utilWriteInt(gzFile, SAVE_GAME_VERSION);

	u8 romname[17];
	cartridge_get_game_name(romname);
	utilGzWrite(gzFile, romname, 16);

	utilGzWrite(gzFile, &CPU::reg[0], sizeof(CPU::reg));

	utilWriteData(gzFile, saveGameStruct);

	utilGzWrite(gzFile, internalRAM, 0x8000);
	utilGzWrite(gzFile, paletteRAM, 0x400);
	utilGzWrite(gzFile, workRAM, 0x40000);
	utilGzWrite(gzFile, vram, 0x20000);
	utilGzWrite(gzFile, oam, 0x400);
	display_save_state(gzFile);
	utilGzWrite(gzFile, ioMem, 0x400);

	soundSaveGame(gzFile);
	cartridge_rtc_save_state(gzFile);
}

gboolean CPUReadState(gzFile gzFile, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	int version = utilReadInt(gzFile);

	if (version > SAVE_GAME_VERSION || version < SAVE_GAME_VERSION_11)
	{
		g_set_error(err, SAVESTATE_ERROR, G_SAVESTATE_UNSUPPORTED_VERSION,
				"Unsupported VisualBoyAdvance save game version %d", version);
		return FALSE;
	}

	u8 savename[17];
	u8 romname[17];

	utilGzRead(gzFile, savename, 16);
	cartridge_get_game_name(romname);

	if (memcmp(romname, savename, 16) != 0)
	{
		savename[16] = 0;
		for (int i = 0; i < 16; i++)
			if (savename[i] < 32)
				savename[i] = 32;

		g_set_error(err, SAVESTATE_ERROR, G_SAVESTATE_WRONG_GAME,
				"Cannot load save game for %s", savename);
		return FALSE;
	}

	utilGzRead(gzFile, &CPU::reg[0], sizeof(CPU::reg));

	utilReadData(gzFile, saveGameStruct);

	if (IRQTicks > 0)
		intState = true;
	else
	{
		intState = false;
		IRQTicks = 0;
	}

	utilGzRead(gzFile, internalRAM, 0x8000);
	utilGzRead(gzFile, paletteRAM, 0x400);
	utilGzRead(gzFile, workRAM, 0x40000);
	utilGzRead(gzFile, vram, 0x20000);
	utilGzRead(gzFile, oam, 0x400);
	display_read_state(gzFile);
	utilGzRead(gzFile, ioMem, 0x400);

	soundReadGame(gzFile, version);

	cartridge_rtc_load_state(gzFile);

	// set pointers!
	layerEnable = DISPCNT;

	gfx_renderer_choose();
	gfx_buffers_clear(true);
	gfx_window0_update();
	gfx_window1_update();

	if (CPU::armState)
	{
		CPU::ARM_PREFETCH();
	}
	else
	{
		CPU::THUMB_PREFETCH();
	}

	CPUUpdateRegister(0x204, ioMem[0x204]);

	return TRUE;
}

void CPUCleanUp()
{
	cartridge_free();

	MMU::uninit();
}

gboolean CPUInitMemory(GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	if (!MMU::init()) {
		g_set_error(err, LOADER_ERROR, G_LOADER_ERROR_FAILED,
				"Failed to allocate memory for %s", "MMU");
		CPUCleanUp();
		return FALSE;
	}

	if (!cartridge_init()) {
		g_set_error(err, LOADER_ERROR, G_LOADER_ERROR_FAILED,
				"Failed to allocate memory for %s", "ROM");
		CPUCleanUp();
		return FALSE;
	}

	gfx_buffers_clear(TRUE);

	return TRUE;
}

static void CPUCompareVCOUNT()
{
	if (VCOUNT == (DISPSTAT >> 8))
	{
		DISPSTAT |= 4;
		UPDATE_REG(0x04, DISPSTAT);

		if (DISPSTAT & 0x20)
		{
			IF |= 4;
			UPDATE_REG(0x202, IF);
		}
	}
	else
	{
		DISPSTAT &= 0xFFFB;
		UPDATE_REG(0x4, DISPSTAT);
	}
	if (layerEnableDelay>0)
	{
		layerEnableDelay--;
		if (layerEnableDelay==1)
			layerEnable = DISPCNT;
	}

}

static void doDMA(u32 &s, u32 &d, u32 si, u32 di, u32 c, int transfer32)
{
	int sm = s >> 24;
	int dm = d >> 24;
	int sw = 0;
	int dw = 0;
	int sc = c;
	u32 cpuDmaLast;

	// This is done to get the correct waitstates.
	if (sm>15)
		sm=15;
	if (dm>15)
		dm=15;

	//if ((sm>=0x05) && (sm<=0x07) || (dm>=0x05) && (dm <=0x07))
	//    blank = (((DISPSTAT | ((DISPSTAT>>1)&1))==1) ?  true : false);

	if (transfer32)
	{
		s &= 0xFFFFFFFC;
		if (s < 0x02000000 && (CPU::reg[15].I >> 24))
		{
			while (c != 0)
			{
				MMU::write32(d, 0);
				d += di;
				c--;
			}
		}
		else
		{
			while (c != 0)
			{
				cpuDmaLast = MMU::read32(s);
				MMU::write32(d, cpuDmaLast);
				d += di;
				s += si;
				c--;
			}
		}
	}
	else
	{
		s &= 0xFFFFFFFE;
		si = (int)si >> 1;
		di = (int)di >> 1;
		if (s < 0x02000000 && (CPU::reg[15].I >> 24))
		{
			while (c != 0)
			{
				MMU::write16(d, 0);
				d += di;
				c--;
			}
		}
		else
		{
			while (c != 0)
			{
				cpuDmaLast = MMU::read16(s);
				MMU::write16(d, cpuDmaLast);
				cpuDmaLast |= (cpuDmaLast<<16);
				d += di;
				s += si;
				c--;
			}
		}
	}

	int totalTicks = 0;

	if (transfer32)
	{
		sw =1+memoryWaitSeq32[sm & 15];
		dw =1+memoryWaitSeq32[dm & 15];
		totalTicks = (sw+dw)*(sc-1) + 6 + memoryWait32[sm & 15] +
		             memoryWaitSeq32[dm & 15];
	}
	else
	{
		sw = 1+memoryWaitSeq[sm & 15];
		dw = 1+memoryWaitSeq[dm & 15];
		totalTicks = (sw+dw)*(sc-1) + 6 + memoryWait[sm & 15] +
		             memoryWaitSeq[dm & 15];
	}

	cpuDmaTicksToUpdate += totalTicks;

}

void CPUCheckDMA(int reason, int dmamask)
{
	// DMA 0
	if ((DM0CNT_H & 0x8000) && (dmamask & 1))
	{
		if (((DM0CNT_H >> 12) & 3) == reason)
		{
			u32 sourceIncrement = 4;
			u32 destIncrement = 4;
			switch ((DM0CNT_H >> 7) & 3)
			{
			case 0:
				break;
			case 1:
				sourceIncrement = (u32)-4;
				break;
			case 2:
				sourceIncrement = 0;
				break;
			}
			switch ((DM0CNT_H >> 5) & 3)
			{
			case 0:
				break;
			case 1:
				destIncrement = (u32)-4;
				break;
			case 2:
				destIncrement = 0;
				break;
			}
#ifdef GBA_LOGGING
			if (settings_log_channel_enabled(LOG_DMA0))
			{
				int count = (DM0CNT_L ? DM0CNT_L : 0x4000) << 1;
				if (DM0CNT_H & 0x0400)
					count <<= 1;
				g_message("DMA0: s=%08x d=%08x c=%04x count=%08x\n", dma0Source, dma0Dest,
				    DM0CNT_H,
				    count);
			}
#endif
			doDMA(dma0Source, dma0Dest, sourceIncrement, destIncrement,
			      DM0CNT_L ? DM0CNT_L : 0x4000,
			      DM0CNT_H & 0x0400);

			if (DM0CNT_H & 0x4000)
			{
				IF |= 0x0100;
				UPDATE_REG(0x202, IF);
				cpuNextEvent = cpuTotalTicks;
			}

			if (((DM0CNT_H >> 5) & 3) == 3)
			{
				dma0Dest = DM0DAD_L | (DM0DAD_H << 16);
			}

			if (!(DM0CNT_H & 0x0200) || (reason == 0))
			{
				DM0CNT_H &= 0x7FFF;
				UPDATE_REG(0xBA, DM0CNT_H);
			}
		}
	}

	// DMA 1
	if ((DM1CNT_H & 0x8000) && (dmamask & 2))
	{
		if (((DM1CNT_H >> 12) & 3) == reason)
		{
			u32 sourceIncrement = 4;
			u32 destIncrement = 4;
			switch ((DM1CNT_H >> 7) & 3)
			{
			case 0:
				break;
			case 1:
				sourceIncrement = (u32)-4;
				break;
			case 2:
				sourceIncrement = 0;
				break;
			}
			switch ((DM1CNT_H >> 5) & 3)
			{
			case 0:
				break;
			case 1:
				destIncrement = (u32)-4;
				break;
			case 2:
				destIncrement = 0;
				break;
			}
			if (reason == 3)
			{
#ifdef GBA_LOGGING
				if (settings_log_channel_enabled(LOG_DMA1))
				{
					g_message("DMA1: s=%08x d=%08x c=%04x count=%08x\n", dma1Source, dma1Dest,
					    DM1CNT_H,
					    16);
				}
#endif
				doDMA(dma1Source, dma1Dest, sourceIncrement, 0, 4,
				      0x0400);
			}
			else
			{
#ifdef GBA_LOGGING
				if (settings_log_channel_enabled(LOG_DMA1))
				{
					int count = (DM1CNT_L ? DM1CNT_L : 0x4000) << 1;
					if (DM1CNT_H & 0x0400)
						count <<= 1;
					g_message("DMA1: s=%08x d=%08x c=%04x count=%08x\n", dma1Source, dma1Dest,
					    DM1CNT_H,
					    count);
				}
#endif
				doDMA(dma1Source, dma1Dest, sourceIncrement, destIncrement,
				      DM1CNT_L ? DM1CNT_L : 0x4000,
				      DM1CNT_H & 0x0400);
			}

			if (DM1CNT_H & 0x4000)
			{
				IF |= 0x0200;
				UPDATE_REG(0x202, IF);
				cpuNextEvent = cpuTotalTicks;
			}

			if (((DM1CNT_H >> 5) & 3) == 3)
			{
				dma1Dest = DM1DAD_L | (DM1DAD_H << 16);
			}

			if (!(DM1CNT_H & 0x0200) || (reason == 0))
			{
				DM1CNT_H &= 0x7FFF;
				UPDATE_REG(0xC6, DM1CNT_H);
			}
		}
	}

	// DMA 2
	if ((DM2CNT_H & 0x8000) && (dmamask & 4))
	{
		if (((DM2CNT_H >> 12) & 3) == reason)
		{
			u32 sourceIncrement = 4;
			u32 destIncrement = 4;
			switch ((DM2CNT_H >> 7) & 3)
			{
			case 0:
				break;
			case 1:
				sourceIncrement = (u32)-4;
				break;
			case 2:
				sourceIncrement = 0;
				break;
			}
			switch ((DM2CNT_H >> 5) & 3)
			{
			case 0:
				break;
			case 1:
				destIncrement = (u32)-4;
				break;
			case 2:
				destIncrement = 0;
				break;
			}
			if (reason == 3)
			{
#ifdef GBA_LOGGING
				if (settings_log_channel_enabled(LOG_DMA2))
				{
					int count = (4) << 2;
					g_message("DMA2: s=%08x d=%08x c=%04x count=%08x\n", dma2Source, dma2Dest,
					    DM2CNT_H,
					    count);
				}
#endif
				doDMA(dma2Source, dma2Dest, sourceIncrement, 0, 4,
				      0x0400);
			}
			else
			{
#ifdef GBA_LOGGING
				if (settings_log_channel_enabled(LOG_DMA2))
				{
					int count = (DM2CNT_L ? DM2CNT_L : 0x4000) << 1;
					if (DM2CNT_H & 0x0400)
						count <<= 1;
					g_message("DMA2: s=%08x d=%08x c=%04x count=%08x\n", dma2Source, dma2Dest,
					    DM2CNT_H,
					    count);
				}
#endif
				doDMA(dma2Source, dma2Dest, sourceIncrement, destIncrement,
				      DM2CNT_L ? DM2CNT_L : 0x4000,
				      DM2CNT_H & 0x0400);
			}

			if (DM2CNT_H & 0x4000)
			{
				IF |= 0x0400;
				UPDATE_REG(0x202, IF);
				cpuNextEvent = cpuTotalTicks;
			}

			if (((DM2CNT_H >> 5) & 3) == 3)
			{
				dma2Dest = DM2DAD_L | (DM2DAD_H << 16);
			}

			if (!(DM2CNT_H & 0x0200) || (reason == 0))
			{
				DM2CNT_H &= 0x7FFF;
				UPDATE_REG(0xD2, DM2CNT_H);
			}
		}
	}

	// DMA 3
	if ((DM3CNT_H & 0x8000) && (dmamask & 8))
	{
		if (((DM3CNT_H >> 12) & 3) == reason)
		{
			u32 sourceIncrement = 4;
			u32 destIncrement = 4;
			switch ((DM3CNT_H >> 7) & 3)
			{
			case 0:
				break;
			case 1:
				sourceIncrement = (u32)-4;
				break;
			case 2:
				sourceIncrement = 0;
				break;
			}
			switch ((DM3CNT_H >> 5) & 3)
			{
			case 0:
				break;
			case 1:
				destIncrement = (u32)-4;
				break;
			case 2:
				destIncrement = 0;
				break;
			}
#ifdef GBA_LOGGING
			if (settings_log_channel_enabled(LOG_DMA3))
			{
				int count = (DM3CNT_L ? DM3CNT_L : 0x10000) << 1;
				if (DM3CNT_H & 0x0400)
					count <<= 1;
				g_message("DMA3: s=%08x d=%08x c=%04x count=%08x\n", dma3Source, dma3Dest,
				    DM3CNT_H,
				    count);
			}
#endif
			doDMA(dma3Source, dma3Dest, sourceIncrement, destIncrement,
			      DM3CNT_L ? DM3CNT_L : 0x10000,
			      DM3CNT_H & 0x0400);
			if (DM3CNT_H & 0x4000)
			{
				IF |= 0x0800;
				UPDATE_REG(0x202, IF);
				cpuNextEvent = cpuTotalTicks;
			}

			if (((DM3CNT_H >> 5) & 3) == 3)
			{
				dma3Dest = DM3DAD_L | (DM3DAD_H << 16);
			}

			if (!(DM3CNT_H & 0x0200) || (reason == 0))
			{
				DM3CNT_H &= 0x7FFF;
				UPDATE_REG(0xDE, DM3CNT_H);
			}
		}
	}
}

void CPUUpdateRegister(u32 address, u16 value)
{
	switch (address)
	{
	case 0x00:
	{ // we need to place the following code in { } because we declare & initialize variables in a case statement
		if ((value & 7) > 5)
		{
			// display modes above 0-5 are prohibited
			DISPCNT = (value & 7);
		}
		bool change = (0 != ((DISPCNT ^ value) & 0x80));
		bool changeBG = (0 != ((DISPCNT ^ value) & 0x0F00));
		u16 changeBGon = ((~DISPCNT) & value) & 0x0F00; // these layers are being activated

		DISPCNT = (value & 0xFFF7); // bit 3 can only be accessed by the BIOS to enable GBC mode
		UPDATE_REG(0x00, DISPCNT);

		if (changeBGon)
		{
			layerEnableDelay = 4;
			layerEnable = value & (~changeBGon);
		}
		else
		{
			layerEnable = value;
			// CPUUpdateTicks();
		}

		if (change && !((value & 0x80)))
		{
			if (!(DISPSTAT & 1))
			{
				lcdTicks = 1008;
				//      VCOUNT = 0;
				//      UPDATE_REG(0x06, VCOUNT);
				DISPSTAT &= 0xFFFC;
				UPDATE_REG(0x04, DISPSTAT);
				CPUCompareVCOUNT();
			}
		}
		gfx_renderer_choose();
		// we only care about changes in BG0-BG3
		if (changeBG)
		{
			gfx_buffers_clear(false);
		}
		break;
	}
	case 0x04:
		DISPSTAT = (value & 0xFF38) | (DISPSTAT & 7);
		UPDATE_REG(0x04, DISPSTAT);
		break;
	case 0x06:
		// not writable
		break;
	case 0x08:
		BG0CNT = (value & 0xDFCF);
		UPDATE_REG(0x08, BG0CNT);
		break;
	case 0x0A:
		BG1CNT = (value & 0xDFCF);
		UPDATE_REG(0x0A, BG1CNT);
		break;
	case 0x0C:
		BG2CNT = (value & 0xFFCF);
		UPDATE_REG(0x0C, BG2CNT);
		break;
	case 0x0E:
		BG3CNT = (value & 0xFFCF);
		UPDATE_REG(0x0E, BG3CNT);
		break;
	case 0x10:
		BG0HOFS = value & 511;
		UPDATE_REG(0x10, BG0HOFS);
		break;
	case 0x12:
		BG0VOFS = value & 511;
		UPDATE_REG(0x12, BG0VOFS);
		break;
	case 0x14:
		BG1HOFS = value & 511;
		UPDATE_REG(0x14, BG1HOFS);
		break;
	case 0x16:
		BG1VOFS = value & 511;
		UPDATE_REG(0x16, BG1VOFS);
		break;
	case 0x18:
		BG2HOFS = value & 511;
		UPDATE_REG(0x18, BG2HOFS);
		break;
	case 0x1A:
		BG2VOFS = value & 511;
		UPDATE_REG(0x1A, BG2VOFS);
		break;
	case 0x1C:
		BG3HOFS = value & 511;
		UPDATE_REG(0x1C, BG3HOFS);
		break;
	case 0x1E:
		BG3VOFS = value & 511;
		UPDATE_REG(0x1E, BG3VOFS);
		break;
	case 0x20:
		BG2PA = value;
		UPDATE_REG(0x20, BG2PA);
		break;
	case 0x22:
		BG2PB = value;
		UPDATE_REG(0x22, BG2PB);
		break;
	case 0x24:
		BG2PC = value;
		UPDATE_REG(0x24, BG2PC);
		break;
	case 0x26:
		BG2PD = value;
		UPDATE_REG(0x26, BG2PD);
		break;
	case 0x28:
		BG2X_L = value;
		UPDATE_REG(0x28, BG2X_L);
		gfx_BG2X_update();
		break;
	case 0x2A:
		BG2X_H = (value & 0xFFF);
		UPDATE_REG(0x2A, BG2X_H);
		gfx_BG2X_update();
		break;
	case 0x2C:
		BG2Y_L = value;
		UPDATE_REG(0x2C, BG2Y_L);
		gfx_BG2Y_update();
		break;
	case 0x2E:
		BG2Y_H = value & 0xFFF;
		UPDATE_REG(0x2E, BG2Y_H);
		gfx_BG2Y_update();
		break;
	case 0x30:
		BG3PA = value;
		UPDATE_REG(0x30, BG3PA);
		break;
	case 0x32:
		BG3PB = value;
		UPDATE_REG(0x32, BG3PB);
		break;
	case 0x34:
		BG3PC = value;
		UPDATE_REG(0x34, BG3PC);
		break;
	case 0x36:
		BG3PD = value;
		UPDATE_REG(0x36, BG3PD);
		break;
	case 0x38:
		BG3X_L = value;
		UPDATE_REG(0x38, BG3X_L);
		gfx_BG3X_update();
		break;
	case 0x3A:
		BG3X_H = value & 0xFFF;
		UPDATE_REG(0x3A, BG3X_H);
		gfx_BG3X_update();
		break;
	case 0x3C:
		BG3Y_L = value;
		UPDATE_REG(0x3C, BG3Y_L);
		gfx_BG3Y_update();
		break;
	case 0x3E:
		BG3Y_H = value & 0xFFF;
		UPDATE_REG(0x3E, BG3Y_H);
		gfx_BG3Y_update();
		break;
	case 0x40:
		WIN0H = value;
		UPDATE_REG(0x40, WIN0H);
		gfx_window0_update();
		break;
	case 0x42:
		WIN1H = value;
		UPDATE_REG(0x42, WIN1H);
		gfx_window1_update();
		break;
	case 0x44:
		WIN0V = value;
		UPDATE_REG(0x44, WIN0V);
		break;
	case 0x46:
		WIN1V = value;
		UPDATE_REG(0x46, WIN1V);
		break;
	case 0x48:
		WININ = value & 0x3F3F;
		UPDATE_REG(0x48, WININ);
		break;
	case 0x4A:
		WINOUT = value & 0x3F3F;
		UPDATE_REG(0x4A, WINOUT);
		break;
	case 0x4C:
		MOSAIC = value;
		UPDATE_REG(0x4C, MOSAIC);
		break;
	case 0x50:
		BLDMOD = value & 0x3FFF;
		UPDATE_REG(0x50, BLDMOD);
		gfx_renderer_choose();
		break;
	case 0x52:
		COLEV = value & 0x1F1F;
		UPDATE_REG(0x52, COLEV);
		break;
	case 0x54:
		COLY = value & 0x1F;
		UPDATE_REG(0x54, COLY);
		break;
	case 0x60:
	case 0x62:
	case 0x64:
	case 0x68:
	case 0x6c:
	case 0x70:
	case 0x72:
	case 0x74:
	case 0x78:
	case 0x7c:
	case 0x80:
	case 0x84:
		soundEvent(address&0xFF, (u8)(value & 0xFF));
		soundEvent((address&0xFF)+1, (u8)(value>>8));
		break;
	case 0x82:
	case 0x88:
	case 0xa0:
	case 0xa2:
	case 0xa4:
	case 0xa6:
	case 0x90:
	case 0x92:
	case 0x94:
	case 0x96:
	case 0x98:
	case 0x9a:
	case 0x9c:
	case 0x9e:
		soundEvent(address&0xFF, value);
		break;
	case 0xB0:
		DM0SAD_L = value;
		UPDATE_REG(0xB0, DM0SAD_L);
		break;
	case 0xB2:
		DM0SAD_H = value & 0x07FF;
		UPDATE_REG(0xB2, DM0SAD_H);
		break;
	case 0xB4:
		DM0DAD_L = value;
		UPDATE_REG(0xB4, DM0DAD_L);
		break;
	case 0xB6:
		DM0DAD_H = value & 0x07FF;
		UPDATE_REG(0xB6, DM0DAD_H);
		break;
	case 0xB8:
		DM0CNT_L = value & 0x3FFF;
		UPDATE_REG(0xB8, 0);
		break;
	case 0xBA:
	{
		bool start = ((DM0CNT_H ^ value) & 0x8000) ? true : false;
		value &= 0xF7E0;

		DM0CNT_H = value;
		UPDATE_REG(0xBA, DM0CNT_H);

		if (start && (value & 0x8000))
		{
			dma0Source = DM0SAD_L | (DM0SAD_H << 16);
			dma0Dest = DM0DAD_L | (DM0DAD_H << 16);
			CPUCheckDMA(0, 1);
		}
	}
	break;
	case 0xBC:
		DM1SAD_L = value;
		UPDATE_REG(0xBC, DM1SAD_L);
		break;
	case 0xBE:
		DM1SAD_H = value & 0x0FFF;
		UPDATE_REG(0xBE, DM1SAD_H);
		break;
	case 0xC0:
		DM1DAD_L = value;
		UPDATE_REG(0xC0, DM1DAD_L);
		break;
	case 0xC2:
		DM1DAD_H = value & 0x07FF;
		UPDATE_REG(0xC2, DM1DAD_H);
		break;
	case 0xC4:
		DM1CNT_L = value & 0x3FFF;
		UPDATE_REG(0xC4, 0);
		break;
	case 0xC6:
	{
		bool start = ((DM1CNT_H ^ value) & 0x8000) ? true : false;
		value &= 0xF7E0;

		DM1CNT_H = value;
		UPDATE_REG(0xC6, DM1CNT_H);

		if (start && (value & 0x8000))
		{
			dma1Source = DM1SAD_L | (DM1SAD_H << 16);
			dma1Dest = DM1DAD_L | (DM1DAD_H << 16);
			CPUCheckDMA(0, 2);
		}
	}
	break;
	case 0xC8:
		DM2SAD_L = value;
		UPDATE_REG(0xC8, DM2SAD_L);
		break;
	case 0xCA:
		DM2SAD_H = value & 0x0FFF;
		UPDATE_REG(0xCA, DM2SAD_H);
		break;
	case 0xCC:
		DM2DAD_L = value;
		UPDATE_REG(0xCC, DM2DAD_L);
		break;
	case 0xCE:
		DM2DAD_H = value & 0x07FF;
		UPDATE_REG(0xCE, DM2DAD_H);
		break;
	case 0xD0:
		DM2CNT_L = value & 0x3FFF;
		UPDATE_REG(0xD0, 0);
		break;
	case 0xD2:
	{
		bool start = ((DM2CNT_H ^ value) & 0x8000) ? true : false;

		value &= 0xF7E0;

		DM2CNT_H = value;
		UPDATE_REG(0xD2, DM2CNT_H);

		if (start && (value & 0x8000))
		{
			dma2Source = DM2SAD_L | (DM2SAD_H << 16);
			dma2Dest = DM2DAD_L | (DM2DAD_H << 16);

			CPUCheckDMA(0, 4);
		}
	}
	break;
	case 0xD4:
		DM3SAD_L = value;
		UPDATE_REG(0xD4, DM3SAD_L);
		break;
	case 0xD6:
		DM3SAD_H = value & 0x0FFF;
		UPDATE_REG(0xD6, DM3SAD_H);
		break;
	case 0xD8:
		DM3DAD_L = value;
		UPDATE_REG(0xD8, DM3DAD_L);
		break;
	case 0xDA:
		DM3DAD_H = value & 0x0FFF;
		UPDATE_REG(0xDA, DM3DAD_H);
		break;
	case 0xDC:
		DM3CNT_L = value;
		UPDATE_REG(0xDC, 0);
		break;
	case 0xDE:
	{
		bool start = ((DM3CNT_H ^ value) & 0x8000) ? true : false;

		value &= 0xFFE0;

		DM3CNT_H = value;
		UPDATE_REG(0xDE, DM3CNT_H);

		if (start && (value & 0x8000))
		{
			dma3Source = DM3SAD_L | (DM3SAD_H << 16);
			dma3Dest = DM3DAD_L | (DM3DAD_H << 16);
			CPUCheckDMA(0,8);
		}
	}
	break;
	case 0x100:
		timer0Reload = value;
		interp_rate();
		break;
	case 0x102:
		timer0Value = value;
		timerOnOffDelay|=1;
		cpuNextEvent = cpuTotalTicks;
		break;
	case 0x104:
		timer1Reload = value;
		interp_rate();
		break;
	case 0x106:
		timer1Value = value;
		timerOnOffDelay|=2;
		cpuNextEvent = cpuTotalTicks;
		break;
	case 0x108:
		timer2Reload = value;
		break;
	case 0x10A:
		timer2Value = value;
		timerOnOffDelay|=4;
		cpuNextEvent = cpuTotalTicks;
		break;
	case 0x10C:
		timer3Reload = value;
		break;
	case 0x10E:
		timer3Value = value;
		timerOnOffDelay|=8;
		cpuNextEvent = cpuTotalTicks;
		break;
	case 0x128: // REG_SIOCNT
#ifdef LINK_EMULATION
		if (linkenable)
		{
			linkUpdateSIOCNT(value);
		}
		else
#endif
		{
			if (value & 0x80)
			{
				value &= 0xff7f;
				if (value & 1 && (value & 0x4000))
				{
					UPDATE_REG(0x12a, 0xFF);
					IF |= 0x80;
					UPDATE_REG(0x202, IF);
					value &= 0x7f7f;
				}
			}
			UPDATE_REG(0x128, value);
		}
		break;
	case 0x12a: // REG_SIODATA8 or REG_SIOMLT_SEND
#ifdef LINK_EMULATION
		if (linkenable)
			LinkSSend(value);
#endif
		{
			UPDATE_REG(0x134, value);
		}
		break;
	case 0x130:
		P1 |= (value & 0x3FF);
		UPDATE_REG(0x130, P1);
		break;
	case 0x132:
		UPDATE_REG(0x132, value & 0xC3FF);
		break;
	case 0x134: // REG_RCNT
#ifdef LINK_EMULATION
		if (linkenable)
			linkUpdateRCNT(value);
		else
#endif
			UPDATE_REG(0x134, value);

		break;
	case 0x140: // REG_HS_CTRL
#ifdef LINK_EMULATION
		if (linkenable)
			StartJOYLink(value);
		else
#endif
			UPDATE_REG(0x140, value);

		break;
	case 0x200:
		IE = value & 0x3FFF;
		UPDATE_REG(0x200, IE);
		if ((IME & 1) && (IF & IE) && CPU::armIrqEnable)
			cpuNextEvent = cpuTotalTicks;
		break;
	case 0x202:
		IF ^= (value & IF);
		UPDATE_REG(0x202, IF);
		break;
	case 0x204:
	{
		memoryWait[0x0e] = memoryWaitSeq[0x0e] = gamepakRamWaitState[value & 3];
		memoryWait[0x08] = memoryWait[0x09] = gamepakWaitState[(value >> 2) & 3];
		memoryWaitSeq[0x08] = memoryWaitSeq[0x09] = gamepakWaitState0[(value >> 4) & 1];

		memoryWait[0x0a] = memoryWait[0x0b] = gamepakWaitState[(value >> 5) & 3];
		memoryWaitSeq[0x0a] = memoryWaitSeq[0x0b] = gamepakWaitState1[(value >> 7) & 1];

		memoryWait[0x0c] = memoryWait[0x0d] = gamepakWaitState[(value >> 8) & 3];
		memoryWaitSeq[0x0c] = memoryWaitSeq[0x0d] = gamepakWaitState2[(value >> 10) & 1];

		for (int i = 8; i < 15; i++)
		{
			memoryWait32[i] = memoryWait[i] + memoryWaitSeq[i] + 1;
			memoryWaitSeq32[i] = memoryWaitSeq[i]*2 + 1;
		}

		CPU::enableBusPrefetch((value & 0x4000) == 0x4000);

		UPDATE_REG(0x204, value & 0x7FFF);

	}
	break;
	case 0x208:
		IME = value & 1;
		UPDATE_REG(0x208, IME);
		if ((IME & 1) && (IF & IE) && CPU::armIrqEnable)
			cpuNextEvent = cpuTotalTicks;
		break;
	case 0x300:
		if (value != 0)
			value &= 0xFFFE;
		UPDATE_REG(0x300, value);
		break;
	default:
		UPDATE_REG(address&0x3FE, value);
		break;
	}
}

static void applyTimer ()
{
	if (timerOnOffDelay & 1)
	{
		timer0ClockReload = TIMER_TICKS[timer0Value & 3];
		if (!timer0On && (timer0Value & 0x80))
		{
			// reload the counter
			TM0D = timer0Reload;
			timer0Ticks = (0x10000 - TM0D) << timer0ClockReload;
			UPDATE_REG(0x100, TM0D);
		}
		timer0On = timer0Value & 0x80 ? true : false;
		TM0CNT = timer0Value & 0xC7;
		interp_rate();
		UPDATE_REG(0x102, TM0CNT);
		//    CPUUpdateTicks();
	}
	if (timerOnOffDelay & 2)
	{
		timer1ClockReload = TIMER_TICKS[timer1Value & 3];
		if (!timer1On && (timer1Value & 0x80))
		{
			// reload the counter
			TM1D = timer1Reload;
			timer1Ticks = (0x10000 - TM1D) << timer1ClockReload;
			UPDATE_REG(0x104, TM1D);
		}
		timer1On = timer1Value & 0x80 ? true : false;
		TM1CNT = timer1Value & 0xC7;
		interp_rate();
		UPDATE_REG(0x106, TM1CNT);
	}
	if (timerOnOffDelay & 4)
	{
		timer2ClockReload = TIMER_TICKS[timer2Value & 3];
		if (!timer2On && (timer2Value & 0x80))
		{
			// reload the counter
			TM2D = timer2Reload;
			timer2Ticks = (0x10000 - TM2D) << timer2ClockReload;
			UPDATE_REG(0x108, TM2D);
		}
		timer2On = timer2Value & 0x80 ? true : false;
		TM2CNT = timer2Value & 0xC7;
		UPDATE_REG(0x10A, TM2CNT);
	}
	if (timerOnOffDelay & 8)
	{
		timer3ClockReload = TIMER_TICKS[timer3Value & 3];
		if (!timer3On && (timer3Value & 0x80))
		{
			// reload the counter
			TM3D = timer3Reload;
			timer3Ticks = (0x10000 - TM3D) << timer3ClockReload;
			UPDATE_REG(0x10C, TM3D);
		}
		timer3On = timer3Value & 0x80 ? true : false;
		TM3CNT = timer3Value & 0xC7;
		UPDATE_REG(0x10E, TM3CNT);
	}
	cpuNextEvent = CPUUpdateTicks();
	timerOnOffDelay = 0;
}

gboolean CPULoadBios(const gchar *biosFileName, GError **err)
{
	int size = 0x4000;
	RomLoader *loader = loader_new(ROM_BIOS, biosFileName);
	if (!loader_load(loader, bios, &size, err)) {
		loader_free(loader);
		return FALSE;
	}
	loader_free(loader);
	return TRUE;
}

void CPUInit()
{
	biosProtected[0] = 0x00;
	biosProtected[1] = 0xf0;
	biosProtected[2] = 0x29;
	biosProtected[3] = 0xe1;

	CPU::init();
}

void CPUReset()
{
	// clean OAM
	memset(oam, 0, 0x400);
	// clean palette
	memset(paletteRAM, 0, 0x400);
	// clean picture
	display_clear();
	// clean vram
	memset(vram, 0, 0x20000);
	// clean io memory
	memset(ioMem, 0, 0x400);

	DISPCNT  = 0x0080;
	DISPSTAT = 0x0000;
	VCOUNT   = 0x0000;
	BG0CNT   = 0x0000;
	BG1CNT   = 0x0000;
	BG2CNT   = 0x0000;
	BG3CNT   = 0x0000;
	BG0HOFS  = 0x0000;
	BG0VOFS  = 0x0000;
	BG1HOFS  = 0x0000;
	BG1VOFS  = 0x0000;
	BG2HOFS  = 0x0000;
	BG2VOFS  = 0x0000;
	BG3HOFS  = 0x0000;
	BG3VOFS  = 0x0000;
	BG2PA    = 0x0100;
	BG2PB    = 0x0000;
	BG2PC    = 0x0000;
	BG2PD    = 0x0100;
	BG2X_L   = 0x0000;
	BG2X_H   = 0x0000;
	BG2Y_L   = 0x0000;
	BG2Y_H   = 0x0000;
	BG3PA    = 0x0100;
	BG3PB    = 0x0000;
	BG3PC    = 0x0000;
	BG3PD    = 0x0100;
	BG3X_L   = 0x0000;
	BG3X_H   = 0x0000;
	BG3Y_L   = 0x0000;
	BG3Y_H   = 0x0000;
	WIN0H    = 0x0000;
	WIN1H    = 0x0000;
	WIN0V    = 0x0000;
	WIN1V    = 0x0000;
	WININ    = 0x0000;
	WINOUT   = 0x0000;
	MOSAIC   = 0x0000;
	BLDMOD   = 0x0000;
	COLEV    = 0x0000;
	COLY     = 0x0000;
	DM0SAD_L = 0x0000;
	DM0SAD_H = 0x0000;
	DM0DAD_L = 0x0000;
	DM0DAD_H = 0x0000;
	DM0CNT_L = 0x0000;
	DM0CNT_H = 0x0000;
	DM1SAD_L = 0x0000;
	DM1SAD_H = 0x0000;
	DM1DAD_L = 0x0000;
	DM1DAD_H = 0x0000;
	DM1CNT_L = 0x0000;
	DM1CNT_H = 0x0000;
	DM2SAD_L = 0x0000;
	DM2SAD_H = 0x0000;
	DM2DAD_L = 0x0000;
	DM2DAD_H = 0x0000;
	DM2CNT_L = 0x0000;
	DM2CNT_H = 0x0000;
	DM3SAD_L = 0x0000;
	DM3SAD_H = 0x0000;
	DM3DAD_L = 0x0000;
	DM3DAD_H = 0x0000;
	DM3CNT_L = 0x0000;
	DM3CNT_H = 0x0000;
	TM0D     = 0x0000;
	TM0CNT   = 0x0000;
	TM1D     = 0x0000;
	TM1CNT   = 0x0000;
	TM2D     = 0x0000;
	TM2CNT   = 0x0000;
	TM3D     = 0x0000;
	TM3CNT   = 0x0000;
	P1       = 0x03FF;
	IE       = 0x0000;
	IF       = 0x0000;
	IME      = 0x0000;

	UPDATE_REG(0x00, DISPCNT);
	UPDATE_REG(0x06, VCOUNT);
	UPDATE_REG(0x20, BG2PA);
	UPDATE_REG(0x26, BG2PD);
	UPDATE_REG(0x30, BG3PA);
	UPDATE_REG(0x36, BG3PD);
	UPDATE_REG(0x130, P1);
	UPDATE_REG(0x88, 0x200);

	// reset internal state
	holdState = false;

	biosProtected[0] = 0x00;
	biosProtected[1] = 0xf0;
	biosProtected[2] = 0x29;
	biosProtected[3] = 0xe1;

	lcdTicks = 1008;
	timer0On = false;
	timer0Ticks = 0;
	timer0Reload = 0;
	timer0ClockReload  = 0;
	timer1On = false;
	timer1Ticks = 0;
	timer1Reload = 0;
	timer1ClockReload  = 0;
	timer2On = false;
	timer2Ticks = 0;
	timer2Reload = 0;
	timer2ClockReload  = 0;
	timer3On = false;
	timer3Ticks = 0;
	timer3Reload = 0;
	timer3ClockReload  = 0;
	dma0Source = 0;
	dma0Dest = 0;
	dma1Source = 0;
	dma1Dest = 0;
	dma2Source = 0;
	dma2Dest = 0;
	dma3Source = 0;
	dma3Dest = 0;
	gfx_renderer_choose();
	layerEnable = DISPCNT;

	gfx_buffers_clear(true);

	cartridge_reset();

	gfx_window0_update();
	gfx_window1_update();

	soundReset();

	CPU::reset();

	lastTime = g_get_monotonic_time();

}

void CPULoop(int ticks)
{
	int clockTicks;
	int timerOverflow = 0;
	// variable used by the CPU core
	cpuTotalTicks = 0;
#ifdef LINK_EMULATION
	if (linkenable)
		cpuNextEvent = 1;
#endif
	cpuBreakLoop = false;
	cpuNextEvent = CPUUpdateTicks();
	if (cpuNextEvent > ticks)
		cpuNextEvent = ticks;


	for (;;)
	{
		if (!holdState)
		{
			if (CPU::armState)
			{
				if (!CPU::armExecute())
					return;
			}
			else
			{
				if (!CPU::thumbExecute())
					return;
			}
			clockTicks = 0;
		}
		else
			clockTicks = CPUUpdateTicks();

		cpuTotalTicks += clockTicks;


		if (cpuTotalTicks >= cpuNextEvent)
		{
			int remainingTicks = cpuTotalTicks - cpuNextEvent;

			clockTicks = cpuNextEvent;
			cpuTotalTicks = 0;

updateLoop:

			if (IRQTicks)
			{
				IRQTicks -= clockTicks;
				if (IRQTicks<0)
					IRQTicks = 0;
			}

			lcdTicks -= clockTicks;


			if (lcdTicks <= 0)
			{
				if (DISPSTAT & 1)  // V-BLANK
				{
					// if in V-Blank mode, keep computing...
					if (DISPSTAT & 2)
					{
						lcdTicks += 1008;
						VCOUNT++;
						UPDATE_REG(0x06, VCOUNT);
						DISPSTAT &= 0xFFFD;
						UPDATE_REG(0x04, DISPSTAT);
						CPUCompareVCOUNT();
					}
					else
					{
						lcdTicks += 224;
						DISPSTAT |= 2;
						UPDATE_REG(0x04, DISPSTAT);
						if (DISPSTAT & 16)
						{
							IF |= 2;
							UPDATE_REG(0x202, IF);
						}
					}

					if (VCOUNT >= 228)  //Reaching last line
					{
						DISPSTAT &= 0xFFFC;
						UPDATE_REG(0x04, DISPSTAT);
						VCOUNT = 0;
						UPDATE_REG(0x06, VCOUNT);
						CPUCompareVCOUNT();
						gfx_frame_new();
					}
				}
				else
				{
					if (DISPSTAT & 2)
					{
						// if in H-Blank, leave it and move to drawing mode
						VCOUNT++;
						UPDATE_REG(0x06, VCOUNT);

						lcdTicks += 1008;
						DISPSTAT &= 0xFFFD;
						if (VCOUNT == 160)
						{
							count++;

							if (count == 60)
							{
								gint64 time = g_get_monotonic_time();
								if (time != lastTime) {
									speed = 100000000/(time - lastTime);
								} else {
									speed = 0;
								}
								lastTime = time;
								count = 0;
							}
							u32 joy = inputDriver->read_joypad(inputDriver);
							P1 = 0x03FF ^ (joy & 0x3FF);
							
							//FIXME: Reenable
							/*if (features.hasMotionSensor)*/
							inputDriver->update_motion_sensor(inputDriver);

							UPDATE_REG(0x130, P1);
							u16 P1CNT = READ16LE(((u16 *)&ioMem[0x132]));
							// this seems wrong, but there are cases where the game
							// can enter the stop state without requesting an IRQ from
							// the joypad.
							if ((P1CNT & 0x4000) || stopState)
							{
								u16 p1 = (0x3FF ^ P1) & 0x3FF;
								if (P1CNT & 0x8000)
								{
									if (p1 == (P1CNT & 0x3FF))
									{
										IF |= 0x1000;
										UPDATE_REG(0x202, IF);
									}
								}
								else
								{
									if (p1 & P1CNT)
									{
										IF |= 0x1000;
										UPDATE_REG(0x202, IF);
									}
								}
							}

							DISPSTAT |= 1;
							DISPSTAT &= 0xFFFD;
							UPDATE_REG(0x04, DISPSTAT);
							if (DISPSTAT & 0x0008)
							{
								IF |= 1;
								UPDATE_REG(0x202, IF);
							}
							CPUCheckDMA(1, 0x0f);
							display_draw_screen();
						}

						UPDATE_REG(0x04, DISPSTAT);
						CPUCompareVCOUNT();

					}
					else
					{
						gfx_line_render();
						display_draw_line(VCOUNT, gfxLineMix);

						// entering H-Blank
						DISPSTAT |= 2;
						UPDATE_REG(0x04, DISPSTAT);
						lcdTicks += 224;
						CPUCheckDMA(2, 0x0f);
						if (DISPSTAT & 16)
						{
							IF |= 2;
							UPDATE_REG(0x202, IF);
						}
					}
				}
			}

			// we shouldn't be doing sound in stop state, but we loose synchronization
			// if sound is disabled, so in stop state, soundTick will just produce
			// mute sound
			soundTicks -= clockTicks;
			if (soundTicks <= 0)
			{
				psoundTickfn();
				soundTicks += SOUND_CLOCK_TICKS;
			}

			if (!stopState)
			{
				if (timer0On)
				{
					timer0Ticks -= clockTicks;
					if (timer0Ticks <= 0)
					{
						timer0Ticks += (0x10000 - timer0Reload) << timer0ClockReload;
						timerOverflow |= 1;
						soundTimerOverflow(0);
						if (TM0CNT & 0x40)
						{
							IF |= 0x08;
							UPDATE_REG(0x202, IF);
						}
					}
					TM0D = 0xFFFF - (timer0Ticks >> timer0ClockReload);
					UPDATE_REG(0x100, TM0D);
				}

				if (timer1On)
				{
					if (TM1CNT & 4)
					{
						if (timerOverflow & 1)
						{
							TM1D++;
							if (TM1D == 0)
							{
								TM1D += timer1Reload;
								timerOverflow |= 2;
								soundTimerOverflow(1);
								if (TM1CNT & 0x40)
								{
									IF |= 0x10;
									UPDATE_REG(0x202, IF);
								}
							}
							UPDATE_REG(0x104, TM1D);
						}
					}
					else
					{
						timer1Ticks -= clockTicks;
						if (timer1Ticks <= 0)
						{
							timer1Ticks += (0x10000 - timer1Reload) << timer1ClockReload;
							timerOverflow |= 2;
							soundTimerOverflow(1);
							if (TM1CNT & 0x40)
							{
								IF |= 0x10;
								UPDATE_REG(0x202, IF);
							}
						}
						TM1D = 0xFFFF - (timer1Ticks >> timer1ClockReload);
						UPDATE_REG(0x104, TM1D);
					}
				}

				if (timer2On)
				{
					if (TM2CNT & 4)
					{
						if (timerOverflow & 2)
						{
							TM2D++;
							if (TM2D == 0)
							{
								TM2D += timer2Reload;
								timerOverflow |= 4;
								if (TM2CNT & 0x40)
								{
									IF |= 0x20;
									UPDATE_REG(0x202, IF);
								}
							}
							UPDATE_REG(0x108, TM2D);
						}
					}
					else
					{
						timer2Ticks -= clockTicks;
						if (timer2Ticks <= 0)
						{
							timer2Ticks += (0x10000 - timer2Reload) << timer2ClockReload;
							timerOverflow |= 4;
							if (TM2CNT & 0x40)
							{
								IF |= 0x20;
								UPDATE_REG(0x202, IF);
							}
						}
						TM2D = 0xFFFF - (timer2Ticks >> timer2ClockReload);
						UPDATE_REG(0x108, TM2D);
					}
				}

				if (timer3On)
				{
					if (TM3CNT & 4)
					{
						if (timerOverflow & 4)
						{
							TM3D++;
							if (TM3D == 0)
							{
								TM3D += timer3Reload;
								if (TM3CNT & 0x40)
								{
									IF |= 0x40;
									UPDATE_REG(0x202, IF);
								}
							}
							UPDATE_REG(0x10C, TM3D);
						}
					}
					else
					{
						timer3Ticks -= clockTicks;
						if (timer3Ticks <= 0)
						{
							timer3Ticks += (0x10000 - timer3Reload) << timer3ClockReload;
							if (TM3CNT & 0x40)
							{
								IF |= 0x40;
								UPDATE_REG(0x202, IF);
							}
						}
						TM3D = 0xFFFF - (timer3Ticks >> timer3ClockReload);
						UPDATE_REG(0x10C, TM3D);
					}
				}
			}

			timerOverflow = 0;

			ticks -= clockTicks;
#ifdef LINK_EMULATION
			if (linkenable)
				LinkUpdate(clockTicks);
#endif
			cpuNextEvent = CPUUpdateTicks();

			if (cpuDmaTicksToUpdate > 0)
			{
				if (cpuDmaTicksToUpdate > cpuNextEvent)
					clockTicks = cpuNextEvent;
				else
					clockTicks = cpuDmaTicksToUpdate;
				cpuDmaTicksToUpdate -= clockTicks;
				if (cpuDmaTicksToUpdate < 0)
					cpuDmaTicksToUpdate = 0;
				goto updateLoop;
			}
#ifdef LINK_EMULATION
			if (linkenable)
				cpuNextEvent = 1;
#endif
			if (IF && (IME & 1) && CPU::armIrqEnable)
			{
				int res = IF & IE;
				if (stopState)
					res &= 0x3080;
				if (res)
				{
					if (intState)
					{
						if (!IRQTicks)
						{
							CPU::interrupt();
							intState = false;
							holdState = false;
							stopState = false;
						}
					}
					else
					{
						if (!holdState)
						{
							intState = true;
							IRQTicks=7;
							if (cpuNextEvent> IRQTicks)
								cpuNextEvent = IRQTicks;
						}
						else
						{
							CPU::interrupt();
							holdState = false;
							stopState = false;
						}
					}
				}
			}

			if (remainingTicks > 0)
			{
				if (remainingTicks > cpuNextEvent)
					clockTicks = cpuNextEvent;
				else
					clockTicks = remainingTicks;
				remainingTicks -= clockTicks;
				if (remainingTicks < 0)
					remainingTicks = 0;
				goto updateLoop;
			}

			if (timerOnOffDelay)
				applyTimer();

			if (cpuNextEvent > ticks)
				cpuNextEvent = ticks;

			if (ticks <= 0 || cpuBreakLoop)
				break;

		}
	}
}

guint gba_get_speed() {
	return speed;
}

void gba_init_input(InputDriver *driver) {
	inputDriver = driver;
}
