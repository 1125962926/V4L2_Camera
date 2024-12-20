/*
 * @Author: Li RF
 * @Date: 2024-11-14 14:15:55
 * @LastEditors: Li RF
 * @LastEditTime: 2024-11-15 12:06:05
 * @Description: 
 * Email: 1125962926@qq.com
 * Copyright (c) 2024 Li RF, All Rights Reserved.
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>

#define FRAME_NUM 4
#define IMAGE_NAME "image"
#define OUTPUT_DIR "output"

typedef struct	//帧缓冲区
{
	void* start;
	unsigned int length;
}Buffer;

// 用户空间内存缓冲区
Buffer* buffers;

//摄像头设备文件名
int fd;


/**
 * @Description: 查看设备信息
 * @return {*}
 */
void get_capacity(void)
{
	struct v4l2_capability cap;
	ioctl(fd, VIDIOC_QUERYCAP, &cap);
	printf("\tDriverName:\t%s\n", cap.driver);
	printf("\tCard Name:\t%s\n", cap.card);
	printf("\tBus info:\t%s\n", cap.bus_info);
	printf("\tDriverVersion:\t%u.%u.%u\n", (cap.version >> 16) & 0xff, (cap.version>>8) & 0xff, cap.version & 0xff);

	printf("\n\tCapabilities:\n");
	if(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
		printf("\tDevice supports video capture\n");
	if(cap.capabilities & V4L2_CAP_STREAMING)
		printf("\tDevice supports streaming\n");
	if(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)
		printf("\tDevice supports video output\n");
	if(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)
		printf("\tDevice supports video overlay\n");
	if(cap.capabilities & V4L2_CAP_READWRITE)
		printf("\tDevice supports read write\n");
}

/**
 * @Description: 枚举摄像头所支持的所有视频采集帧率
 * @param {__u32} pixel_format: 像素格式
 * @param {__u32} width: 宽
 * @param {__u32} height: 高
 * @return {*}
 */
void get_fps(__u32 pixel_format, __u32 width, __u32 height)
{
	struct v4l2_frmivalenum frmival;
	frmival.index = 0;
	frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	frmival.pixel_format = pixel_format;
	frmival.width = width;
	frmival.height = height;
	while (0 == ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival))
	{
		printf("\t\t rate <%dfps>\n", frmival.discrete.denominator);
		// printf("denominator: %d, numerator: %d\n", frmival.discrete.denominator, frmival.discrete.numerator);
		frmival.index++;
	}
}

/**
 * @Description: 枚举摄像头所支持的所有视频采集分辨率
 * @param {__u32} pixel_format: 像素格式
 * @return {*}
 */
void get_frame_size(__u32 pixel_format)
{
	struct v4l2_frmsizeenum frmsize;
	frmsize.index = 0;
	frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	frmsize.pixel_format = pixel_format;
	while (0 == ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize))
	{
		printf("\t\t size <width: %d * height: %d>\n", frmsize.discrete.width, frmsize.discrete.height);
		// 获取该像素对应的帧率
		get_fps(pixel_format, frmsize.discrete.width, frmsize.discrete.height);
		frmsize.index++;
		printf("\n");
	}
}

/**
 * @Description: 枚举设备支持的数据格式
 * @return {int}
 */
int get_fmtdesc(void)
{
	struct v4l2_fmtdesc v4fmt;
	v4fmt.index = 0;
	v4fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while(0 == ioctl(fd, VIDIOC_ENUM_FMT, &v4fmt))
	{
		// printf("index = %d\n", v4fmt.index);
		// printf("flags = %d\n", v4fmt.flags);
		printf("\tdescription = %s\n", v4fmt.description);

		unsigned char *p = (unsigned char *)&v4fmt.pixelformat;
		printf("\tpixelformat = %c%c%c%c\n", p[0], p[1], p[2], p[3]);
		// printf("reserved = %d\n", v4fmt.reserved[0]);	

		// 获取该采集格式对应的分辨率
		get_frame_size(v4fmt.pixelformat);
		printf("\n");
		v4fmt.index++;
	}
	if(v4fmt.index == 0)
		return -1;
	return 0;

}

/**
 * @Description: 设置帧格式
 * @param {__u32} pixel_format: 像素格式
 * @param {__u32} width: 宽
 * @param {__u32} height: 高
 * @return {int}
 */
int set_format(__u32 pixel_format, __u32 width, __u32 height)
{
    struct v4l2_format format;
	memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;// 设置摄像头数据的宽和高

	// 设置视频采集格式    V4L2_PIX_FMT_YUYV     V4L2_PIX_FMT_MJPEG
    format.fmt.pix.pixelformat = pixel_format;
	// 根据硬件能力和请求的图像尺寸自动选择最合适的场类型
    format.fmt.pix.field = V4L2_FIELD_ANY;
    int ret = ioctl(fd, VIDIOC_S_FMT, &format);
    if(ret < 0)
		return -1;
	return 0;
}

/**
 * @Description: 获取当前帧格式
 * @return {int}
 */
