#include "loadelf.h"

#include <vector>

namespace Emu293 {
	const uint8_t magic[] = { 0x7F, 'E', 'L', 'F', 0x01, 0x01, 0x01, 0x00};

	std::unordered_map<std::string, uint32_t> symbols_fwd;
	std::unordered_map<uint32_t, std::string> symbols_bwd;

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

		uint32_t shOff = get_uint32le(&(elfHeader[0x20]));
		uint16_t shSize = get_uint16le(&(elfHeader[0x2E]));
		uint16_t shNum = get_uint16le(&(elfHeader[0x30]));

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

		std::vector<char> strtab;
		for (int i = 0; i < shNum; i++) {
			fseek(elfFile, shOff + i * shSize, 0);
			uint8_t sectionHeader[40];
			if(fread(sectionHeader,1,40,elfFile) != 40) {
				printf("Failed to read ELF section %d header\n", i);
				return 0;
			}
			if (get_uint32le(&(sectionHeader[0x04])) == 0x3) {
				// String table
				strtab.clear();
				uint32_t strOff = get_uint32le(&(sectionHeader[0x10]));
				uint32_t strSize = get_uint32le(&(sectionHeader[0x14]));
				fseek(elfFile, strOff, 0);
				strtab.resize(strSize, '\0');
				fread(strtab.data(),1,strSize,elfFile);
			}
		}

		for (int i = 0; i < shNum; i++) {
			fseek(elfFile, shOff + i * shSize, 0);
			uint8_t sectionHeader[40];
			if(fread(sectionHeader,1,40,elfFile) != 40) {
				printf("Failed to read ELF section %d header\n", i);
				return 0;
			}
			if (get_uint32le(&(sectionHeader[0x04])) == 0x2) {
				// Symbol table
				symbols_fwd.clear();
				symbols_bwd.clear();
				uint32_t symOff = get_uint32le(&(sectionHeader[0x10]));
				uint32_t symSize = get_uint32le(&(sectionHeader[0x14]));
				fseek(elfFile, symOff, 0);
				uint8_t symbol[16];
				for (int j = 0; j < symSize; j += 16) {
					fread(symbol,1,16,elfFile);
					uint32_t symname = get_uint32le(&(symbol[0x0]));
					uint32_t symaddr = get_uint32le(&(symbol[0x4]));
					std::string symstr;
					while (symname < strtab.size() && strtab[symname] != '\0')
						symstr += strtab[symname++];
					symbols_fwd[symstr] = symaddr;
					symbols_bwd[symaddr] = symstr;
				}
			}
		}



		return entryPoint;
	}

	bool LoadNORToRAM(const char *filename, uint32_t &entryPoint, uint32_t &stackPtr) {
		FILE  *romFile;
		romFile = fopen(filename,"rb");
		if(!romFile) {
			printf("Failed to open NOR ROM file\n");
			return false;
		}
		uint8_t norHeader[0x20];
		if(fread(norHeader,1,0x20,romFile) != 32) {
			printf("Failed to read NOR file header\n");
			return false;
		}
		uint32_t load_addr = get_uint32le(&(norHeader[0x0C]));
		uint32_t stack_addr = get_uint32le(&(norHeader[0x10]));
		uint32_t entry_point = get_uint32le(&(norHeader[0x14]));
		printf("Load addr: %08x\n", load_addr);
		printf("Stack start?: %08x\n", stack_addr);
		printf("Entry point: %08x\n", entry_point);
		if ((load_addr & 0xFE000000) != 0xA0000000 || (entry_point & 0xFE000000) != 0xA0000000) {
			printf("NOR header addresses out of bound\n");
			return false;
		}
		//fseek(romFile, 0, 0);
		uint8_t byte = 0;
		while (fread(&byte,1,1,romFile)) {
			write_memU8(load_addr++, byte);
		}
		fclose(romFile);
		entryPoint = entry_point;
		stackPtr = stack_addr;
		return true;
	}

}

