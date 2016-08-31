#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "media.h"
#include "msm_cam_sensor.h"

uint8_t *buffer;

#define QCAMERA_VNODE_GROUP_ID			2
#define MSM_CAMERA_SUBDEV_SENSOR_INIT 	14
#define MSM_CAMERA_EVENT_MIN			0
#define MSM_CAMERA_EVENT_MAX			8
#define MSM_CAMERA_V4L2_EVENT_TYPE (V4L2_EVENT_PRIVATE_START + 0x00002000)

// generics
#define DEF_NODE "/dev/video1"
#define MAX_DEV_NAME_SIZE 32


static int xioctl(int fd, int request, void *arg)
{
        int r;

        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);

        return r;
}

int print_caps(int fd)
{
        struct v4l2_capability caps = {};
        if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps))
        {
                perror("Querying Capabilities");
                return 1;
        }

        printf( "Driver Caps:\n"
                "  Driver: \"%s\"\n"
                "  Card: \"%s\"\n"
                "  Bus: \"%s\"\n"
                "  Version: %d.%d\n"
                "  Capabilities: %08x\n",
                caps.driver,
                caps.card,
                caps.bus_info,
                (caps.version>>16)&&0xff,
                (caps.version>>24)&&0xff,
                caps.capabilities);


        struct v4l2_cropcap cropcap = {0};
        cropcap.type = V4L2_CAP_VIDEO_CAPTURE_MPLANE;
        if (-1 == xioctl (fd, VIDIOC_SUBSCRIBE_EVENT, &cropcap))
        {
                perror("Querying Cropping Capabilities");
                return 1;
        }
        else
        	printf("Starting the video subscrive event\n");

        int support_grbg10 = 0;

        struct v4l2_fmtdesc fmtdesc = {0};
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        char fourcc[5] = {0};
        char c, d, e, f;
        printf("FMT : CE Desc\n--------------------\n");
        while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
        {
	           printf("%s\n", fmtdesc.description);
               strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
                if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264)
                    support_grbg10 = 1;
                c = fmtdesc.flags & 1? 'H' : ' ';
                d = fmtdesc.flags & 2? '2' : ' ';
    			e = fmtdesc.flags & 3? '6' : ' ';
	            f = fmtdesc.flags & 4? '4' : ' ';
                printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
                fmtdesc.index++;
        }

        struct v4l2_format fmt = {0};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 752;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SGRBG10;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        {
            perror("Setting Pixel Format");
            return 1;
        }

        strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
        printf( "Selected Camera Mode:\n"
                "  Width: %d\n"
                "  Height: %d\n"
                "  PixFmt: %s\n"
                "  Field: %d\n",
                fmt.fmt.pix.width,
                fmt.fmt.pix.height,
                fourcc,
                fmt.fmt.pix.field);
        return 0;
}

int init_mmap(int fd)
{
    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
        perror("Requesting Buffer");
        return 1;
    }

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
    {
        perror("Querying Buffer");
        return 1;
    }

    buffer = mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    printf("Length: %d\nAddress: %p\n", buf.length, buffer);
    printf("Image Length: %d\n", buf.bytesused);

    return 0;
}

int capture_image(int fd)
{
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    {
        perror("Query Buffer");
        return 1;
    }

    if(-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type))
    {
        perror("Start Capture");
        return 1;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {0};
    tv.tv_sec = 2;
    int r = select(fd+1, &fds, NULL, NULL, &tv);
    if(-1 == r)
    {
        perror("Waiting for Frame");
        return 1;
    }

    if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
    {
        perror("Retrieving Frame");
        return 1;
    }

    int outfd = open("out.img", O_RDWR);
    write(outfd, buffer, buf.bytesused);
    close(outfd);

    return 0;
}



