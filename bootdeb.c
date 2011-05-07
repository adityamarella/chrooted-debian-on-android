#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/loop.h>
#include <fcntl.h>
#include <string.h>
#include <linux/fs.h>

#ifdef HAVE_ANDROID 
    #include <android/log.h>
    #define LI(...) __android_log_print(ANDROID_LOG_INFO, "BOOTDEB", __VA_ARGS__)
    #define LE(...) __android_log_print(ANDROID_LOG_ERROR, "BOOTDEB", __VA_ARGS__)
    //#define LI(...) fprintf(stdout,  __VA_ARGS__)
    //#define LE(...) fprintf(stderr,  __VA_ARGS__)
    #define LOOPDEVICE "/dev/block/loop50"
    #define IMGPATH "/mnt/sdcard/debian/squeeze.img"
    #define MNTPATH "/data/local/mnt"
#else
    #define LI(...) fprintf(stdout,  __VA_ARGS__)
    #define LE(...) fprintf(stderr,  __VA_ARGS__)
    #define LOOPDEVICE "/dev/block/loop50"
    #define IMGPATH "/home/aditya/debian/squeeze.img"
    #define MNTPATH "/mnt/external"
#endif

static int my_umount(const char *d) 
{
    int r = umount(d);
    if (r<0) {
        r = -errno;
        LE("umount failed %s, %d, %s\n", d, errno, strerror(errno));
    }
    return r;
}

static int create_loop_device(const char *devname, const char *img_path) 
{
    int device_fd;
    int img_fd;
    int r = 0;
    struct loop_info64 linfo;
    

    img_fd = open(img_path, O_RDWR);
    if (img_fd < 0) {
        LE("Failed to open img %d\n", errno);
        r = -errno;
        goto ERR_IMG_FD;
    }

    LE("Creating loop device %s\n", devname);
    r = mknod(devname, S_IFBLK|0666, makedev(7, 30));
    if (r < 0) {
        LE("Failed to mknod loopdevice %d\n", errno);
        r = 0;
        if(errno != EEXIST) {
            r = -errno;
            goto ERR_MKNOD;
        }
    }

    LE("Opening loop device %s\n", devname);
    device_fd = open(devname, O_RDWR);
    if (device_fd < 0) {
        LE("failed to open loop device %d\n", errno);
        r = -errno;
        goto ERR_OPEN;
    }

    LE("ioctl loop_set_fd %s\n", devname);
    if (ioctl(device_fd, LOOP_SET_FD, img_fd) < 0) {
        r = -errno;
        LE("loop set fd ioctl failed %d\n", errno);
        goto ERR_SET_FD;
    }
    
    LE("ioctl loop_set_status %s\n", devname);
    memset(&linfo, 0, sizeof(struct loop_info64));
    strncpy((char *)linfo.lo_file_name, IMGPATH, LO_NAME_SIZE);
    if (ioctl(device_fd, LOOP_SET_STATUS, &linfo) < 0 ) {
        r = -errno;
        LE("loop set fd ioctl set status %d\n", errno);
        goto ERR_SET_FD;
    }

ERR_SET_FD:
    close(device_fd);
ERR_OPEN:
ERR_MKNOD:
    close(img_fd);
ERR_IMG_FD:
    return r;
}

static int clear_loop_device(const char *devname)
{
    int device_fd;
    int r = 0;

    LI("Clearing loop device\n");

    device_fd = open(devname, O_RDONLY);
    if (device_fd < 0) {
        LE("Failed to open loop %d, %s\n", errno, devname);
        return -errno;
    }

    if (ioctl(device_fd, LOOP_CLR_FD, 0) < 0) {
        LE("loop clr fd ioctl failed %d\n", errno);
        r = -errno;
    }

    close(device_fd);
    return r;
}

static int mount_devices(const char* devname, const char* mnt_path) 
{
    int r;
    char pts[64], proc[64], sysfs[64], s[64];

    r = mount(devname, mnt_path, "ext2", MS_SILENT, NULL); 
    if (r<0) {
        LE("mount failed %d\n", errno);
    }

    snprintf(pts, sizeof(pts), "%s/dev/pts", mnt_path);
    r = mount("devpts", pts, "devpts", MS_SILENT, NULL); 
    if (r<0) {
        if(errno==EBUSY)
            r = 0;
        else
            r = -errno;
        LE("mount devpts failed %d\n", r);
        goto ERRPTS;
    }
    snprintf(proc, sizeof(proc), "%s/proc", mnt_path);
    r = mount("proc", proc, "proc", MS_SILENT, NULL); 
    if (r<0) {
        if(errno==EBUSY)
            r = 0;
        else
            r = -errno;
        LE("mount proc failed %d\n", r);
        goto ERRPROC;
    }
    snprintf(sysfs, sizeof(sysfs), "%s/sys", mnt_path);
    r = mount("sysfs", sysfs, "sysfs", MS_SILENT, NULL); 
    if (r<0) {
        if(errno==EBUSY)
            r = 0;
        else
            r = -errno;
        LE("mount sysfs failed %d\n", r);
        goto ERRSYSFS;
    }

    snprintf(s, sizeof(s), "%s/system", mnt_path);
    mkdir(s, 0666);
    r = mount("/system", s, NULL, MS_BIND, NULL); 
    if (r<0) {
        if(errno==EBUSY)
            r = 0;
        else
            r = -errno;
        LE("mount bind failed %d\n", r);
        goto ERRBIND;
    }
    return r;

ERR:
    umount(s);
ERRBIND:
    umount(sysfs);
ERRSYSFS:
    umount(proc);
ERRPROC:
    umount(pts);
ERRPTS:
    umount(mnt_path);
    return r;
}

