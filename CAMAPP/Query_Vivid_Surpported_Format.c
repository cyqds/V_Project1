#include <stdio.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(){
    int fd = open("/dev/video0", O_RDWR);
    if(fd < 0){
        perror("open failed");
        return -1;
    }

    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index =0;
    while(1){
        
        if(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0){
            break;  
        }
        printf("index: %d\n", fmtdesc.index);
        printf("description: %s\n", fmtdesc.description);
               printf("pixel format: %c%c%c%c\n", 
               (fmtdesc.pixelformat >>  0) & 0xff,
               (fmtdesc.pixelformat >>  8) & 0xff,
               (fmtdesc.pixelformat >> 16) & 0xff,
               (fmtdesc.pixelformat >> 24) & 0xff);
        printf("flags: %d\n", fmtdesc.flags);
        printf("reserved: %d\n", fmtdesc.reserved[0]);
        struct v4l2_frmsizeenum frmsize;
        frmsize.index = 0;
        frmsize.pixel_format = fmtdesc.pixelformat;
        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf(" Surpported Resolution: %dx%d\n", frmsize.discrete.width, frmsize.discrete.height);
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                       frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                // Handles stepped or continuous resolution ranges (here only discrete resolutions are handled as an example)
            }
            frmsize.index++;
        }


       fmtdesc.index++;

    }
    close(fd);
    return 0;





}
