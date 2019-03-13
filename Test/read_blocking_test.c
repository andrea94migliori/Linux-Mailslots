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

void *thread_write(void *args) {
    sleep(30);
    int fd = *(int*)args;
    write(fd, "test", 5);
}

void *thread_read(void* args) {
    int fd = *(int*)args;
    char read_buf[MAX_SEGMENT_SIZE];
    printf("Blocking read called on empty mailslot\n");
    read(fd, read_buf, MAX_SEGMENT_SIZE);
    printf("Reader woken up\n");
}



int main(int argc, char** argv) {
    int ret;
    char read_buf[MAX_SEGMENT_SIZE];
    pthread_t read_thread, write_thread;


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

    /* BLOCKING READ */
    if(pthread_create(&read_thread, NULL, thread_read, (void*) &fd)) {
            fprintf(stderr, "Error creating thread\n");
            return -1;
    }

    if(pthread_create(&write_thread, NULL, thread_write, (void*) &fd)) {
        fprintf(stderr, "Error creating thread\n");
        return -1;
    }

    if(pthread_join(read_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
    }

    if(pthread_join(write_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
    }

    close(fd);
    return 0;
}
