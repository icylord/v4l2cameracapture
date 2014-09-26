#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>           
#include <fcntl.h>            
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define CAMERA_DEV "/dev/video0"

#define IMAGE_HEIGHT 720
#define IMAGE_WIDTH 1280
#define VIDEO_FORMAT V4L2_PIX_FMT_MJPEG
#define BUFFER_COUNT 4

typedef struct VideoBuffer {
    void   *start;
    size_t  length;
} VideoBuffer;

VideoBuffer framebuf[BUFFER_COUNT]; 


int main()
{
	int i, ret;

	// open device
	int cam_fd;
	cam_fd = open(CAMERA_DEV, O_RDWR, 0);
	if (cam_fd < 0) {
		printf("Open camera device %s error!\n", CAMERA_DEV);
		exit(-1);
	}

	// query device driver information
	struct v4l2_capability cap;
	ret = ioctl(cam_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("VIDIOC_QUERYCAP failed (%d)\n", ret);
        exit(-1);
	}

	// Print capability infomations
    printf("Capability Informations:\n");
    printf(" driver: %s\n", cap.driver);
    printf(" card: %s\n", cap.card);
    printf(" bus_info: %s\n", cap.bus_info);
    printf(" version: %08X\n", cap.version);
    printf(" capabilities: %08X\n", cap.capabilities);

   	// setting video format
   	struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = IMAGE_WIDTH;
    fmt.fmt.pix.height      = IMAGE_HEIGHT;
    fmt.fmt.pix.pixelformat = VIDEO_FORMAT;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    ret = ioctl(cam_fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        printf("VIDIOC_S_FMT failed (%d)\n", ret);
        exit(-1);
    }

    // Print Stream Format
    printf("Stream Format Informations:\n");
    printf(" type: %d\n", fmt.type);
    printf(" width: %d\n", fmt.fmt.pix.width);
    printf(" height: %d\n", fmt.fmt.pix.height);
    char fmtstr[8];
    memset(fmtstr, 0, 8);
    memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
    printf(" pixelformat: %s\n", fmtstr);
    printf(" field: %d\n", fmt.fmt.pix.field);
    printf(" bytesperline: %d\n", fmt.fmt.pix.bytesperline);
    printf(" sizeimage: %d\n", fmt.fmt.pix.sizeimage);
    printf(" colorspace: %d\n", fmt.fmt.pix.colorspace);
    printf(" priv: %d\n", fmt.fmt.pix.priv);
    printf(" raw_date: %s\n", fmt.fmt.raw_data);

    // query fps
    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(struct v4l2_streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(cam_fd, VIDIOC_G_PARM, &streamparm) == -1)
    {
        printf(" get stream parameters failed");
        exit(-1);
    }

    printf(" current fps:\t%u frames per %u second\n",
            streamparm.parm.capture.timeperframe.denominator,
            streamparm.parm.capture.timeperframe.numerator);

    if ((streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == V4L2_CAP_TIMEPERFRAME)
    {
        printf(" capabilities:\t support pragrammable frame rates\n");
    }


    // Alloc buffer memory
    struct v4l2_requestbuffers reqbuf;
    
    reqbuf.count = BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    
    ret = ioctl(cam_fd, VIDIOC_REQBUFS, &reqbuf);
    if(ret < 0) {
        printf("VIDIOC_REQBUFS failed (%d)\n", ret);
        exit(-1);
    }

    
    VideoBuffer* buffers = calloc( reqbuf.count, sizeof(*buffers) );
    struct v4l2_buffer buf;

    for (i = 0; i < reqbuf.count; i++) {
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(cam_fd , VIDIOC_QUERYBUF, &buf);
        if(ret < 0) {
            printf("VIDIOC_QUERYBUF (%d) failed (%d)\n", i, ret);
            return ret;
        }

        // mmap buffer
        framebuf[i].length = buf.length;
        framebuf[i].start = (char *) mmap(0, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, cam_fd, buf.m.offset);
        if (framebuf[i].start == MAP_FAILED) {
            printf("mmap (%d) failed: %s\n", i, strerror(errno));
            return -1;
        }
    
        // Queen buffer
        ret = ioctl(cam_fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            printf("VIDIOC_QBUF (%d) failed (%d)\n", i, ret);
            return -1;
        }

        printf("Frame buffer %d: address=0x%x, length=%d\n", i, (unsigned int)framebuf[i].start, framebuf[i].length);
    }

    // start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(cam_fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        printf("VIDIOC_STREAMON failed (%d)\n", ret);
        exit(-1);
    }

    for (i = 0; i < 5; i++) {
    	// Get frame
    	ret = ioctl(cam_fd, VIDIOC_DQBUF, &buf);
    	if (ret < 0) {
        	printf("VIDIOC_DQBUF failed (%d)\n", ret);
        	exit(-1);
    	}

    	char filename[64];
    	sprintf(filename, "%d.jpg", i);
    	FILE *fp = fopen(filename, "wb");
    	if (fp < 0) {
        	printf("open frame data file failed\n");
        	return -1;
    	}
    	fwrite(framebuf[buf.index].start, 1, buf.bytesused, fp);
    	fclose(fp);
    	printf("Capture one frame saved in %s\n", filename);

    	// requeue buffer
    	ret = ioctl(cam_fd, VIDIOC_QBUF, &buf);
    	if (ret < 0) {
        	printf("VIDIOC_QBUF failed (%d)\n", ret);
        	exit(-1);
        }
    }

    // stop streaming
    ret = ioctl(cam_fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        printf("VIDIOC_STREAMON failed (%d)\n", ret);
        exit(-1);
    }

    // Release the resource
    for (i = 0; i < BUFFER_COUNT; i++) 
    {
        munmap(framebuf[i].start, framebuf[i].length);
    }

    close(cam_fd);
    printf("Camera test Done.\n");
    return 0;
}
