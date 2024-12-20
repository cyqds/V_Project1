#include <stdio.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>    // For malloc and free
#include <string.h>    // For memset

int main() {
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    struct v4l2_format vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vfmt.fmt.pix.width = 320;
    vfmt.fmt.pix.height = 180;
    vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    if (ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
        perror("setting format failed");
        close(fd);
        return -1;
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_USERPTR;
    reqbuf.count = 4;
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers failed");
        close(fd);
        return -1;
    }

    unsigned char *buffers[4];
    unsigned int lengths[4];
    struct v4l2_buffer mapbuffer;
    memset(&mapbuffer, 0, sizeof(mapbuffer));
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for (int i = 0; i < reqbuf.count; i++) {
        mapbuffer.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            perror("query buffer failed");
            close(fd);
            return -1;
        }

        // Allocate memory
        buffers[i] = (unsigned char *)malloc(mapbuffer.length);
        if (!buffers[i]) {
            perror("malloc failed");
            close(fd);
            // Free previously allocated buffers
            for (int j = 0; j < i; j++) {
                free(buffers[j]);
            }
            return -1;
        }
        lengths[i] = mapbuffer.length;

        // Set user pointer
        mapbuffer.memory = V4L2_MEMORY_USERPTR;
        mapbuffer.m.userptr = (unsigned long)buffers[i];

        // Queue the buffer
        if (ioctl(fd, VIDIOC_QBUF, &mapbuffer) < 0) {
            perror("VIDIOC_QBUF failed");
            // Free allocated buffers
            for (int j = 0; j <= i; j++) {
                free(buffers[j]);
            }
            close(fd);
            return -1;
        }
    }

    // Start capturing
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon failed");
        // Free allocated buffers
        for (int j = 0; j < reqbuf.count; j++) {
            free(buffers[j]);
        }
        close(fd);
        return -1;
    }

    // Capture one frame
    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    capturebuffer.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
        perror("dqbuf failed");
        // Free allocated buffers
        for (int j = 0; j < reqbuf.count; j++) {
            free(buffers[j]);
        }
        close(fd);
        return -1;
    }

    FILE *fp = fopen("capture.yuyv", "wb");
    if (!fp) {
        perror("fopen failed");
        // Free allocated buffers
        for (int j = 0; j < reqbuf.count; j++) {
            free(buffers[j]);
        }
        close(fd);
        return -1;
    }
    fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
    fclose(fp);

    // Re-queue the buffer
    capturebuffer.m.userptr = (unsigned long)buffers[capturebuffer.index];
    if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
        perror("VIDIOC_QBUF failed");
        // Free allocated buffers
        for (int j = 0; j < reqbuf.count; j++) {
            free(buffers[j]);
        }
        close(fd);
        return -1;
    }

    // Stop capturing
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("streamoff failed");
        // Free allocated buffers
        for (int j = 0; j < reqbuf.count; j++) {
            free(buffers[j]);
        }
        close(fd);
        return -1;
    }

    // Free buffers
    for (int i = 0; i < reqbuf.count; i++) {
        free(buffers[i]);
    }

    close(fd);
    return 0;
}