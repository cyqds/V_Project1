#include <stdio.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#define BUFFER_COUNT 4
#define DEVICE_PATH "/dev/video0"
#define WIDTH 320
#define HEIGHT 180
#define V4L2_PIX_FMT V4L2_PIX_FMT_YUYV
#define FRAME_COUNT 10

typedef struct {
    int fd;
    int buffer_index; // Index of the buffer this thread will handle
    unsigned char *buffer;
    unsigned int length;
} ThreadData;

pthread_mutex_t lock; // 全局互斥锁
void *capture_and_save(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    int fd = data->fd;
    int buffer_index = data->buffer_index;
    unsigned char *buffer = data->buffer;
    unsigned int length = data->length;

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    capturebuffer.memory = V4L2_MEMORY_MMAP;
    capturebuffer.index = buffer_index; // Use the buffer index assigned to this thread

    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        pthread_mutex_lock(&lock); // 锁定互斥锁
        if (ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
            perror("dqbuf failed");
            return NULL;
        }

        // Save to file
        char filename[32];
        snprintf(filename, sizeof(filename), "./output/capture%d", frame);

        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            perror("fopen failed");
            return NULL;
        }

        fwrite(buffer + capturebuffer.m.offset, 1, capturebuffer.bytesused, fp);
        fclose(fp);

        // Inform the driver that the buffer is free
        if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
            perror("qbuf failed");
            return NULL;
        }
        pthread_mutex_unlock(&lock); // 解锁互斥锁
    }
    return NULL;
}

int main() {
    pthread_mutex_init(&lock, NULL); // 初始化互斥锁
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    struct v4l2_format vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vfmt.fmt.pix.width = WIDTH;
    vfmt.fmt.pix.height = HEIGHT;
    vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT;
    if (ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
        perror("setting format failed");
        close(fd);
        return -1;
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = BUFFER_COUNT;
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers failed");
        close(fd);
        return -1;
    }

    unsigned char *buffers[BUFFER_COUNT];
    unsigned int lengths[BUFFER_COUNT];
    struct v4l2_buffer mapbuffer;
    memset(&mapbuffer, 0, sizeof(mapbuffer));
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pthread_t threads[BUFFER_COUNT];
    ThreadData thread_data[BUFFER_COUNT];

    for (int i = 0; i < BUFFER_COUNT; i++) {
        mapbuffer.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            perror("query buffer failed");
            close(fd);
            return -1;
        }

        buffers[i] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.offset);
        lengths[i] = mapbuffer.length;

        thread_data[i].fd = fd;
        thread_data[i].buffer_index = i;
        thread_data[i].buffer = buffers[i];
        thread_data[i].length = lengths[i];

        if (ioctl(fd, VIDIOC_QBUF, &mapbuffer) < 0) {
            perror("qbuf failed");
            close(fd);
            return -1;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon failed");
        close(fd);
        return -1;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, capture_and_save, &thread_data[i]) != 0) {
            perror("pthread_create failed");
            close(fd);
            return -1;
        }
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("streamoff failed");
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        munmap(buffers[i], lengths[i]);
    }

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers release failed");
    }
    pthread_mutex_destroy(&lock); // 销毁互斥锁
    close(fd);
    return 0;
}