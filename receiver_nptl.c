#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

#define THREAD_POOL_SIZE	16
#define BUFSIZE			58
#define UDP_PORT		3000

typedef struct bitmap {
	int bitmap;
	int last_bit;
	pthread_mutex_t mutex;
} bitmap_t;

typedef struct thread {
	int bit;
	pthread_t self;
	pthread_cond_t cond;
	bitmap_t *bitmap;
	char buf[64];	// cache line: 64
} thread_t;

struct timespec start;
struct timespec end;

static bitmap_t g_bitmap = {
	.bitmap = 0,
	.last_bit = 0,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};
static thread_t g_thread_pool[THREAD_POOL_SIZE];

static inline long timestamp(struct timespec *start, struct timespec *end)
{
	return (1000000000 * (end->tv_sec - start->tv_sec)) +
		(end->tv_nsec - start->tv_nsec);
}

static int __do_cb(char *data)
{
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("[%ld] th%ld: working...\n", timestamp(&start, &end),
	       syscall(SYS_gettid));

	/*
	// may be it will spend 10us to work
	usleep(5);
	*/

	return 0;
}

static void *th_cb(void *args)
{
	thread_t *th = (thread_t *)args;
	int bit = th->bit;
	bitmap_t *bm = th->bitmap;
	pthread_cond_t *c = &th->cond;
	pthread_mutex_t *m = &bm->mutex;

	while (1) {
		pthread_mutex_lock(m);
		while (!(bit & bm->bitmap)) {
			pthread_cond_wait(c, m);

			// get to work .....
			__do_cb(th->buf);
			break;
		}
		bm->bitmap &= ~bit;
		pthread_mutex_unlock(m);
	}

	return NULL;
}

static int thead_pool_init(void)
{
	int i;
	thread_t *th = NULL;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (i = 0; i < THREAD_POOL_SIZE; i++) {
		th = &g_thread_pool[i];
		th->bit = 1 << i;
		pthread_cond_init(&th->cond, NULL);
		//th->cond = PTHREAD_COND_INITIALIZER;
		th->bitmap = &g_bitmap;
		pthread_create(&th->self, &attr, th_cb, th);
	}

	return 0;
}

static void thead_pool_destroy(void)
{
	int i;
	for (i = 0; i < THREAD_POOL_SIZE; i++)
		pthread_cond_destroy(&g_thread_pool[i].cond);
}

static thread_t *get_thread(void)
{
	thread_t *th;
	bitmap_t *bm;
	int count;
	int bit;

	th = NULL;
	bm = &g_bitmap;
	count = THREAD_POOL_SIZE;
	bit = (bm->last_bit + 1) % THREAD_POOL_SIZE;

	pthread_mutex_lock(&bm->mutex);
	while (count-- > 0) {
		if (!((1 << bit) & bm->bitmap))
			break;
		bit = (bit + 1) % THREAD_POOL_SIZE;
	}
	if (count > 0) {
		th = &g_thread_pool[bit];
		bm->last_bit = bit;
	} else {
		printf("WARN: out of thread pool!!\n");
	}
	pthread_mutex_unlock(&bm->mutex);

	return th;
}

int main(int argc, char **argv)
{
	int sockfd;
	struct sockaddr_in serveraddr, clientaddr;
	socklen_t clientlen;
	char buf[BUFSIZE];
	ssize_t n;
	int retval = -1;
	thread_t *th;

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0)
		goto socket_err;

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(UDP_PORT);

	if (bind(sockfd, (struct sockaddr *) &serveraddr,
				sizeof(struct sockaddr_in)) < 0)
		goto bind_err;

	thead_pool_init();

	while (1) {
		th = get_thread();

		if (th) {
			bitmap_t *bm = th->bitmap;
			n = recvfrom(sockfd, th->buf, BUFSIZE, 0,
					(struct sockaddr *)&clientaddr,
					&clientlen);
			if (n < 0) {
				perror("ERROR: recvfrom");
				continue;
			}
			clock_gettime(CLOCK_MONOTONIC, &start);
			pthread_mutex_lock(&bm->mutex);
			bm->bitmap |= th->bit;
			pthread_cond_signal(&th->cond);
			pthread_mutex_unlock(&bm->mutex);
		} else {	// out of thread pool, sync!!!
			n = recvfrom(sockfd, buf, BUFSIZE, 0,
					(struct sockaddr *)&clientaddr,
					&clientlen);
			if (n < 0) {
				perror("ERROR: recvfrom");
				continue;
			}
			__do_cb(buf);
		}
	}

	thead_pool_destroy();
	retval = 0;

bind_err:
socket_err:
	close(sockfd);
	return retval;
}
