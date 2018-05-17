// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 1999-2003 Forgotten
// Copyright (C) 2005-2006 Forgotten and the VBA development team

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "CartridgeEEprom.h"

#include "string.h"

#define EEPROM_IDLE           0
#define EEPROM_READADDRESS    1
#define EEPROM_READDATA       2
#define EEPROM_READDATA2      3
#define EEPROM_WRITEDATA      4

static int eepromMode = EEPROM_IDLE;
static int eepromByte = 0;
static int eepromBits = 0;
static int eepromAddress = 0;
static guint8 eepromData[0x2000];
static guint8 eepromBuffer[16];
static int eepromSize = 0x0200;

void cartridge_eeprom_init()
{
	memset(eepromData, 0xFF, 0x2000);
}

void cartridge_eeprom_reset(int size)
{
	eepromMode = EEPROM_IDLE;
	eepromByte = 0;
	eepromBits = 0;
	eepromAddress = 0;
	eepromSize = size;
}

int cartridge_eeprom_read(guint32 address)
{
	switch (eepromMode)
	{
	case EEPROM_IDLE:
	case EEPROM_READADDRESS:
	case EEPROM_WRITEDATA:
		return 1;
	case EEPROM_READDATA:
	{
		eepromBits++;
		if (eepromBits == 4)
		{
			eepromMode = EEPROM_READDATA2;
			eepromBits = 0;
			eepromByte = 0;
		}
		return 0;
	}
	case EEPROM_READDATA2:
	{
		int data = 0;
		int address = eepromAddress << 3;
		int mask = 1 << (7 - (eepromBits & 7));
		data = (eepromData[address+eepromByte] & mask) ? 1 : 0;
		eepromBits++;
		if ((eepromBits & 7) == 0)
			eepromByte++;
		if (eepromBits == 0x40)
			eepromMode = EEPROM_IDLE;
		return data;
	}
	default:
		return 0;
	}
	return 1;
}

void cartridge_eeprom_write(guint32 address, guint8 value)
{
	int bit = value & 1;
	switch (eepromMode)
	{
	case EEPROM_IDLE:
		eepromByte = 0;
		eepromBits = 1;
		eepromBuffer[eepromByte] = bit;
		eepromMode = EEPROM_READADDRESS;
		break;
	case EEPROM_READADDRESS:
		eepromBuffer[eepromByte] <<= 1;
		eepromBuffer[eepromByte] |= bit;
		eepromBits++;
		if ((eepromBits & 7) == 0)
		{
			eepromByte++;
		}
		if (eepromSize == 0x2000) // 64K
		{
			if (eepromBits == 0x11)
			{
				eepromAddress = ((eepromBuffer[0] & 0x3F) << 8) |
				                ((eepromBuffer[1] & 0xFF));
				if (!(eepromBuffer[0] & 0x40))
				{
					eepromBuffer[0] = bit;
					eepromBits = 1;
					eepromByte = 0;
					eepromMode = EEPROM_WRITEDATA;
				}
				else
				{
					eepromMode = EEPROM_READDATA;
					eepromByte = 0;
					eepromBits = 0;
				}
			}
		}
		else // 4K
		{
			if (eepromBits == 9)
			{
				eepromAddress = (eepromBuffer[0] & 0x3F);
				if (!(eepromBuffer[0] & 0x40))
				{
					eepromBuffer[0] = bit;
					eepromBits = 1;
					eepromByte = 0;
					eepromMode = EEPROM_WRITEDATA;
				}
				else
				{
					eepromMode = EEPROM_READDATA;
					eepromByte = 0;
					eepromBits = 0;
				}
			}
		}
		break;
	case EEPROM_READDATA:
	case EEPROM_READDATA2:
		// should we reset here?
		eepromMode = EEPROM_IDLE;
		break;
	case EEPROM_WRITEDATA:
		eepromBuffer[eepromByte] <<= 1;
		eepromBuffer[eepromByte] |= bit;
		eepromBits++;
		if ((eepromBits & 7) == 0)
		{
			eepromByte++;
		}
		if (eepromBits == 0x40)
		{
			// write data;
			for (int i = 0; i < 8; i++)
			{
				eepromData[(eepromAddress << 3) + i] = eepromBuffer[i];
			}
		}
		else if (eepromBits == 0x41)
		{
			eepromMode = EEPROM_IDLE;
			eepromByte = 0;
			eepromBits = 0;
		}
		break;
	}
}

gboolean cartridge_eeprom_read_battery(FILE *file, size_t size)
{
	return fread(eepromData, 1, size, file) == size;
}

gboolean cartridge_eeprom_write_battery(FILE *file)
{
	return fwrite(eepromData, 1, eepromSize, file) == (size_t)eepromSize;
}