int get_format(void)
{
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if(ret < 0)
    {
        printf("VIDIOC_G_FMT fail\n");
        return -1;
    }
	printf("\twidth:%d, height:%d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
	printf("\tpixelformat:\t%c%c%c%c\n", fmt.fmt.pix.pixelformat & 0xff, (fmt.fmt.pix.pixelformat >> 8) & 0xff, (fmt.fmt.pix.pixelformat >> 16) & 0xff, (fmt.fmt.pix.pixelformat >> 24) & 0xff);
	return 0;
}

/**
 * @Description: 设置帧率
 * @param {__u32} fps: 帧率
 * @return {int}
 */
int set_fps(__u32 fps)
{
	struct v4l2_streamparm streamparm;
	memset(&streamparm, 0, sizeof(struct v4l2_streamparm));
	streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ioctl(fd, VIDIOC_G_PARM, &streamparm);
	// 检查是否支持设置帧率
	if(V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability)
	{
		streamparm.parm.capture.timeperframe.numerator = 1;
		streamparm.parm.capture.timeperframe.denominator = fps;
		// 设置帧率
		if (0 != ioctl(fd, VIDIOC_S_PARM, &streamparm)) {
			printf("Set frame rate failed\n");
			return -1;
		}
		printf("Set frame rate to %d fps\n", fps);
	}
	else
	{
	    printf("This device does not support setting frame rate\n");
		return -1;
	}
	return 0;
}

/**
 * @Description: 检查是否支持高品质成像模式
 * @return {*}
 */
void chech_high_quality(void)
{
    struct v4l2_streamparm streamparm;
	streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	ioctl(fd, VIDIOC_G_PARM, &streamparm);

	if (V4L2_MODE_HIGHQUALITY & streamparm.parm.capture.capability)
		printf("Support high-quality imaging mode\n");
	else
		printf("This device does not support high-quality imaging mode\n");
}

/**
 * @Description: 申请内存缓冲区
 * @return {int}
 */
int requestbuffers(void)
{
	struct v4l2_requestbuffers req;

	req.count = FRAME_NUM; //帧缓冲数量
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	int ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	
	return ret;
}

/**
 * @Description: 查询缓存信息，然后再使用缓存信息进行映射
 * @return {int}
 */
int query_mmap_buf(void)
{
	struct v4l2_buffer buf;
	for (unsigned int n_buffers = 0; n_buffers < FRAME_NUM; ++n_buffers)
	{
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		// 获取内存缓冲区的帧
		if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buf))
		{
			fprintf(stderr, "VIDIOC_QUERYBUF error/n");
			return -1;
		}
		buffers[n_buffers].length = buf.length;

		// 将内存缓冲区的帧与用户空间内存进行映射
		buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		if (MAP_FAILED == buffers[n_buffers].start)
		{
			printf("index: %d mmap failed/n", n_buffers);
			return -1;
		}
		// 放入摄像头帧采集入队队列
		if(0 > ioctl(fd, VIDIOC_QBUF, &buf))
		{
			printf("index: %d VIDIOC_QBUF error/n", n_buffers);
		}
	}
	return 0;
}

/**
 * @Description: 取消映射
 * @return {*}
 */
void unmap_buf(void)
{
	if(buffers)
    for(int i = 0; i < FRAME_NUM; ++i)
    	munmap(buffers[i].start, buffers[i].length);
}

/**
 * @Description: 开启视频流
 * @return {int}
 */
int stream_on(void)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 != ioctl(fd, VIDIOC_STREAMON, &type))
	{
		printf("ERR(%s):VIDIOC_STREAM ON failed\n", __func__);
		return -1;
	}
	return 0;
}

/**
 * @Description: 关闭视频流
 * @return {*}
 */
int stream_off(void)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 != ioctl(fd, VIDIOC_STREAMOFF, &type))
	{
		printf("ERR(%s):VIDIOC_STREAM OFF failed\n", __func__);
		return -1;
	}
	return 0;
}

/**
 * @Description: 保存为文件
 * @param {int} index: 当前帧
 * @param {int} format: 数据格式
 * @param {Buffer} buf: 缓冲区
 * @return {int}
 */
int process_image(int index, int format, Buffer buf)
{
	printf("\n**** image: %d ****\n", index);

	struct stat st = {0};
	if (stat(OUTPUT_DIR, &st) == -1) {
        // 尝试创建目录
        if (mkdir(OUTPUT_DIR, 0777) == -1) {
            perror("mkdir failed");
            return -1;
        }
    }

	char filename[1024];
	char file_end[5];
	switch (format)
	{
	case 0://YUYV
		sprintf(file_end, "yuv");
		break;
	case 1://MJPEG
		sprintf(file_end, "jpg");
		break;
	default:
		break;
	}

	sprintf(filename, "%s/%s_%d.%s", OUTPUT_DIR, IMAGE_NAME, index, file_end);

	// 写入未经编码的二进制数据，确保数据的完整性
	FILE *file = fopen(filename, "wb");

	//将采集到的数据写入文件
	fwrite(buf.start, buf.length, 1, file);

	fclose(file);
	return 0;
}

