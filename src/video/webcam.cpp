#include "webcam.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
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
    fd_set cam_fds;
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
}