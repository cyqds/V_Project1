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
#include <errno.h>

#define BUFFER_COUNT 5

typedef struct {
    int capture_fd;
    int output_fd;
    unsigned char **buffers;
    unsigned char **buffers2;
    unsigned int *lengths2;
    unsigned int *lengths;
    int frame_count;
    const char *m2m_file;
    int size;
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
    int capture_fd = data->capture_fd;
    int output_fd = data->output_fd;
    unsigned char **buffers = data->buffers;
    unsigned char **buffers2 = data->buffers2;
    unsigned int *lengths = data->lengths;
    unsigned int *lengths2 = data->lengths2;
    int frame_count = data->frame_count;
    int size = data->size;

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.memory = V4L2_MEMORY_MMAP;

    fd_set fds;
    struct timeval tv;

    for (int frame = 0; frame < frame_count; frame++) {
        capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        FD_ZERO(&fds);
        FD_SET(capture_fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int ret = select(capture_fd + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            perror("select error");
            return NULL;
        } else if (ret == 0) {
            perror("select timeout");
            continue;
        }
        if (FD_ISSET(capture_fd, &fds)) {
            if (ioctl(capture_fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
                perror("dqbuf1 failed");
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
            printf("Writing frame %d, length: %d\n", frame, length);
            fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
            fclose(fp);
            if (ioctl(capture_fd, VIDIOC_QBUF, &capturebuffer) < 0) {
                perror("qbuf failed");
                return NULL;
            }
        }
        if (data->m2m_file) {
            struct v4l2_buffer outputbuffer;
            memset(&outputbuffer, 0, sizeof(outputbuffer));
            outputbuffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            outputbuffer.memory = V4L2_MEMORY_MMAP;

            if (ioctl(output_fd, VIDIOC_DQBUF, &outputbuffer) < 0) {
                perror("dqbuf2 failed");
                return NULL;
            }

            if (ioctl(output_fd, VIDIOC_QBUF, &outputbuffer) < 0) {
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
    const char *m2m_file = NULL;
    int type;
    int output_fd;
    int size;
    unsigned char *buffers2[BUFFER_COUNT];
    unsigned int lengths2[BUFFER_COUNT];

    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"pixelformat", required_argument, 0, 'p'},
        {"frame_count", required_argument, 0, 'f'},
        {"m2m_file", required_argument, 0, 'm'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "d:w:h:p:f:m:", long_options, &option_index)) != -1) {
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
            case 'm':
                m2m_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>] [--m2m_file=<file> | -m <file>]\n", argv[0]);
                return -1;
        }
    }

    if (!device || width <= 0 || height <= 0 || pixelformat == 0 || frame_count <= 0) {
        fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>] [--m2m_file=<file> | -m <file>]\n", argv[0]);
        return -1;
    }

    int capture_fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (capture_fd < 0) {
        perror("open capture device failed");
        return -1;
    }
    if(m2m_file){
        output_fd = open(device, O_RDWR | O_NONBLOCK, 0);
        if (output_fd < 0) {
            perror("open output device failed");
            return -1;
        }
    }
    

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    if (ioctl(capture_fd, VIDIOC_S_FMT, &format) < 0) {
        perror("set capture format failed");
        return -1;
    }
    if(m2m_file){
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        format.fmt.pix.width = 1920;
        format.fmt.pix.height = 1080;
        format.fmt.pix.pixelformat = pixelformat;
        if (ioctl(output_fd, VIDIOC_S_FMT, &format) < 0) {
            perror("set output format failed");
            return -1;
    }
    }
    

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(capture_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request capture buffer failed");
        return -1;
    }

    unsigned char *buffers[BUFFER_COUNT];
    unsigned int lengths[BUFFER_COUNT];
    struct v4l2_buffer bufferinfo;
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        bufferinfo.index = i;
        if (ioctl(capture_fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
            perror("query capture buffer failed");
            return -1;
        }
        buffers[i] = (unsigned char *)mmap(NULL, bufferinfo.length, PROT_READ | PROT_WRITE, MAP_SHARED, capture_fd, bufferinfo.m.offset);
        lengths[i] = bufferinfo.length;
        if (buffers[i] == MAP_FAILED) {
            perror("mmap capture buffer failed");
            return -1;
        }
        if (ioctl(capture_fd, VIDIOC_QBUF, &bufferinfo) < 0) {
            perror("queue capture buffer failed");
            return -1;
        }
    }
    if(m2m_file){
    int fp;

    char file_name[100];
    snprintf(file_name, sizeof(file_name), "%s", m2m_file);
    fp = open(file_name, O_RDONLY);
    if (fp < 0) {
        perror("Failed to open file");
        return -1;
    }
    size = lseek(fp, 0, SEEK_END);
    lseek(fp, 0, SEEK_SET); // Reset file pointer to the beginning
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(output_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request output buffer failed");
        return -1;
    }


    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        bufferinfo.index = i;
        if (ioctl(output_fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
            perror("query output buffer failed");
            return -1;
        }
        buffers2[i] = (unsigned char *)mmap(NULL, bufferinfo.length, PROT_READ | PROT_WRITE, MAP_SHARED, output_fd, bufferinfo.m.offset);
        lengths2[i] = bufferinfo.length;
        if (buffers2[i] == MAP_FAILED) {
            perror("mmap output buffer failed");
            return -1;
        }
        lseek(fp , 0 , SEEK_SET);
        if (read(fp, buffers2[i], size) != size) {
            perror("failed to read image file");
            return -1;
        }
        printf("read frame2 %d, length: %d\n", i, size);
        if (ioctl(output_fd, VIDIOC_QBUF, &bufferinfo) < 0) {
            perror("queue output buffer failed");
            return -1;
        }
    }
    close(fp);

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(output_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon output failed");
        return -1;
    }

    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(capture_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon capture failed");
        return -1;
    }

    pthread_t thread;
    Thread_data thread_data;
    thread_data.capture_fd = capture_fd;
    thread_data.buffers = buffers;
    thread_data.lengths = lengths;
    thread_data.frame_count = frame_count;
    thread_data.m2m_file = m2m_file;
    if(m2m_file){
        thread_data.size = size;
        thread_data.output_fd = output_fd;
        thread_data.buffers2 = buffers2;
        thread_data.lengths2 = lengths2;
    }
    if (pthread_create(&thread, NULL, capture_and_save, &thread_data) != 0) {
        perror("pthread_create failed");
        return -1;
    }
    pthread_join(thread, NULL);
    if(m2m_file){
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(output_fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("streamoff output failed");
        return -1;
        }
    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(capture_fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("streamoff capture failed");
        return -1;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        munmap(buffers[i], lengths[i]);
    }
    if(m2m_file){
        for (int i = 0; i < BUFFER_COUNT; i++) {
        munmap(buffers2[i], lengths2[i]);
    }
    }
    

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    if (ioctl(capture_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request capture buffer failed");
        return -1;
    }
    if(m2m_file){
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(output_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request output buffer failed");
        return -1;
    }
    close(output_fd);
    }
    

    close(capture_fd);
    return 0;
}