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
#include <stdatomic.h>
#define BUFFER_COUNT 5
/*
Three kind of situations:1.only device 
                         2.has device and m2mfile 
                         3.ont thread has device、m2mfile and cap_dev,another thread have device and cap_dev doesn't have m2mfile
*/
static atomic_int g_stream_active = ATOMIC_VAR_INIT(1); 
static struct fdinfo {
    int fd;
    int cap_fd;
}fd_info;


typedef struct {
    const char *device;
    int width;
    int height;
    int pixelformat;
    int frame_count;
    const char *m2m_file;
    int *dma_buf;
    const char *cap;
} ThreadParams;

typedef struct {
    int fd;
    int cap_fd;
    unsigned char **buffers;
    unsigned char **buffers2;
    unsigned int *lengths;
    unsigned int *lengths2;
    int frame_count;
    const char *m2m_file;
    int size;
    const char *cap;
    int *dmabuf;
    const char *device;
} CaptureData;

int set_video_format(int fd, enum v4l2_buf_type type, int width, int height, int pixelformat) {
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = type;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;
    printf("Setting format: %d %d %d\n", width, height, pixelformat);

    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        perror("set format failed");
        return -1;
    }
    return 0;
}

int request_and_map_buffers(int fd, const char *m2m_file, enum v4l2_buf_type type,
                           int buffer_count, unsigned char **buffers, 
                           unsigned int *lengths, int *size, int *dmabuf, const char *cap) {
    struct v4l2_buffer bufferinfo;
    int fp = -1;
    if (m2m_file &&!cap) {
        char file_name[100];
        snprintf(file_name, sizeof(file_name), "%s", m2m_file);
        fp = open(file_name, O_RDONLY);
        if (fp < 0) {
            perror("Failed to open file");
            return -1;
        }
        *size = lseek(fp, 0, SEEK_END);
        lseek(fp, 0, SEEK_SET);
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = buffer_count;
    reqbuf.type = type;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if(type==V4L2_BUF_TYPE_VIDEO_OUTPUT && cap){
        reqbuf.memory = V4L2_MEMORY_DMABUF;
    }

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("request buffers failed");
        if (fp != -1) close(fp);
        return -1;
    }
    if(type==V4L2_BUF_TYPE_VIDEO_OUTPUT && cap){
        for (int i = 0; i < buffer_count; i++) {
            memset(&bufferinfo, 0, sizeof(bufferinfo));
            bufferinfo.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            bufferinfo.memory = V4L2_MEMORY_DMABUF;
            bufferinfo.index  = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
                return -1;
            }
            bufferinfo.m.fd=dmabuf[i];
        }
        return 0;
    }

    
    struct v4l2_exportbuffer expbuf;
    for (int i = 0; i < buffer_count; i++) {
        memset(&bufferinfo, 0, sizeof(bufferinfo));
        bufferinfo.type = type;
        bufferinfo.memory = V4L2_MEMORY_MMAP;
        bufferinfo.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &bufferinfo) < 0) {
            perror("query buffer failed");
            if (fp != -1) close(fp);
            return -1;
        }
       
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        expbuf.index = i;
        expbuf.plane = 0;
        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf)) {
            return -1;
        }
        dmabuf[i] = expbuf.fd;
 
        
        
        buffers[i] = (unsigned char *)mmap(NULL, bufferinfo.length, 
                                         PROT_READ | PROT_WRITE, 
                                         MAP_SHARED, fd, bufferinfo.m.offset);
        lengths[i] = bufferinfo.length;
        
        if (buffers[i] == MAP_FAILED) {
            perror("mmap buffer failed");
            if (fp != -1) close(fp);
            return -1;
        }

        if (m2m_file && !cap) {
            lseek(fp, 0, SEEK_SET);
            if (read(fp, buffers[i], *size) != *size) {
                perror("failed to read image file");
                if (fp != -1) close(fp);
                return -1;
            }
        }
        
        if (ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0) {
            perror("queue buffer failed");
            if (fp != -1) close(fp);
            return -1;
        }
    }
    
    if (fp != -1) {
        close(fp);
    }
    return 0;
}

