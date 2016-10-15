#include "bufctl.h"
#include "../system.h"
#include "../helper.h"

#include <cstdio>

namespace Emu293 {
/*
 * Not a proper BUFCTL implementation
 * Due to the present lack of documentation, the PPU just renders to its own private (non-addressable)
 * memory shared with the TVE.
 */
	const int bufctl_ptr_setting		= 1;

	const int bufctl_ptrset_text1ctrl	= 1;
	const int bufctl_ptrset_text2ctrl	= 2;
	const int bufctl_ptrset_text3ctrl	= 3;
	const int bufctl_ptrset_ppuctrl		= 4;
	const int bufctl_ptrset_tvectrl		= 7;

	const int bufctl_text1_ptr			= 3;
	const int bufctl_text2_ptr			= 4;
	const int bufctl_text3_ptr			= 5;
	const int bufctl_ppu_ptr			= 6;
	const int bufctl_tve_ptr			= 8;

	const int bufctl_p2t_setting		= 15;

	const int bufctl_p2t_p2ten			= 0;

	const int bufctl_nreg				= 20;


	 //For debugging purposes, print a message if any register address set to false is accessed
	 //as they are not currently implemented

												// 0      1      2      3      4      5      6      7      8      9
	const bool bufctl_mapped_regs[bufctl_nreg] = { false, true , false, true , true , true , true , false, true , false,
			                                       false, false, false, false, false, true , false, false, false, true
                                                 };

	static uint32_t bufctl_regs[bufctl_nreg] = { 0 };

	int BufCtl_GetTextBufferIndex(int textNum) {
		switch(textNum) {
		case 1:
			return (bufctl_regs[bufctl_text1_ptr] & 0x03);
		case 2:
			return (bufctl_regs[bufctl_text2_ptr] & 0x03);
		case 3:
			return (bufctl_regs[bufctl_text2_ptr] & 0x03);
		default:
			printf("BufCtl error: request for text buffer number %d which is out of range.\n",textNum);
			return 0;
		}
	}
	int BufCtl_GetPPUBufferIndex() {
		return bufctl_regs[bufctl_ppu_ptr];
	}
	void BufCtl_PPUPaintDone() {
		//increment pointers if required
		//is this behaviour correct? not sure
		if(check_bit(bufctl_regs[bufctl_ptr_setting],bufctl_ptrset_text1ctrl)) {
			bufctl_regs[bufctl_text1_ptr] = (bufctl_regs[bufctl_text1_ptr] + 1) % 2;
		}
		if(check_bit(bufctl_regs[bufctl_ptr_setting],bufctl_ptrset_text2ctrl)) {
			bufctl_regs[bufctl_text2_ptr] = (bufctl_regs[bufctl_text2_ptr] + 1) % 2;
		}
		if(check_bit(bufctl_regs[bufctl_ptr_setting],bufctl_ptrset_text3ctrl)) {
			bufctl_regs[bufctl_text3_ptr] = (bufctl_regs[bufctl_text3_ptr] + 1) % 2;
		}

	}
}
