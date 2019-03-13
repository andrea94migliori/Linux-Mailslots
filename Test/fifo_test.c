#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio_ext.h>
#include "const.h"

int main(int argc, char** argv) {
    int ret, count = 0;
    char msg[MAX_SEGMENT_SIZE];
    char read_buf[MAX_SEGMENT_SIZE+1];


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

    printf("\nInsert a message or type stop to end:\n");
    fgets(msg, MAX_SEGMENT_SIZE+1, stdin);
    while (strncmp(msg, "stop", strlen("stop"))) {
        __fpurge(stdin);
        msg[strcspn(msg, "\n")] = '\0';
        ret = write(fd, msg, strlen(msg)+1);
        if (ret < 0) {
            printf("ERROR in write: %s\n", strerror(errno));
            return -1;
        }
        count++;
        printf("Insert a message or type stop to end:\n");
        fgets(msg, MAX_SEGMENT_SIZE+1, stdin);
    }

    printf("\nRead messages:\n");

    while (count > 0) {
        ret = read(fd, read_buf, MAX_SEGMENT_SIZE);
        if (ret < 0) {
            printf("ERROR in read: %s\n", strerror(errno));
            return -1;
        }
        count--;
        read_buf[ret] = '\0';
        printf("%s\n", read_buf);
    }

    close(fd);
    return 0;
}
