#define EXPORT_SYMTAB

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>     /* For kmalloc, kfree */
#include <linux/mutex.h>
#include <linux/wait.h>     /* For wait_queue */
#include "linux_mail_slot.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Migliori");
MODULE_DESCRIPTION("This module implements a device file driver for Linux FIFO mailslot");

static segment* mailslots[MAX_MINOR_NUM];
static elem head = {NULL, -1, NULL, NULL};
static elem tail = {NULL, -1, NULL, NULL};
static list writers_list[MAX_MINOR_NUM];
static list readers_list[MAX_MINOR_NUM];

static int major;
static int current_max_segment_size[MAX_MINOR_NUM];
static int used_space[MAX_MINOR_NUM];
static struct mutex mutex[MAX_MINOR_NUM];
static int read_blk_mode[MAX_MINOR_NUM];
static int write_blk_mode[MAX_MINOR_NUM];

DECLARE_WAIT_QUEUE_HEAD(writers_queue);
DECLARE_WAIT_QUEUE_HEAD(readers_queue);

//----------------------------------------------------------------------

static int mailslot_open(struct inode *inode, struct file *filp) {
    int current_minor = CURRENT_DEVICE;

    printk(KERN_INFO "%s: OPEN operation called on device file with minor number %d\n", MODNAME, current_minor);

    if (current_minor >= MAX_MINOR_NUM || current_minor < 0) {
        printk(KERN_ERR "%s: ERROR - device file with invalid minor number (%d). Minor should be in range [0-255]\n", MODNAME, current_minor);
        return -1;
    }
    return 0;
}

//----------------------------------------------------------------------

static int mailslot_release(struct inode *inode, struct file *filp) {
    int current_minor = CURRENT_DEVICE;

    printk(KERN_INFO "%s: CLOSE operation called on device file with minor number %d\n", MODNAME, current_minor);
    return 0;
}

//----------------------------------------------------------------------