/**
 * @Description: 信号结束程序
 * @param {int} signal: 处理的信号
 * @return {*}
 */
void exit_handler(int signal)
{
    if (buffers)
    {
		printf("程序即将退出\n");
		unmap_buf();
		free(buffers);
		buffers = NULL;
		close(fd);
    }
    exit(signal);
}


int main(int argc, char **argv)
{
	printf("\n****************** USB Camera Application ********************\n\n");
	if (argc != 6) {
		printf("Usage: %s <width> <height> <rate: 25 30> <0:YUYV, 1:MJPEG> <image num>\n\n", argv[0]);
        return -1;
    }

	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	int rate = atoi(argv[3]);
	int format = atoi(argv[4]);
	int image_num = atoi(argv[5]);

	__u32 pixel_format;
	if(format == 0)
		pixel_format = V4L2_PIX_FMT_YUYV;
	else if(format == 1)
		pixel_format = V4L2_PIX_FMT_MJPEG;
	else
	{
		printf("Invalid format: <0:YUYV, 1:MJPEG>\n");
		return -1;
	}

	// 设置退出信号
	signal(SIGINT, exit_handler);

	int ret;
	// 打开设备
	fd = open("/dev/video0", O_RDWR);
	if(fd < 0)
	{
		perror("打开设备失败");
		return -1;
	}

	// 获取设备信息
	printf("设备信息:\n");
	get_capacity();

	// 显示支持的帧格式
	printf("\n支持的帧格式:\n");
	ret = get_fmtdesc();
	if(ret < 0)
	{
		perror("获取帧格式失败");
		close(fd);
		return -1;
	}
	
	// 设置帧格式
	printf("正在设置帧格式...\n");
	ret = set_format(pixel_format, width, height);
	if(ret < 0)
	{
		perror("设置帧格式失败");
		close(fd);
		return -1;
	}

	// 设置帧率
	printf("\n正在设置帧率...\n");
	ret = set_fps(25);
	if(ret < 0)
	{
		perror("设置帧率失败");
		// close(fd);
		// return -1;
	}

	// 检查是否支持高品质成像模式
	printf("\n检查是否支持高品质成像模式...\n");
	chech_high_quality();

	// 确认当前帧格式
	printf("\n当前帧格式:\n");
	ret = get_format();
	if(ret < 0)
	{
		perror("获取帧格式失败");
		close(fd);
		return -1;
	}

	// 申请内存缓冲区
	printf("正在申请内存缓冲区...\n");
	ret = requestbuffers();
	if(ret < 0)
	{
		perror("申请内存缓冲区失败");
		close(fd);
		return -1;
	}
	
	// 申请用户空间缓冲区内存
	buffers = (Buffer*)calloc(FRAME_NUM, sizeof(Buffer));
	if (!buffers) 
	{
		fprintf(stderr, "Out of memory/n");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// 将内存映射到用户空间
	printf("\n将内存映射到用户空间...\n");
	ret = query_mmap_buf();
	if(ret < 0)
	{
		perror("将内存映射到用户空间失败");
		free(buffers);
		close(fd);
		return -1;
	}

	//开始视频流的采集
	printf("\n开始视频流的采集...\n");
	ret = stream_on();
	if(ret < 0)
	{
		perror("开始视频流的采集失败");
		unmap_buf();
		free(buffers);
		close(fd);
		return -1;
	}
	
	struct pollfd poll_fds[1];
	poll_fds[0].fd = fd;
	poll_fds[0].events = POLLIN; //等待可读
	
	struct v4l2_buffer buf;
	for(int i = 1; i <= image_num; i++)
	{
		memset(&buf,0,sizeof(buf));
		buf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory =V4L2_MEMORY_MMAP;

		// 等待设备上的可读事件
		if (poll(poll_fds, 1, 10000) == -1) {
			printf("ERR(%s):poll 失败\n", __func__);
			break;
		}

		// 检查是否有可读事件
		if(!(poll_fds[0].revents & POLLIN))
			continue;

		//取出缓冲帧
		if (0 != ioctl(fd, VIDIOC_DQBUF, &buf))
		{
			printf("ERR(%s):提取数据失败\n", __func__);
			break;
		}

		// 图像处理
		ret = process_image(i, format, buffers[buf.index]);
		if(ret < 0)
		{
			printf("ERR(%s):图像处理失败\n", __func__);
			break;
		}

		//放入缓冲帧
		if (0 != ioctl(fd, VIDIOC_QBUF, &buf))
		{
			printf("ERR(%s):放入缓冲帧失败\n", __func__);
			break;
		}
		
	}
		
	//关闭视频流
	printf("\n关闭视频流...\n");
	stream_off();
	
	//资源释放
	unmap_buf();
	free(buffers);
	close(fd);
	printf("\n****************** Successfully, the execution ends ********************\n\n");
	return 0;
}