int ensure_dir_exists(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0777) != 0) {
            perror("mkdir failed");
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", path);
        return -1;
    }
    return 0;
}
int get_frame_to_m2m_loop(CaptureData *data){
    int fd = data->fd;
    int cap_fd = data->cap_fd;
    unsigned char **buffers = data->buffers;
    unsigned int *lengths = data->lengths;
    int frame_count = data->frame_count;
    const char *cap = data->cap;
    const char *m2mfile = data->m2m_file;

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.memory = V4L2_MEMORY_MMAP; 
    fd_set fds;
    struct timeval tv;

    for (int frame = 0; frame < frame_count; frame++) {
        capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        capturebuffer.memory=V4L2_MEMORY_MMAP;
        FD_ZERO(&fds);
        FD_SET(cap_fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int ret = select(cap_fd + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            perror("select error");
            return -1;
        } else if (ret == 0) {
            perror("select timeout");
            continue;
        }
        
        if (FD_ISSET(cap_fd, &fds)) {
            if (ioctl(cap_fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
                perror("dqbuf2 failed");
                return -1;
            }
            capturebuffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            capturebuffer.memory = V4L2_MEMORY_DMABUF;
            if (ioctl(fd, VIDIOC_QUERYBUF, &capturebuffer) < 0) {
                perror("querybuf failed");
                return -1;
            }
            capturebuffer.m.fd=data->dmabuf[capturebuffer.index];
            capturebuffer.bytesused=capturebuffer.length;
            if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
                perror("qbuf failed");
                return -1;
            }

        }
    }

    return 0;
}

int capture_and_save(CaptureData *data) {
    int fd = data->fd;
    int cap_fd = data->cap_fd;
    unsigned char **buffers = data->buffers;
    unsigned int *lengths = data->lengths;
    int frame_count = data->frame_count;
    const char *cap = data->cap;
    const char *m2mfile = data->m2m_file;
   

    struct v4l2_buffer capturebuffer;
    memset(&capturebuffer, 0, sizeof(capturebuffer));
    capturebuffer.memory = V4L2_MEMORY_MMAP; 
    fd_set fds;
    struct timeval tv;

    for (int frame = 0; frame < frame_count; frame++) {
        printf("frame:%d\n",frame);
        if (!atomic_load(&g_stream_active)) {
                    break;  
            }
        printf("frame:%d\n",frame);
        capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        capturebuffer.memory=V4L2_MEMORY_MMAP;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            perror("select error");
            return -1;
        } else if (ret == 0) {
            perror("select timeout");
            continue;
        }
        
        if (FD_ISSET(fd, &fds)) {
            if (ioctl(fd, VIDIOC_DQBUF, &capturebuffer) < 0) {
                perror("dqbuf1 failed");
                return -1;
            }
            char filename[32];
            if (ensure_dir_exists("./output") != 0) {
                fprintf(stderr, "Error: failed to create output directory\n");
                return -1;
            }
            
            snprintf(filename, sizeof(filename), "./output/frame%d", frame);
            FILE *fp = fopen(filename, "wb");
            if (fp == NULL) {
                perror("fopen failed");
                return -1;
            }
            fwrite(buffers[capturebuffer.index], 1, lengths[capturebuffer.index], fp);
            fclose(fp);
  
                
            capturebuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; 
   
            
            if (ioctl(fd, VIDIOC_QBUF, &capturebuffer) < 0) {
                perror("qbuf failed1");
                return -1;
            }
        }
        
        if (data->m2m_file) {
            // if (!atomic_load(&g_stream_active)) {
            //         continue;  // 流已停止，跳过处理
            // }
            struct v4l2_buffer outputbuffer;
            memset(&outputbuffer, 0, sizeof(outputbuffer));
            outputbuffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            outputbuffer.memory = V4L2_MEMORY_MMAP;
            if(cap){
                outputbuffer.memory = V4L2_MEMORY_DMABUF;
            }
            if (ioctl(fd, VIDIOC_DQBUF, &outputbuffer) < 0) {
                perror("dqbuf2 failed");
                return -1;
            }
            if(cap){
                outputbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                outputbuffer.memory = V4L2_MEMORY_MMAP;
                ioctl(cap_fd, VIDIOC_QUERYBUF, &outputbuffer);
            }
            if(cap){
                if (ioctl(cap_fd, VIDIOC_QBUF, &outputbuffer) < 0) {
                perror("qbuf failed");
                return -1;
                }
            }
            else{
                if (ioctl(fd, VIDIOC_QBUF, &outputbuffer) < 0) {
                perror("qbuf failed3");
                return -1;
                }
                
            }
            
        }
    }
    return 0;

}

