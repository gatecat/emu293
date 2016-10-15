#include "loadelf.h"
namespace Emu293 {
	const uint8_t magic[] = { 0x7F, 'E', 'L', 'F', 0x01, 0x01, 0x01, 0x00};
	uint32_t LoadElfToRAM(const char *filename) {
		FILE  *elfFile;
		elfFile = fopen(filename,"rb");
		if(!elfFile) {
			printf("Failed to open ELF file\n");
			return 0;
		}
		uint8_t elfHeader[52];
		if(fread(elfHeader,1,52,elfFile) != 52) {
			printf("Failed to read ELF header\n");
			return 0;
		}
		if(memcmp(elfHeader, magic, sizeof(magic)) != 0) {
			printf("Not a valid ELF file\n");
			return 0;
		}
		uint32_t entryPoint = get_uint32le(&(elfHeader[0x18]));
		uint32_t phOff = get_uint32le(&(elfHeader[0x1C]));
		uint16_t phSize = get_uint16le(&(elfHeader[0x2A]));
		uint16_t phNum = get_uint16le(&(elfHeader[0x2C]));
		if((phSize < 32) || (phNum < 1)) {
			printf("Bad or missing ELF program header\n");
			return 0;
		}
		uint8_t progHeader[32];
		fseek(elfFile, phOff, 0);
		if(fread(progHeader,1,32,elfFile) != 32) {
			printf("Failed to read ELF program header\n");
			return 0;
		}
		if(get_uint32le(progHeader) != 1) {
			printf("Segment 0 has an invalid type\n");
			return 0;
		}

		uint32_t segOff = get_uint32le(&(progHeader[4]));
		uint32_t segVaddr = get_uint32le(&(progHeader[8]));
		uint32_t segFileSz= get_uint32le(&(progHeader[16]));
		uint32_t segMemSz= get_uint32le(&(progHeader[20]));
		fseek(elfFile, segOff, 0);
		uint8_t * tmpBuf = new uint8_t[segMemSz];
		memset(tmpBuf, 0,segMemSz);
		if(fread(tmpBuf,1,segFileSz,elfFile) != segFileSz) {
			printf("Failed to read ELF executable code\n");
			return 0;
		}
		for(int i = 0; i < segMemSz; i++) {
			//printf("Write 0x%08x to 0x%08x\n",tmpBuf[i],segVaddr + i);
			write_memU8(segVaddr + i,tmpBuf[i]);
		}
		delete[] tmpBuf;

		return entryPoint;
	}
}

