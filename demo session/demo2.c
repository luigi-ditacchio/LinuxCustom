#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define O_SESSION       04000000

int main()
{
	int fd, ret;
	char *str = "\nIt's raining in the park, but meantime";
	char buf[1024];
	
	printf("Hi, I'm process %d\n", getpid());
	
	/*
	 * Open session on file "sultans"
	 */
	fd = open("sultans", O_RDWR | O_SESSION);
	if (fd < 0) {
		printf("Error in file creation\n");
		return -1;
	}
	
	
	/*
	 * Read and print what's written on file
	 */
	ret = read(fd, buf, 1024);
	if (ret < 0) {
		printf("Error in read\n");
		return -1;
	}
	
	buf[ret] = '\0';
	printf("Text of song line 1:\n%s\n", buf);
	
	
	/*
	 * Write second line of song
	 * not visible to other procs
	 */
	write(fd, str, strlen(str));
	
	
	if (fork()) {
		//parent - sleep 5 secs and close
		sleep(5);
		if (close(fd)) {
			printf("Close file failed\n");
			return -1;
		}
		return 0;
	}
	else {
		//child - open "sultans"
		fd = open("sultans", O_RDONLY);
		if (fd < 0) {
			printf("Error in file creation\n");
			return -1;
		}
		
		/*
		 * FIRST READ
		 */
		ret = read(fd, buf, 1024);
		if (ret < 0) {
			printf("Error in read\n");
			return -1;
		}
		
		buf[ret] = '\0';
		printf("CHILD: First read:\n%s\n", buf);
		
		
		sleep(7);
		
		/*
		 * SECOND READ
		 */
		lseek(fd, 0, SEEK_SET);
		ret = read(fd, buf, 1024);
		if (ret < 0) {
			printf("Error in read\n");
			return -1;
		}
		
		buf[ret] = '\0';
		printf("CHILD: Second read:\n%s\n", buf);
		
		
		if (close(fd)) {
			printf("Close file failed\n");
			return -1;
		}
		return 0;
	
	}
	return 0;
	
	
}