void cleanup_resources(int fd, unsigned char **buffers, unsigned int *lengths, 
                      unsigned char **buffers2, unsigned int *lengths2, 
                      int has_m2m) {
    
    // Unmap buffers
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (buffers[i] != NULL) {
            munmap(buffers[i], lengths[i]);
        }
        if (has_m2m && buffers2[i] != NULL) {
            munmap(buffers2[i], lengths2[i]);
        }
    }
    
    // Free buffers (set count to 0)
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 0;
    ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    
    if (has_m2m) {
        reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    }
    
    close(fd);
}

void* v4l2_thread_function(void *arg) {
    ThreadParams *params = (ThreadParams *)arg;
    int fd = -1;
    unsigned char *buffers[BUFFER_COUNT] = {NULL};
    unsigned int lengths[BUFFER_COUNT] = {0};
    unsigned char *buffers2[BUFFER_COUNT] = {NULL};
    unsigned int lengths2[BUFFER_COUNT] = {0};
    int *dma_buf = params->dma_buf;
    int size = 0;
    int ret = 0;
    int ret0 = 0;

    // Open device
    fd = open(params->device, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("open capture device failed");
        return (void*)-1;
    }
    if(params->cap&&!params->m2m_file){
        fd_info.cap_fd=fd;
    }
    else{
        fd_info.fd=fd;
    }

    
    // Set output format if M2M file is provided
    if (params->m2m_file) {
        if(params->cap){
            if (set_video_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, 
                            1280, 960, params->pixelformat) < 0) {
            printf("222");
            close(fd);
            return (void*)-1;
            }
        }
        else{
            if (set_video_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, 
                            1920, 1080, params->pixelformat) < 0) {
            printf("333\n");
            close(fd);
            return (void*)-1;
            }

        }
    }

    // Set capture format
    if (set_video_format(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, 
                         params->width, params->height, params->pixelformat) < 0) {
        close(fd);
        return (void*)-1;
    }

    // Request and map capture buffers
    if (request_and_map_buffers(fd, NULL, V4L2_BUF_TYPE_VIDEO_CAPTURE, 
                               BUFFER_COUNT, buffers, lengths, &size,dma_buf,params->cap) < 0) {
        cleanup_resources(fd, buffers, lengths, buffers2, lengths2, params->m2m_file != NULL);
        return (void*)-1;
    }

    // Request and map output buffers if needed
    if (params->m2m_file) {
        if (request_and_map_buffers(fd, params->m2m_file, V4L2_BUF_TYPE_VIDEO_OUTPUT, 
                                   BUFFER_COUNT, buffers2, lengths2, &size,dma_buf,params->cap) < 0) {
            cleanup_resources(fd, buffers, lengths, buffers2, lengths2, 1);
            return (void*)-1;}
    }
    
    // Start streams
    enum v4l2_buf_type type;
    if (params->m2m_file) {
        type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            perror("streamon output failed");
            cleanup_resources(fd, buffers, lengths, buffers2, lengths2, 1);
            return (void*)-1;
        }
    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon capture failed");
        cleanup_resources(fd, buffers, lengths, buffers2, lengths2, params->m2m_file != NULL);
        return (void*)-1;
    }
    
    // Prepare capture data
    CaptureData cap_data = {
        .fd = fd_info.fd,
        .cap_fd = fd_info.cap_fd,
        .buffers = buffers,
        .buffers2 = buffers2,
        .lengths = lengths,
        .lengths2 = lengths2,
        .frame_count = params->frame_count,
        .m2m_file = params->m2m_file,
        .size = size,
        .cap = params->cap,
        .dmabuf = dma_buf,
        .device = params->device
    };
    
    // Perform capture
    if(params->cap&&!params->m2m_file){
        ret0=get_frame_to_m2m_loop(&cap_data);
    }
    else{
    ret = capture_and_save(&cap_data);
    }

    // Stop streams
    atomic_store(&g_stream_active, 0);
    usleep(100000);  
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);

    if (params->m2m_file) {
        type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    // Cleanup
    cleanup_resources(fd, buffers, lengths, buffers2, lengths2, params->m2m_file != NULL);
    
    return (void*)(intptr_t)ret;
}