static ssize_t mailslot_read(struct file * filp, char * buff, size_t len, loff_t * off) {
    int res, current_minor = CURRENT_DEVICE;
    elem me;
    segment* msg_to_delete;
    segment* tmp;
    char* kernel_buffer;
    elem* aux;

    printk(KERN_INFO "%s: READ operation called on device file with minor number %d\n", MODNAME, current_minor);

    me.task = current;
    me.pid = current->pid;
    me.next = NULL;
    me.prev = NULL;

    // preliminary checks
    if (len == 0) {
        printk(KERN_ERR "%s: ERROR - message not read because input length is 0\n", MODNAME);
        return -EMSGSIZE;
    }

    if(len > MAX_SEGMENT_SIZE)
        len = MAX_SEGMENT_SIZE;

    // allocating buffer in kernel space so as to move data in critical section without the possibility of going to sleep
    kernel_buffer = kmalloc(len, GFP_KERNEL);
    memset(kernel_buffer, 0, len);

    // entering in critical section
    if (read_blk_mode[current_minor] == BLOCKING_MODE) {
        if (mutex_lock_interruptible(&mutex[current_minor])) {
            printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
            return -ERESTARTSYS;
        }
    }

    else {
        if (!mutex_trylock(&mutex[current_minor])) {
            printk(KERN_ERR "%s: ERROR - non-blocking read operation and resource not available\n", MODNAME);
            kfree(kernel_buffer);
            return -EAGAIN;
        }
    }

    // there is nothing to read
    while(mailslots[current_minor] == NULL) {

        printk(KERN_INFO "%s: mailslot is empty, nothing to read\n", MODNAME);

        // if non-blocking, return (all or nothing)
        if (read_blk_mode[current_minor] == NON_BLOCKING_MODE) {
            printk(KERN_ERR "%s: ERROR - non-blocking read operation and nothing to read\n", MODNAME);
            kfree(kernel_buffer);
            mutex_unlock(&mutex[current_minor]);
            return -EAGAIN;
        }

        // put the task in readers_list
        aux = &(readers_list[current_minor].tail);
        if (aux->prev == NULL) {
            printk(KERN_ERR "%s: ERROR - malformed readers sleeplist, service damaged!\n", MODNAME);
            kfree(kernel_buffer);
            mutex_unlock(&mutex[current_minor]);
            return -1;
        }
        aux->prev->next = &me;
        me.prev = aux->prev;
        me.next = aux;
        aux->prev = &me;

        mutex_unlock(&mutex[current_minor]);

        printk(KERN_INFO "%s: process %d goes to sleep\n", MODNAME, current->pid);

        // going to sleep out of critical section
        res = wait_event_interruptible(readers_queue, mailslots[current_minor] != NULL);
        if (res != 0) {
            printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
            return -ERESTARTSYS;
        }

        // woken up, removing the task from the list (critical section)
        if (read_blk_mode[current_minor] == BLOCKING_MODE) {
            if (mutex_lock_interruptible(&mutex[current_minor])) {
                printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
                return -ERESTARTSYS;
            }
        }

        else {
            if (!mutex_trylock(&mutex[current_minor])) {
                printk(KERN_ERR "%s: ERROR - non-blocking read operation and resource not available\n", MODNAME);
                kfree(kernel_buffer);
                return -EAGAIN;
            }
        }

        aux = &(readers_list[current_minor].head);
        if (aux == NULL) {
            printk(KERN_ERR "%s: ERROR - malformed readers sleeplist upon wakeup, service damaged!\n", MODNAME);
            kfree(kernel_buffer);
            mutex_unlock(&mutex[current_minor]);
            return -1;
        }
        me.prev->next = me.next;
        me.next->prev = me.prev;

        printk(KERN_INFO "%s: process %d has been woken up\n", MODNAME, current->pid);
    }

    printk(KERN_INFO "%s : length to read = %zu and message size = %d\n", MODNAME, len, mailslots[current_minor]->size);

    // length to read < first segment size
    if(len <  mailslots[current_minor]->size){
        printk(KERN_ERR "%s: ERROR - trying to read an amount of data less than first segment size\n", MODNAME);
        kfree(kernel_buffer);
        mutex_unlock(&mutex[current_minor]);
        return -EINVAL;
    }

    len = mailslots[current_minor]->size;
    memcpy(kernel_buffer, mailslots[current_minor]->payload, len);
    used_space[current_minor] -= len;

    msg_to_delete = mailslots[current_minor];
    mailslots[current_minor] = mailslots[current_minor]->next;

    // in case of malformed writers sleeplist, recover initial situation
    aux = &(writers_list[current_minor].head);
    if (aux == NULL) {
        printk(KERN_ERR "%s: ERROR - malformed writers sleeplist, service damaged!\n", MODNAME);
        kfree(kernel_buffer);
        used_space[current_minor] += len;
        tmp = mailslots[current_minor];
        mailslots[current_minor] = msg_to_delete;
        mailslots[current_minor]->next = tmp;
        mutex_unlock(&mutex[current_minor]);
        return -1;
    }

    // time to awake writers
    while (aux->next != &(writers_list[current_minor].tail)) {
        wake_up_process(aux->next->task);
        aux = aux->next;
    }

    mutex_unlock(&mutex[current_minor]);

    // move data to user space buffer with copy_to_user (out of critical section)
    if (copy_to_user(buff, kernel_buffer, len)) {
        printk(KERN_ERR "%s: ERROR in copy_to_user()\n", MODNAME);
        return -1;
    }

    kfree(kernel_buffer);
    kfree(msg_to_delete->payload);
    kfree(msg_to_delete);

    return len;
}

//----------------------------------------------------------------------

