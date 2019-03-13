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
    int fd = *(int*)args;
    write(fd, "test", MAX_SEGMENT_SIZE);
}

void *thread_read(void* args) {
    sleep(30);
    int fd = *(int*)args;
    char read_buf[MAX_SEGMENT_SIZE];
    read(fd, read_buf, MAX_SEGMENT_SIZE);
}


int main(int argc, char** argv) {
    int ret, i;
    char read_buf[MAX_SEGMENT_SIZE];
    pthread_t read_thread;
    pthread_t write_thread[N+1];


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

    while(ioctl(fd, GET_FREESPACE_SIZE_CTL) < MAX_MAIL_SLOT_SIZE) {
        read(fd, read_buf, MAX_SEGMENT_SIZE);
   }

    /* BLOCKING WRITE */
    for (i = 0; i < N; i++) {
        if(pthread_create(&write_thread[i], NULL, thread_write, (void*) &fd)) {
            fprintf(stderr, "Error creating thread\n");
            return -1;
        }
    }

    printf("Blocking write called on a full mailslot\n");

    if(pthread_create(&write_thread[N], NULL, thread_write, (void*) &fd)) {
        fprintf(stderr, "Error creating thread\n");
        return -1;
    }

    if(pthread_create(&read_thread, NULL, thread_read, (void*) &fd)) {
            fprintf(stderr, "Error creating thread\n");
            return -1;
    }

    for (i = 0; i <= N; i++) {
        if(pthread_join(write_thread[i], NULL)) {
            fprintf(stderr, "Error joining thread\n");
            return -1;
        }
    }

    printf("Writer woken up\n");

    if(pthread_join(read_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return -1;
    }

    close(fd);
    return 0;
}
