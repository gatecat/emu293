#pragma once
#include "../helper.h"

namespace Emu293 {
	void webcam_init(const std::string &device);
	bool webcam_grab_frame_rgb565(uint8_t *buf, int w, int h);
	void webcam_stop();
};