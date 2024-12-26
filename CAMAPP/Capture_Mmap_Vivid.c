#include <poll.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>




#define BUFFER_COUNT 4

typedef struct{
    int fd;
    unsigned char *buffer;
    unsigned int length;
    int frame_count;
}Thread_data;

void *capture_and_save(void *arg){
    Thread_data *data = (Thread_data *)arg;
    int fd = data->fd;
    unsigned char *buffer = data->buffer;
    unsigned int length = data->length;
    int frame_count = data->frame_count;

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    capturebuffer.memory = V4L2_MEMORY_MMAP;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    for(int frame=-0; frame<frame_count; frame++){
        int ret = poll(&pfd, 1, 5000);
        if(ret == -1){
            perror("poll error");
            return NULL;
        }
        else if(ret == 0){
            perror("poll timeout");
            continue;
        }
        if(pfd.revents & POLLIN){
            if(ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0){
                perror("dqbuf failed");
                return NULL;
            }
            char filename[32];
            snprintf(filename,sizeof(filename), "./output/frame%d", frame);
            FILE *fp = fopen(filename, "wb");
            if(fp == NULL){
                perror("fopen failed");
                return NULL;
            }
            fwrite(buffer+capturebuffer.m.offset, 1, capturebuffer.bytesused, fp);
            fclose(fp);
            if(ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0){
                perror("qbuf failed");
                return NULL;
            }    
        }
    }
    return NULL;
}

int main(int argc,char **argv){
    if (argc <6){
        fprintf(stderr, "Usage: %s <device> <width> <height> <pixelformat> <frame_count>\n", argv[0]);
        return -1;
    }
    const char *device = argv[1];
    int width = strtol(argv[2], NULL, 10);
    int height = strtol(argv[3], NULL, 10);
    int pixelformat = v4l2_fourcc(argv[4][0], argv[4][1], argv[4][2], argv[4][3]);
    int frame_count = strtol(argv[5], NULL, 10);
    int fd = open(device, O_RDWR|O_NONBLOCK, 0);
    if(fd < 0){
        perror("open failed");
        return -1;
    }
    
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    if(ioctl(fd, VIDIOC_S_FMT, &format) < 0){
        perror("set format failed");
        return -1;
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0){
        perror("request buffer failed");
        return -1;
    }

    unsigned char *buffers[BUFFER_COUNT];
    unsigned int lengths[BUFFER_COUNT];
    struct v4l2_buffer bufferinfo;
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for(int i=0; i<BUFFER_COUNT; i++){
        bufferinfo.index = i;
        if(ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0){
            perror("query buffer failed");
            return -1;
        }
        buffers[i] = (unsigned char *)mmap(NULL, bufferinfo.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, bufferinfo.m.offset);
        lengths[i] = bufferinfo.length;
        if(buffers[i] == MAP_FAILED){
            perror("mmap failed");
            return -1;
        }
        if(ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0){
            perror("qbuf failed");
            return -1;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0){
        perror("streamon failed");
        return -1;
    }

    pthread_t thread;
    Thread_data thread_data;
    thread_data.fd = fd;
    thread_data.buffer = buffers[0];
    thread_data.length = lengths[0];
    thread_data.frame_count = frame_count;
    if(pthread_create(&thread, NULL, capture_and_save, &thread_data) != 0){
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