#include "timer.h"
#include "irq_if.h"
#include <cstdio>
using namespace std;

//Limited timer support. No PWM or capture/compare

#define NTIMERS 6

#define TIMER_CTRL				0
#define TIMER_CTRL_EN			31U
#define TIMER_CTRL_IRQ_EN		27U
#define TIMER_CTRL_IRQ_FLAG 	26U

#define TIMER_CTRL_CCP 			1
#define TIMER_PRELOAD 			2
#define TIMER_CPP_REGS			3
#define TIMER_UPCOUNT			4
#define TIMER_NREGS				5

#define CKG_TMR_CLKSEL			57
#define CKG_NREGS 				88

namespace Emu293 {
	// CKG sits here because we do frequency stuff here; maybe not the wright place though?
	uint32_t ckg_regs[CKG_NREGS] = {0};

	void CKGDeviceWriteHandler(uint16_t addr, uint32_t val) {
	  addr /= 4;
	  if (addr < CKG_NREGS) {
		ckg_regs[addr] = val;
	  } else {
		printf("CKG write error: address 0x%04x out of range, dat=0x%08x\n",
			   addr * 4, val);
	  }
	}

	uint32_t CKGDeviceReadHandler(uint16_t addr) {
	  addr /= 4;
	  if (addr < CKG_NREGS) {
		return ckg_regs[addr];
	  } else {
		printf("CKG read error: address 0x%04x out of range\n", addr * 4);
		return 0;
	  }
	}

	void CKGDeviceResetHandler() {
		for (auto &r : ckg_regs)
			r = 0;
	}

	void CKGDeviceState(SaveStater &s) {
		s.tag("CKG");
		s.a(ckg_regs);
	}

	bool get_clock_enable(uint32_t offset) {
		return ckg_regs[offset/4] & 0x1;
	}

	void InitCKGDevice(PeripheralInitInfo initInfo) {}

	const Peripheral CKGPeripheral = {"CKG", InitCKGDevice,
										 CKGDeviceReadHandler,
										 CKGDeviceWriteHandler,
										 CKGDeviceResetHandler,
										 CKGDeviceState};

	uint32_t timer_regs[NTIMERS][TIMER_NREGS] = {0};

	void InitTimerDevice(PeripheralInitInfo initInfo) {

	}

	int div_count = 0; 

	void TimerTick(bool is_32khz) {
		if (!is_32khz) {
			// clock divider
			++div_count;
			if (div_count < (ckg_regs[CKG_TMR_CLKSEL] & 0xff))
				return;
			else
				div_count = 0;
		}
		for(int i = 0; i < NTIMERS; i++) {
			//printf("tmr%d st = 0x%08x\n",i,timer_regs[i][TIMER_CTRL]);
			// check which clock this timer runs on
			if (check_bit(ckg_regs[CKG_TMR_CLKSEL], 8 + i) != is_32khz)
				continue;
			if(check_bit(timer_regs[i][TIMER_CTRL],TIMER_CTRL_EN) && (check_bit(timer_regs[i][TIMER_CTRL_CCP],31)==check_bit(timer_regs[i][TIMER_CTRL_CCP],30))) {
				if(timer_regs[i][TIMER_UPCOUNT] <= 0xFFFF) {
					timer_regs[i][TIMER_UPCOUNT]++;
				} else {
					timer_regs[i][TIMER_UPCOUNT] = timer_regs[i][TIMER_PRELOAD];
					if(check_bit(timer_regs[i][TIMER_CTRL],TIMER_CTRL_IRQ_EN)) {
						SetIRQState(56, true);
						set_bit(timer_regs[i][TIMER_CTRL],TIMER_CTRL_IRQ_FLAG);
					}
				}
			}
		}
	}
	uint32_t TimerDeviceReadHandler(uint16_t addr) {
		uint32_t tmr = (addr >> 12) & 0x0F;
		if(tmr >= NTIMERS) {
			printf("TMR device read error: address 0x%04x out of bounds\n",addr);
			return 0;
		} else {
			uint32_t addr32 = (addr & 0xFF) / 4;
			if(addr32 >= TIMER_NREGS) {
				printf("TMR device read error: address 0x%04x out of bounds\n",addr);
				return 0;
			} else {
				return timer_regs[tmr][addr32];
			}
		}
	}
	void TimerDeviceWriteHandler(uint16_t addr, uint32_t val) {
		uint32_t tmr = (addr >> 12) & 0x0F;
		if(tmr >= NTIMERS) {
			printf("TMR device read error: address 0x%04x out of bounds\n",addr);
		} else {
			uint32_t addr32 = (addr & 0xFF) / 4;
			if(addr32 >= TIMER_NREGS) {
				printf("TMR device read error: address 0x%04x out of bounds\n",addr);
			} else {
			//	printf("TMR%d Write 0x%08x to 0x%02x!\n",tmr, val,addr32);

				timer_regs[tmr][addr32] = val;
				if(addr32 == TIMER_CTRL) {
					if(check_bit(val,TIMER_CTRL_IRQ_FLAG) || (!check_bit(val,TIMER_CTRL_EN)) || (!check_bit(val,TIMER_CTRL_IRQ_EN))) {
						SetIRQState(56, false);
						clear_bit(timer_regs[tmr][TIMER_CTRL],TIMER_CTRL_IRQ_FLAG);
					}
				}
				if(addr32 == TIMER_PRELOAD) {
					timer_regs[tmr][TIMER_UPCOUNT] = val;
				}
			}
		}

	}

	void TimerDeviceResetHandler() {
		for (auto &t : timer_regs)
			for (auto &r : t)
				r = 0;
	}

	void TimerDeviceState(SaveStater &s) {
		s.tag("TIMER");
		s.i(div_count);
		for (int i = 0; i < NTIMERS; i++)
			s.a(timer_regs[i]);
	}

	const Peripheral TimerPeripheral = {
			"TIMER",
			InitTimerDevice,
			TimerDeviceReadHandler,
			TimerDeviceWriteHandler,
			TimerDeviceResetHandler,
			TimerDeviceState
	};
}
