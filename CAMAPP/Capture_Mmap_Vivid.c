#include <stdio.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>    
#include <sys/mman.h>
#define BUFFER_COUNT 4
#define DEVICE_PATH "/dev/video0"
#define WIDTH 320
#define HEIGHT 180
#define V4L2_PIX_FMT V4L2_PIX_FMT_YUYV
#define FRAME_COUNT 20
int main(){
    //open device
    // int fd = open(DEVICE_PATH, O_RDWR | O_NONBLOCK, 0);
     int fd = open(DEVICE_PATH, O_RDWR );
    if(fd < 0){
        perror("open failed");
        return -1;
    }

    //setting format
    struct v4l2_format vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vfmt.fmt.pix.width = WIDTH;
    vfmt.fmt.pix.height = HEIGHT;
    vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT;
    if(ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
        perror("setting format failed");
        return -1;
    }

    //request buffer
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = BUFFER_COUNT;
    if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers failed");
        return -1;
    }
sleep(2);
    // mapping
    unsigned char *buffers[BUFFER_COUNT];
    unsigned int lengths[BUFFER_COUNT];
    struct v4l2_buffer mapbuffer;
    memset(&mapbuffer, 0, sizeof(mapbuffer));
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for(int i = 0; i < BUFFER_COUNT; i++){
        mapbuffer.index = i;
        sleep(1);
        if(ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0){
            printf("i = %d\n", i);
            perror("query buffer failed");
            return -1;
        }
        // mapping to userspace
        buffers[i] = (unsigned char *)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.offset);
        lengths[i] = mapbuffer.length;
   
        if (ioctl(fd,VIDIOC_QBUF,&mapbuffer)< 0){
            perror("放回失败");
        }
    }

    //capture
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0){
        perror("streamon failed");
        return -1;
    }

    //example: capture frames
    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        struct v4l2_buffer capturebuffer;
        memset(&capturebuffer, 0, sizeof(capturebuffer));
        capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
            perror("dqbuf failed");
            return -1;
        }
 
        // // 保存帧到文件
        char filename[20];
        snprintf(filename, sizeof(filename), "./output/capture%d", frame);
        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            perror("fopen failed");
            return -1;
        }
        fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
        fclose(fp);

        // 通知内核缓冲区已释放
        if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
            perror("qbuf failed");
            return -1;
        }
    }
    //stop capture
    if(ioctl(fd, VIDIOC_STREAMOFF, &type) < 0){
        perror("streamoff failed");
        return -1;
    }
    //free buffer
    for(int i = 0; i < BUFFER_COUNT; i++){
        munmap(buffers[i], lengths[i]);
    }

    //release buffer
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers failed");
        return -1;
    }


    close(fd);
    return 0;

}