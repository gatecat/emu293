#include <string>
#include <iostream>
#include <fstream>

#include "helper.h"

namespace Emu293 {
    std::string vstringf(const char *fmt, va_list ap)
    {
        std::string string;
        char *str = NULL;

    #if defined(_WIN32) || defined(__CYGWIN__)
        int sz = 64 + strlen(fmt), rc;
        while (1) {
            va_list apc;
            va_copy(apc, ap);
            str = (char *)realloc(str, sz);
            rc = vsnprintf(str, sz, fmt, apc);
            va_end(apc);
            if (rc >= 0 && rc < sz)
                break;
            sz *= 2;
        }
    #else
        if (vasprintf(&str, fmt, ap) < 0)
            str = NULL;
    #endif

        if (str != NULL) {
            string = str;
            free(str);
        }

        return string;
    }

    std::string stringf(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        std::string result = vstringf(format, ap);
        va_end(ap);
        return result;
    }

    void write_bmp(std::string filename, int width, int height, uint32_t *data) {
        std::ofstream out(filename);
        int rowsize = (3 * width);
        rowsize = ((rowsize + 3) / 4) * 4;
        uint8_t header[54];
        fill(header, header + 54, 0x0);
        header[0] = 0x42;
        header[1] = 0x4D;
        int imagesize = 54 + height * rowsize;
        header[2] = (imagesize & 0xFF);
        header[3] = ((imagesize >> 8) & 0xFF);
        header[4] = ((imagesize >> 16) & 0xFF);
        header[5] = ((imagesize >> 24) & 0xFF);
        header[10] = 54;
        header[14] = 40;
        header[18] = (width & 0xFF);
        header[19] = ((width >> 8) & 0xFF);
        header[20] = ((width >> 16) & 0xFF);
        header[21] = ((width >> 24) & 0xFF);
        header[22] = (height & 0xFF);
        header[23] = ((height >> 8) & 0xFF);
        header[24] = ((height >> 16) & 0xFF);
        header[25] = ((height >> 24) & 0xFF);
        header[26] = 1;
        header[27] = 0;
        header[28] = 24;
        header[38] = 0xD7;
        header[39] = 0xD;
        header[42] = 0xD7;
        header[43] = 0xD;
        header[49] = 0x1;
        header[53] = 0x1;
        out.write((const char *)header, 54);
        uint8_t *linebuf = new uint8_t[rowsize];
        uint8_t *lptr = linebuf;
        for (int y = height - 1; y >= 0; y--) {
            lptr = linebuf;
            for (int x = 0; x < width; x++) {
                uint32_t argb = data[y * width + x];
                *(lptr++) = (argb)&0xFF;
                *(lptr++) = (argb >> 8) & 0xFF;
                *(lptr++) = (argb >> 16) & 0xFF;
            }
            out.write((const char *)linebuf, rowsize);
        }
        delete[] linebuf;
    }
}