static ssize_t mailslot_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
    int res, current_minor = CURRENT_DEVICE;
    elem me;
    segment* new_msg;
    segment* tmp;
    elem* aux;

    printk(KERN_INFO "%s: WRITE operation called on device file with minor number %d\n", MODNAME, current_minor);

    me.task = current;
    me.pid = current->pid;
    me.next = NULL;
    me.prev = NULL;

    // preliminary check before allocation
    if (len > current_max_segment_size[current_minor] || len == 0) {
        printk(KERN_ERR "%s: ERROR - message not written because too large or empty. Message size = %zu, Maximum segment size = %d\n",
                    MODNAME, len, current_max_segment_size[current_minor]);
        return -EMSGSIZE;
    }

    // allocating segment out of critical section (possibility of going to sleep)
    new_msg = kmalloc(sizeof(segment), GFP_KERNEL);
    memset(new_msg, 0, sizeof(segment));

    new_msg->payload = kmalloc(len, GFP_KERNEL);
    memset(new_msg->payload, 0, len);
    if (copy_from_user(new_msg->payload, buff, len)) {
        printk(KERN_ERR "%s: ERROR in copy_from_user()\n", MODNAME);
        return -1;
    }

    if (write_blk_mode[current_minor] == BLOCKING_MODE) {
        if (mutex_lock_interruptible(&mutex[current_minor])) {
                printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
                return -ERESTARTSYS;
        }
    }

    else {
        if (!mutex_trylock(&mutex[current_minor])) {
            printk(KERN_ERR "%s: ERROR - non-blocking write operation and resource not available\n", MODNAME);
            kfree(new_msg->payload);
            kfree(new_msg);
            return -EAGAIN;
        }
    }

    // mailslot is full or free space is not enough
    while( len > (MAX_MAIL_SLOT_SIZE-used_space[current_minor]) ) {

        printk(KERN_INFO "%s: mailslot full or insufficient space\n", MODNAME);

        // if non-blocking, return (all or nothing)
        if (write_blk_mode[current_minor] == NON_BLOCKING_MODE) {
            printk(KERN_ERR "%s: ERROR - non-blocking write operation and insufficient space\n", MODNAME);
            kfree(new_msg->payload);
            kfree(new_msg);
            mutex_unlock(&mutex[current_minor]);
            return -EAGAIN;
        }

        // put the task in writers_list
        aux = &(writers_list[current_minor].tail);
        if (aux->prev == NULL) {
            printk(KERN_ERR "%s: ERROR - malformed writers sleeplist, service damaged!\n", MODNAME);
            kfree(new_msg->payload);
            kfree(new_msg);
            mutex_unlock(&mutex[current_minor]);
            return -1;
        }
        aux->prev->next = &me;
        me.prev = aux->prev;
        me.next = aux;
        aux->prev = &me;

        mutex_unlock(&mutex[current_minor]);

        printk(KERN_INFO "%s: process %d goes to sleep\n", MODNAME, current->pid);

        // going to sleep out of critical section
        res = wait_event_interruptible(writers_queue, len <= (MAX_MAIL_SLOT_SIZE-used_space[current_minor]));
        if (res != 0) {
            printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
            return -ERESTARTSYS;
        }

        // woken up, removing the task from the list (critical section)
        if (write_blk_mode[current_minor] == BLOCKING_MODE) {
            if (mutex_lock_interruptible(&mutex[current_minor])) {
                printk(KERN_ERR "%s: ERROR - process %d has been woken up by a signal\n", MODNAME, current->pid);
                return -ERESTARTSYS;
            }
        }

        else {
            if (!mutex_trylock(&mutex[current_minor])) {
                printk(KERN_ERR "%s: ERROR - non-blocking write operation and resource not available\n", MODNAME);
                kfree(new_msg->payload);
                kfree(new_msg);
                return -EAGAIN;
            }
        }

        aux = &(writers_list[current_minor].head);
        if (aux == NULL) {
            printk(KERN_ERR "%s: ERROR - malformed writers sleeplist upon wakeup, service damaged!\n", MODNAME);
            kfree(new_msg->payload);
            kfree(new_msg);
            mutex_unlock(&mutex[current_minor]);
            return -1;
        }

        me.prev->next = me.next;
        me.next->prev = me.prev;

        printk(KERN_INFO "%s: process %d has been woken up\n", MODNAME, current->pid);
    }

    new_msg->size = len;
    new_msg->next = NULL;

    // add the segment to the mailslot and increment used space in the mailbox
    if (mailslots[current_minor] == NULL)
        mailslots[current_minor] = new_msg;

    else {
        tmp = mailslots[current_minor];
        while(tmp->next != NULL)
            tmp = tmp->next;
        tmp->next = new_msg;
    }

    used_space[current_minor] += len;

    // time to awake one reader
    aux = &(readers_list[current_minor].head);
    if (aux == NULL) {
        printk(KERN_ERR "%s: ERROR - malformed readers sleeplist, service damaged!\n", MODNAME);
        kfree(new_msg->payload);
        kfree(new_msg);
        mutex_unlock(&mutex[current_minor]);
        return -1;
    }

    if (aux->next != &(readers_list[current_minor].tail))
        wake_up_process(aux->next->task);

    mutex_unlock(&mutex[current_minor]);

    return len;
}

//----------------------------------------------------------------------

