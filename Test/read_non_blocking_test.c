#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio_ext.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include "const.h"


void *thread_read(void* args) {
    int fd = *(int*)args;
    char read_buf[MAX_SEGMENT_SIZE];
    printf("Non-blocking read called on empty mailslot\n");
    if (read(fd, read_buf, MAX_SEGMENT_SIZE) < 0)
        printf("Nothing to read and non-blocking read operation. ABORTED.\n");
}



int main(int argc, char** argv) {
    int ret;
    char read_buf[MAX_SEGMENT_SIZE];
    pthread_t read_thread;


	if(argc != 3){
		printf("You should pass MAJOR number and MINOR number as parameters\n");
		return -1;
	}

	int major = atoi(argv[1]);
	int minor = atoi(argv[2]);
	dev_t device = makedev(major, minor);

    char pathname[80];
    sprintf(pathname,"/dev/mailslot%d", minor);

	if( mknod(pathname, S_IFCHR|0666, device) == -1 ){
		if(errno == EEXIST)
			printf("Pathname '%s' already exists\n",pathname);

        else {
			printf("ERROR in the creation of the file %s: %s\n", pathname, strerror(errno));
			return -1;
        }
    }

	int fd = open(pathname, 0666);

	if(fd == -1) {
		printf("ERROR while opening the file %s: %s\n", pathname, strerror(errno));
		return -1;
    }

    while(ioctl(fd, GET_FREESPACE_SIZE_CTL) < MAX_MAIL_SLOT_SIZE)
       read(fd, read_buf, MAX_SEGMENT_SIZE);

    /* NON-BLOCKING READ */
    ioctl(fd, CHANGE_READ_BLOCKING_MODE_CTL, NON_BLOCKING_MODE);

    if(pthread_create(&read_thread, NULL, thread_read, (void*) &fd)) {
            fprintf(stderr, "Error creating thread\n");
            return -1;
    }

    if(pthread_join(read_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
    }

    ioctl(fd, CHANGE_READ_BLOCKING_MODE_CTL, BLOCKING_MODE);

    close(fd);
    return 0;
}
