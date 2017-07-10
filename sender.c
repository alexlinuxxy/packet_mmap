#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUFSIZE 58
#define SERVER_IP_ADDR "192.168.0.255"
#define UDP_PORT 3000

/* 
 * error - wrapper for perror
 */
static void error(char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int sockfd, n;
	struct sockaddr_in serveraddr;
	char buf[BUFSIZE];
	int broadcast = 1;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		error("socket");
	if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
		error("setsockopt(SO_BROADCAST)");

	memset((char *)&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(UDP_PORT);
	if (inet_aton(SERVER_IP_ADDR, &serveraddr.sin_addr) == 0)
		error("inet_aton");

	memset(buf, 0xAB, sizeof(buf));

	while (1) {
		int i;
		struct timespec req = {
			.tv_sec = 0,
			.tv_nsec = 500000000,
		};
		struct timespec rem;
		int ret;

		for (i = 0; i < 16; i++) {
			n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
			if (n < 0)
				error("sendto");
			printf("send to %s:%d %d byte\n", SERVER_IP_ADDR, UDP_PORT, n);
		}
retry:
		ret = nanosleep(&req, &rem);
		if (ret) {
			if (errno == EINTR) {
				req.tv_sec = rem.tv_sec;
				req.tv_nsec = rem.tv_nsec;
				goto retry;
			}
			error("nanosleep");
		}
	}

	close(sockfd);
	return 0;
}