static int umount_devices(const char *mnt_path)
{
    int r = 0;
    char pts[64], proc[64], sysfs[64], s[64];
    snprintf(pts, sizeof(pts), "%s/dev/pts", mnt_path);
    snprintf(proc, sizeof(proc), "%s/proc", mnt_path);
    snprintf(sysfs, sizeof(sysfs), "%s/sys", mnt_path);
    snprintf(s, sizeof(s), "%s/system", mnt_path);

    my_umount(pts);
    my_umount(proc);
    my_umount(sysfs);
    my_umount(s);
    my_umount(mnt_path);
 
    return r;
}

static int break_chroot_jail(int dir_fd) 
{
    int r = 0;

    r = fchdir(dir_fd);
    if(r<0) {
        r = -errno;
        LE("Failed to fchdir - %s\n",strerror(errno));
        goto ERR;
    }
   
    r = chroot(".");
    if(r<0) {
        r = -errno;
        LE("Failed to escape out of chroot jail - %s\n",strerror(errno));
    }
ERR:
    return r;
} 

static int deinit(const char* devname, const char *mnt_path) 
{
    int r = 0;

    umount_devices(mnt_path);  
    r = clear_loop_device(devname);
    if (r !=0) {
        r = -errno;
        LE("clear_loop_device failed %d\n", r);
    }

    r = remove(devname);
    if (r !=0) {
        r = -errno;
        LE("remove device failed %s\n", strerror(errno));
    }
   
    return r;
}

static int init(const char *devname, const char *mnt_path, const char* img_path) {

    int r = 0, i;
    char tmp[256];
    int dir_fd;
    LI("Start init()\n");
    
    r = create_loop_device(devname, img_path);
    if (r<0 && r!=-EBUSY) 
        goto ERRMNT;
 
    r = mount_devices(devname, mnt_path);
    if (r<0 && r!=-EBUSY) 
        goto ERRMNT;

    LI("Trying chroot %s\n", mnt_path);
    r = chroot(mnt_path);
    if (r<0) {
        LE("chrooting failed %d\n", errno);
        goto ERRMNT;
    }

    return r;

ERRMNT:
    deinit(devname, mnt_path);
ERR:  
    LI("End init()\n");
 
}

static int start_ssh(void) 
{
    int r = 0;

    LI("Starting ssh...\n");
    r = system("/etc/init.d/ssh start");
    if (r<0) {
        LE("ssh start failed %d\n", r);
    }
    return r;
}

static int stop_ssh(void)
{

    int r = 0;

    LI("Stopping ssh...\n");
    r = system("/etc/init.d/ssh stop");
    if (r<0) {
        LE("ssh start failed %d\n", r);
    }
    return r;
}

int main(int argc, char **argv) 
{
    int dir_fd, r = 0;
    
    if(argc!=2) {
        LE("Invalid no of args, expected 2 got %d\n", argc); 
        return 0; 
    }

    r = mkdir(MNTPATH, 0755);
    if (r<0) {
        LE("Error creating dir %s, %s\n", MNTPATH, strerror(errno));
    }

    dir_fd = open("/", O_RDONLY);
    if(dir_fd<0) {
        LE("Failed to open / for reading - %s\n", strerror(errno));  
        return 0;
    } 

    if (strcmp(argv[1], "--start-ssh") == 0) {
        init(LOOPDEVICE, MNTPATH, IMGPATH); 
        start_ssh();
        break_chroot_jail(dir_fd);
    }

    else if (strcmp(argv[1], "--stop-ssh") == 0) {
        init(LOOPDEVICE, MNTPATH, IMGPATH); 
        stop_ssh();
        break_chroot_jail(dir_fd);
        deinit(LOOPDEVICE, MNTPATH); 
    }

    close(dir_fd);
    return 0;
} 
