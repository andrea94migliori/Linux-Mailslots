#ifndef LINUX_MAIL_SLOT_HEADER
#define LINUX_MAIL_SLOT_HEADER

#define MODNAME "MAIL_SLOT"
#define DEVICE_NAME "mailslot"  /* Device file name in /dev/ */

#define MAX_MAIL_SLOT_SIZE (1<<20) // 1MB of max storage (upper limit)
#define MAX_SEGMENT_SIZE (1<<10) // 1KB of max segment size (upper limit)
#define MAX_MINOR_NUM (256)

#define CURRENT_DEVICE MINOR(filp->f_dentry->d_inode->i_rdev)

#define BLOCKING_MODE 0
#define NON_BLOCKING_MODE 1

// IOCTL
#define CHANGE_WRITE_BLOCKING_MODE_CTL 3
#define CHANGE_READ_BLOCKING_MODE_CTL 4
#define CHANGE_MAX_SEGMENT_SIZE_CTL 5
#define GET_MAX_SEGMENT_SIZE_CTL 6
#define GET_FREESPACE_SIZE_CTL 7
#define GET_WRITE_BLOCKING_MODE_CTL 8
#define GET_READ_BLOCKING_MODE_CTL 9

typedef struct segment{
    int size;
    char* payload;
    struct segment* next;
} segment;

typedef struct _elem{
    struct task_struct *task;
    int pid;
    struct _elem * next;
    struct _elem * prev;
} elem;

typedef struct list{
   elem head;
   elem tail;
}list;

static int mailslot_open(struct inode *, struct file *);
static int mailslot_release(struct inode *, struct file *);
static ssize_t mailslot_read(struct file * , char * , size_t , loff_t *);
static ssize_t mailslot_write(struct file *, const char *, size_t, loff_t *);
static long mailslot_ctl (struct file *filp, unsigned int param1, unsigned long param2);


#endif
