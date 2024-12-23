#include <stdio.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>    
#include <sys/mman.h>
int main(){
    //open device
    int fd = open("/dev/video0", O_RDWR);
    if(fd < 0){
        perror("open failed");
        return -1;
    }

    //setting format
    struct v4l2_format vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vfmt.fmt.pix.width = 320;
    vfmt.fmt.pix.height = 180;
    vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if(ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
        perror("setting format failed");
        return -1;
    }

    //request buffer
    struct v4l2_requestbuffers reqbuf;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 4;
    if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("requesting buffers failed");
        return -1;
    }

    // mapping
    unsigned char *buffers[4];
    unsigned int lengths[4];
    struct v4l2_buffer mapbuffer;
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for(int i = 0; i < reqbuf.count; i++){
        mapbuffer.index = i;
        if(ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0){
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
    //example: capture 1 frames
    struct v4l2_buffer capturebuffer;
    capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd, VIDIOC_DQBUF, &capturebuffer)  < 0){
        perror("dqbuf failed");
        return -1;
    }
    FILE *fp = fopen("capture.yuyv", "wb");
    if(fp == NULL){
        perror("fopen failed");
        return -1;
    }
    fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
    fclose(fp);

    //inform  the kernel that the buffer is free
    // if(ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0){
    //     perror("qbuf failed");
    //     return -1;
    // }
    //stop capture
    if(ioctl(fd, VIDIOC_STREAMOFF, &type) < 0){
        perror("streamoff failed");
        return -1;
    }
    //free buffer
    for(int i = 0; i < reqbuf.count; i++){
        munmap(buffers[i], lengths[i]);
    }
    close(fd);
    return 0;

}