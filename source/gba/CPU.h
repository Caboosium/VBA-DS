#ifndef GBACPU_H
#define GBACPU_H

#include "../common/Types.h"
#include "MMU.h"
#include "Globals.h" //TODO: Remove

namespace CPU
{

union reg_pair
{
	struct
	{
#ifdef WORDS_BIGENDIAN
		u8 B3;
		u8 B2;
		u8 B1;
		u8 B0;
#else
		u8 B0;
		u8 B1;
		u8 B2;
		u8 B3;
#endif
	} B;
	struct
	{
#ifdef WORDS_BIGENDIAN
		u16 W1;
		u16 W0;
#else
		u16 W0;
		u16 W1;
#endif
	} W;
#ifdef WORDS_BIGENDIAN
	volatile u32 I;
#else
	u32 I;
#endif
};

extern reg_pair reg[45];

extern bool N_FLAG;
extern bool C_FLAG;
extern bool Z_FLAG;
extern bool V_FLAG;

extern bool armState;
extern bool armIrqEnable;
extern u32 armNextPC;
extern int armMode;

extern bool busPrefetch;
extern bool busPrefetchEnable;
extern u32 busPrefetchCount;

extern u32 cpuPrefetch[2];
extern u8 cpuBitsSet[256];

void init();
void reset();
void interrupt();
void enableBusPrefetch(bool enable);

int armExecute();
int thumbExecute();

int dataTicksAccess16(u32 address);
int dataTicksAccess32(u32 address);
int dataTicksAccessSeq16(u32 address);
int dataTicksAccessSeq32(u32 address);
int codeTicksAccess16(u32 address);
int codeTicksAccess32(u32 address);
int codeTicksAccessSeq16(u32 address);
int codeTicksAccessSeq32(u32 address);

void CPUSwitchMode(int mode, bool saveState);
void CPUSwitchMode(int mode, bool saveState, bool breakLoop);
void CPUUpdateCPSR();
void CPUUpdateFlags(bool breakLoop);
void CPUUpdateFlags();
void CPUUndefinedException();
void CPUSoftwareInterrupt();
void CPUSoftwareInterrupt(int comment);

inline void ARM_PREFETCH()
{
	cpuPrefetch[0] = MMU::read32(armNextPC);
	cpuPrefetch[1] = MMU::read32(armNextPC + 4);
}

inline void THUMB_PREFETCH()
{
	cpuPrefetch[0] = MMU::read16(armNextPC);
	cpuPrefetch[1] = MMU::read16(armNextPC + 2);
}

inline void ARM_PREFETCH_NEXT()
{
	cpuPrefetch[1] = MMU::read32(armNextPC+4);
}

inline void THUMB_PREFETCH_NEXT()
{
	cpuPrefetch[1] = MMU::read16(armNextPC+2);
}

} // namespace CPU

#endif // GBACPU_H