int main(int argc, char **argv) {
    ThreadParams cap_params = {0};
    ThreadParams out_params = {0};
    // int dma_buf[BUFFER_COUNT] = {0};
    int dma_buf[BUFFER_COUNT] = {-1}; 

    cap_params.width = 1280;
    cap_params.height = 960;
    cap_params.pixelformat = v4l2_fourcc('N', 'V', '1', '2');

    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"pixelformat", required_argument, 0, 'p'},
        {"frame_count", required_argument, 0, 'f'},
        {"m2m_file", required_argument, 0, 'm'},
        {"cap_dev", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "d:w:h:p:f:m:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                out_params.device = optarg;
                break;
            case 'w':
                out_params.width = strtol(optarg, NULL, 10);
                break;
            case 'h':
                out_params.height = strtol(optarg, NULL, 10);
                break;
            case 'p':
                if (strlen(optarg) == 4) {
                    out_params.pixelformat = v4l2_fourcc(optarg[0], optarg[1], optarg[2], optarg[3]);
                } else {
                    fprintf(stderr, "Invalid pixel format\n");
                    return -1;
                }
                break;
            case 'f':
                cap_params.frame_count =out_params.frame_count= strtol(optarg, NULL, 10);
                break;
            case 'm':
                out_params.m2m_file = optarg;
                break;
            case 'c':
                out_params.cap=cap_params.cap = cap_params.device = optarg;
                out_params.m2m_file=cap_params.cap;
                break;
            default:
                fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>] [--m2m_file=<file> | -m <file>]\n", argv[0]);
                return -1;
        }
    }

    if (!out_params.device || out_params.width <= 0 || out_params.height <= 0 || 
        out_params.pixelformat == 0 || out_params.frame_count <= 0) {
        fprintf(stderr, "Usage: %s [--device=<device> | -d <device>] [--width=<width> | -w <width>] [--height=<height> | -h <height>] [--pixelformat=<pixelformat> | -p <pixelformat>] [--frame_count=<frame_count> | -f <frame_count>] [--m2m_file=<file> | -m <file>]\n", argv[0]);
        return -1;
    }

    cap_params.dma_buf = dma_buf;
    out_params.dma_buf = dma_buf;
    // printf("cap_params.cap: %s,cap_prarams.device:%s\n",cap_params.cap,cap_params.device);
    // printf("out_params.cap: %s,out_prarams.device:%s\n",out_params.cap,out_params.device);
    pthread_t thread,cap_thread;
    if(cap_params.cap && !cap_params.m2m_file){
        if(pthread_create(&cap_thread, NULL, v4l2_thread_function, &cap_params) != 0){
            perror("pthread_create failed");
            return -1;
            }
    }

    if (pthread_create(&thread, NULL, v4l2_thread_function, &out_params) != 0) {
        perror("pthread_create failed");
        return -1;
    }

    void *out_ret, *cap_ret;
    if (cap_params.cap)
    {
        pthread_join(cap_thread, &cap_ret);
    }
    pthread_join(thread, &out_ret);
    
    if ((intptr_t)out_ret != 0) {
        fprintf(stderr, "Thread execution failed1\n");
        return -1;
    }
    if(cap_params.cap){
        if ((intptr_t)cap_ret != 0) {
            fprintf(stderr, "Thread execution failed\n");
            return -1;
        }
    }
    
    return 0;
}