static long mailslot_ctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int current_minor = CURRENT_DEVICE;

	printk(KERN_INFO "%s : IOCTL operation called on device file with minor number %d - cmd = %d, arg = %ld\n",
                MODNAME, current_minor, cmd, arg);

	switch(cmd){

		case CHANGE_WRITE_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: changing write blocking mode for device file with minor number %d\n", MODNAME, current_minor);

            if (arg != 0 && arg != 1) {
                printk(KERN_ERR "%s: ERROR - invalid argument for blocking mode (0 or 1)\n", MODNAME);
                return -EINVAL;
            }
            write_blk_mode[current_minor] = arg;
			break;

        case CHANGE_READ_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: changing read blocking mode for device file with minor number %d\n", MODNAME, current_minor);

            if (arg != 0 && arg != 1) {
                printk(KERN_ERR "%s: ERROR - invalid argument for blocking mode (0 or 1)\n", MODNAME);
                return -EINVAL;
            }
            read_blk_mode[current_minor] = arg;
            break;

		case CHANGE_MAX_SEGMENT_SIZE_CTL:
            printk(KERN_INFO "%s: changing maximum segment size for device file with minor number %d\n", MODNAME, current_minor);

			if(arg < 1 || arg > MAX_SEGMENT_SIZE){
                printk(KERN_ERR "%s: ERROR - invalid argument for maximum segment size\n", MODNAME);
                return -EINVAL;
            }
            current_max_segment_size[current_minor] = arg;
			break;

		case GET_MAX_SEGMENT_SIZE_CTL:
            printk(KERN_INFO "%s: getting maximum segment size for device file with minor number %d\n", MODNAME, current_minor);
            return current_max_segment_size[current_minor];

		case GET_FREESPACE_SIZE_CTL:
            printk(KERN_INFO "%s: getting free space size for device file with minor number %d\n", MODNAME, current_minor);
            return MAX_MAIL_SLOT_SIZE - used_space[current_minor];

        case GET_WRITE_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: getting write blocking mode for device file with minor number %d\n", MODNAME, current_minor);
            return write_blk_mode[current_minor];

        case GET_READ_BLOCKING_MODE_CTL:
            printk(KERN_INFO "%s: getting read blocking mode for device file with minor number %d\n", MODNAME, current_minor);
            return read_blk_mode[current_minor];

		default:
			printk(KERN_ERR "%s: ERROR - inappropriate ioctl for device\n", MODNAME);
			return -ENOTTY;
	}
	return 0;
}


static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mailslot_open,
    .release = mailslot_release,
    .read = mailslot_read,
    .write = mailslot_write,
    .unlocked_ioctl = mailslot_ctl
};


int init_module(void) {
    int i;
	major = register_chrdev(0, DEVICE_NAME, &fops);

	if (major < 0) {
	  printk(KERN_ERR "%s: ERROR - registering mail slot device failed\n", MODNAME);
	  return major;
	}

	printk(KERN_INFO "%s: mail slot device registered. Major number = %d\n", MODNAME, major);

    for (i = 0; i < MAX_MINOR_NUM; i++){
        mailslots[i] = NULL;
        current_max_segment_size[i] = MAX_SEGMENT_SIZE;
        write_blk_mode[i] = BLOCKING_MODE;
        read_blk_mode[i] = BLOCKING_MODE;
        used_space[i] = 0;
        mutex_init(&mutex[i]);
        readers_list[i].head = head;
        readers_list[i].tail = tail;
        readers_list[i].head.next = &readers_list[i].tail;
        readers_list[i].tail.prev = &readers_list[i].head;
        writers_list[i].head = head;
        writers_list[i].tail = tail;
        writers_list[i].head.next = &writers_list[i].tail;
        writers_list[i].tail.prev = &writers_list[i].head;
    }
    return 0;
}

void cleanup_module(void) {
    int i;
    for(i = 0; i < MAX_MINOR_NUM; i++) {
        while(mailslots[i] != NULL) {
            segment* msg_to_delete = mailslots[i];
            mailslots[i] = mailslots[i]->next;
            kfree(msg_to_delete->payload);
            kfree(msg_to_delete);
        }
	}

	unregister_chrdev(major, DEVICE_NAME);
	printk(KERN_INFO "%s: mail slot device unregistered. Major number = %d\n", MODNAME, major);
}

