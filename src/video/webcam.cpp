#include "webcam.h"

#ifdef _WIN32
namespace Emu293 {
// TODO
void webcam_init(const std::string &device) {
}
bool webcam_grab_frame_rgb565(uint8_t *out, int w, int h) {
    return false;
}
void webcam_stop() {
}
}

#else

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <vector>

#include "libv4l2.h"

// Reference: https://www.kernel.org/doc/html/v4.8/media/uapi/v4l/v4l2grab.c.html

namespace Emu293 {
namespace {
    struct buffer {
        void   *start;
        size_t length;
    };

    int cam_fd = -1;
    struct v4l2_format fmt = {};
    std::vector<struct buffer> buffers;

    const int out_w = 640, out_h = 480;

    static void xioctl(int fh, int request, void *arg)
    {
        int r;
        do {
                r = v4l2_ioctl(fh, request, arg);
        } while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));
        if (r == -1) {
            printf("error %d, %s\n", errno, strerror(errno));
            exit(1);
        }
    }
};

void webcam_init(const std::string &device) {
    cam_fd = v4l2_open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (cam_fd < 0) {
        printf("failed to open V4L device %s: %s\n", device.c_str(), strerror(errno));
        exit(1);
    }
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = out_w;
    fmt.fmt.pix.height      = out_h;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    xioctl(cam_fd, VIDIOC_S_FMT, &fmt);
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
        printf("failed to set pixel format to RGB24\n");
        exit(1);
    }
    if ((fmt.fmt.pix.width != 640) || (fmt.fmt.pix.height != 480))
        printf("video output: request %dx%d, got %dx%d\n", out_w, out_h, fmt.fmt.pix.width, fmt.fmt.pix.height);

    struct v4l2_requestbuffers req = {};
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    xioctl(cam_fd, VIDIOC_REQBUFS, &req);

    buffers.resize(req.count);
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        xioctl(cam_fd, VIDIOC_QUERYBUF, &buf);

        buffers[i].length = buf.length;
        buffers[i].start = v4l2_mmap(NULL, buf.length,
                      PROT_READ | PROT_WRITE, MAP_SHARED,
                      cam_fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < int(buffers.size()); ++i) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        xioctl(cam_fd, VIDIOC_QBUF, &buf);
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam_fd, VIDIOC_STREAMON, &type);
}

void webcam_stop() {
    if (cam_fd == -1)
        return; // never used...
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam_fd, VIDIOC_STREAMOFF, &type);
    for (auto &buf : buffers)
        v4l2_munmap(buf.start, buf.length);
    v4l2_close(cam_fd);
}

static void rgb888to565(uint8_t *in, uint8_t *out) {
    uint8_t r = in[0], g = in[1], b = in[2];
    r >>= 3;
    g >>= 2;
    b >>= 3;
    uint16_t result = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
    *(reinterpret_cast<uint16_t*>(out))=result;
}

bool webcam_grab_frame_rgb565(uint8_t *out, int w, int h) {
    if (cam_fd == -1)
        return false;
    int r = 0;
    struct timeval tv = {};
    fd_set cam_fds;
    do {
        FD_ZERO(&cam_fds);
        FD_SET(cam_fd, &cam_fds);

        /* Timeout. */
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        r = select(cam_fd + 1, &cam_fds, NULL, NULL, &tv);
    } while ((r == -1 && (errno == EINTR)));
    if (r == -1) {
        perror("select");
        return errno;
    }
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    xioctl(cam_fd, VIDIOC_DQBUF, &buf);
    for (int oy = 0; oy < h; oy++) {
        for (int ox = 0; ox < w; ox++) {
            if (oy >= fmt.fmt.pix.height || ox >= fmt.fmt.pix.width)
                continue;
            int pix_idx = (oy * fmt.fmt.pix.width + ox) * 3;
            int out_idx = ((((h - 1) - oy)  * w) + ((w-1)-ox)) * 2;
            rgb888to565(reinterpret_cast<uint8_t*>(buffers[buf.index].start) + pix_idx, out + out_idx);
        }
    }
    xioctl(cam_fd, VIDIOC_QBUF, &buf);
    return true;
}
}
#endif