int probe_media(char *node_name)
{
	char dev_name[MAX_DEV_NAME_SIZE];
	int dev_fd = 0;
	int rc = 0;
	struct media_device_info mdev_info;
	struct media_entity_desc entity;
	int num_entities = 0;
	int num_media_devices = 0;

	while (1) 
	{
		printf("num_media_devices : %d \n", num_media_devices);
		snprintf(dev_name, sizeof(dev_name), "/dev/media%d", num_media_devices++);
		dev_fd = open(dev_name, O_RDWR | O_NONBLOCK);
		if (dev_fd == -1)
		{
		    printf("Opening video device failed : %s\n", dev_name);		
			return 1;
		}
		
		rc = ioctl(dev_fd, MEDIA_IOC_DEVICE_INFO, &mdev_info);
		if (rc < 0) 
		{
		  close(dev_fd);
		  return 1;
		}
		
		if (strncmp(mdev_info.model, "msm_config", sizeof(mdev_info.model)) != 0)
		{
			printf("msm_config is not matching\n");
			continue;
		}
		else
		{
			printf("msm_config is matching with : %s from %s \n", mdev_info.model, dev_name);
		}
	
		num_entities = 1;
		while (1) 
		{
			memset(&entity, 0, sizeof(entity));
			entity.id = num_entities++;
			rc = ioctl(dev_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
			if (rc < 0) 
			{
				rc = 0;
				break;
			}
		
		if (entity.type == MEDIA_ENT_T_DEVNODE_V4L &&
		      entity.group_id == QCAMERA_VNODE_GROUP_ID) 
			{
				/* found the video device */
				strncpy(node_name, entity.name, MAX_DEV_NAME_SIZE);
				printf("Copied the %s node from entity\n", node_name);
				close(dev_fd);
				return 0;
			}
		}
	}
	return 0;
}

int probe_subdev(char *node_name)
{
	char dev_name[MAX_DEV_NAME_SIZE];
	int dev_fd = 0;
	int rc = 0;
	struct media_device_info mdev_info;
	struct media_entity_desc entity;
	int num_entities = 0;
	int num_media_devices = 0;

	while (1) 
	{
		printf("num_media_devices : %d \n", num_media_devices);
		snprintf(dev_name, sizeof(dev_name), "/dev/media%d", num_media_devices++);
		dev_fd = open(dev_name, O_RDWR | O_NONBLOCK);
		if (dev_fd == -1)
		{
		    printf("Opening video device failed : %s\n", dev_name);		
			return 1;
		}
		
		rc = ioctl(dev_fd, MEDIA_IOC_DEVICE_INFO, &mdev_info);
		if (rc < 0) 
		{
		  close(dev_fd);
		  return 1;
		}
		
		if (strncmp(mdev_info.model, "msm_config", sizeof(mdev_info.model)) != 0)
		{
			printf("msm_config is not matching\n");
			continue;
		}
		else
		{
			printf("msm_config is matching with : %s from %s \n", mdev_info.model, dev_name);
		}
	
		num_entities = 1;
		while (1) 
		{
			memset(&entity, 0, sizeof(entity));
			entity.id = num_entities++;
			rc = ioctl(dev_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
			if (rc < 0) 
			{
				rc = 0;
				break;
			}
		
			printf("entity name %s type %d group id %d\n",entity.name, entity.type, entity.group_id);
		if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV &&
		      entity.group_id == MSM_CAMERA_SUBDEV_SENSOR_INIT) 
			{
				/* found the video device */
				strncpy(node_name, entity.name, MAX_DEV_NAME_SIZE);
				printf("Copied the %s node from entity\n", node_name);
				close(dev_fd);
				return 0;
			}
		}
	}
	return 0;
}

int subscription(int fd)
{
	struct v4l2_event_subscription subscribe;
	int i = 0;
	
	// starting to the process
	memset(&subscribe, 0, sizeof(struct v4l2_event_subscription));
	
	subscribe.type = MSM_CAMERA_V4L2_EVENT_TYPE;
	for (i = MSM_CAMERA_EVENT_MIN + 1; i < MSM_CAMERA_EVENT_MAX; i++) 
	{
		subscribe.id = i;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &subscribe) < 0)
			return -1;
	}
	return 0;
}

int open_subdev_cam(int fd)
{
 	struct sensor_init_cfg_data cfg;
 	cfg.cfgtype = 1;
 	if (-1 == xioctl(fd, VIDIOC_MSM_SENSOR_INIT_CFG, &cfg))
 	{
 		printf("Probe failed\n");
 		return -1;
 	}	
 	else
 		printf("Waiting the camera to start ... \n");
	return 0;
}

int main( int argc, char * argv [] ) 
{
        int fd;
        int probe_done_fd;
		char node_name[MAX_DEV_NAME_SIZE];
		char probed_node[MAX_DEV_NAME_SIZE];
		char dev_name[MAX_DEV_NAME_SIZE];
		
		memset(probed_node, '\0', MAX_DEV_NAME_SIZE);
		probe_media(probed_node);
			
		memset(node_name, '\0', MAX_DEV_NAME_SIZE);
		if ( NULL == argv[1] )
		{
			memset(dev_name, '\0', MAX_DEV_NAME_SIZE);
			printf("Probed node is : %s\n", probed_node);
			snprintf(dev_name, sizeof(dev_name), "/dev/%s", probed_node);
			strncpy(node_name, dev_name, sizeof(dev_name));
			printf("Copied dev node is : %s\n", node_name);
		}
		else
			strncpy(node_name, argv[1], strlen(argv[1]));

		printf("dev node is : %s\n", node_name);
		
        fd = open(node_name, O_RDWR);
        if (fd == -1)
        {
                perror("Opening video device failed\n");
                return 1;
        }
        else
        	printf("Device %s opened\n", node_name);

		if(subscription(fd))
            return 1;

		memset(probed_node, '\0', MAX_DEV_NAME_SIZE);
		probe_subdev(probed_node);
		snprintf(dev_name, sizeof(dev_name), "/dev/%s", probed_node);
		fd = open(dev_name, O_RDWR);
        if (probe_done_fd == -1)
        {
			perror("Opening video subdev failed\n");
			return 1;
        }
        else
        	printf("Subdev %s opened\n", dev_name);

		if (open_subdev_cam(probe_done_fd) == -1)
			return 1;
			
        if(print_caps(fd))
            return 1;

        if(init_mmap(fd))
            return 1;

        if(capture_image(fd))
            return 1;

        close(fd);
        return 0;
}
