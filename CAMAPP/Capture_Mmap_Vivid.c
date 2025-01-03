#include <sys/select.h>
#include <sys/time.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <getopt.h>

#define BUFFER_COUNT 5

typedef struct {
    int fd;
    unsigned char **buffers;
    unsigned int *lengths;
    int frame_count;
} Thread_data;

// check if a directory exists, create it if not
int ensure_dir_exists(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        //if the directory does not exist, create it
        if (mkdir(path, 0777) != 0) {
            perror("mkdir failed");
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        //path exists but is not a directory
        fprintf(stderr, "Error: %s is not a directory\n", path);
        return -1;
    }
    return 0;
}

void *capture_and_save(void *arg) {
    Thread_data *data = (Thread_data *)arg;
    int fd = data->fd;
    unsigned char **buffers = data->buffers;
    unsigned int *lengths = data->lengths;
    int frame_count = data->frame_count;

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    capturebuffer.memory = V4L2_MEMORY_MMAP;

    fd_set fds;
    struct timeval tv;

    for (int frame = 0; frame < frame_count; frame++) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            perror("select error");
            return NULL;
        } else if (ret == 0) {
            perror("select timeout");
            continue;
        }
        if (FD_ISSET(fd, &fds)) {
            if (ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
                perror("dqbuf failed");
                return NULL;
            }
            char filename[32];
            if (ensure_dir_exists("./output") != 0) {
                fprintf(stderr, "Error: failed to create output directory\n");
                return NULL;
            }
            snprintf(filename, sizeof(filename), "./output/frame%d", frame);
            FILE *fp = fopen(filename, "wb");
            if (fp == NULL) {
                perror("fopen failed");
                return NULL;
            }
            unsigned char *buffer = buffers[capturebuffer.index];
            unsigned int length = lengths[capturebuffer.index];
            // printf("capture_ndex%d\n", capturebuffer.index);
            // fwrite(buffer + capturebuffer.m.offset, 1, capturebuffer.bytesused, fp);
            fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
            fclose(fp);
            if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
                perror("qbuf failed");
                return NULL;
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *device = NULL;
    int width = 0;
    int height = 0;
    int pixelformat = 0;
    int frame_count = 0;

    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"pixelformat", required_argument, 0, 'p'},
        {"frame_count", required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "d:w:h:p:f:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                device = optarg;
                break;
            case 'w':
                width = strtol(optarg, NULL, 10);
                break;
            case 'h':
                height = strtol(optarg, NULL, 10);
                break;
            case 'p':
                if (strlen(optarg) == 4) {
                    pixelformat = v4l2_fourcc(optarg[0], optarg[1], optarg[2], optarg[3]);
                } else {
                    fprintf(stderr, "Invalid pixel format\n");
                    return -1;
                }
                break;
            case 'f':
                frame_count = strtol(optarg, NULL, 10);
                break;
            default:
                fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>]\n", argv[0]);
                return -1;
        }
    }

    if (!device || width <= 0 || height <= 0 || pixelformat == 0 || frame_count <= 0) {
        fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>]\n", argv[0]);
        return -1;
    }

    int fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        perror("set format failed");
        return -1;
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request buffer failed");
        return -1;
    }

    unsigned char *buffers[BUFFER_COUNT];
    unsigned int lengths[BUFFER_COUNT];
    struct v4l2_buffer bufferinfo;
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        bufferinfo.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
            perror("query buffer failed");
            return -1;
        }
        buffers[i] = (unsigned char *)mmap(NULL, bufferinfo.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bufferinfo.m.offset);
        lengths[i] = bufferinfo.length;
        if (buffers[i] == MAP_FAILED) {
            perror("mmap failed");
            return -1;
        }
        if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
            perror("qbuf failed");
            return -1;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon failed");
        return -1;
    }

    pthread_t thread;
    Thread_data thread_data;
    thread_data.fd = fd;
    thread_data.buffers = buffers;
    thread_data.lengths = lengths;
    thread_data.frame_count = frame_count;
    if (pthread_create(&thread, NULL, capture_and_save, &thread_data) != 0) {
        perror("pthread_create failed");
        return -1;
    }
    pthread_join(thread, NULL);

    if(ioctl(fd, VIDIOC_STREAMOFF, &type) < 0){
        perror("streamoff failed");
        return -1;
    }

    for(int i=0; i<BUFFER_COUNT; i++){
        munmap(buffers[i], lengths[i]);
    }

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0){
        perror("request buffer failed");
        return -1;
    }
    
    close(fd);
    return 0;


}