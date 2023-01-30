//Emulation of the SD card itself, providing an interface for sdperiph
//Not really a finished implementation, but should work well enough for this purpose.
//At some point this should really be converted to run async to speed things up
#pragma once
#include "../helper.h"
namespace Emu293 {
	//Init card, mounting a given disk image
	bool SD_InitCard(const char *filename);
	//Reinit card, without loading a new disk image
	void SD_ResetCard();
	//Send command, optionally giving an argument
	void SD_Command(uint8_t command, uint32_t argument = 0);
	//Read a command response byte
	uint32_t SD_Command_ReadResponse();

	//Write data to card
	void SD_Write(uint8_t *buf, int len);
	//Read data from card
	void SD_Read(uint8_t *buf, int len);
	//SD card savestate handler
	void SD_State(SaveStater &s);
}
