/* 
本程序维护一个256bytes*4缓冲区，两个信号量保护（读和写）。创建两
个线程，一个监听广播消息并将获取的数据写到缓冲区，另外一个线程将
缓冲区数据写到声卡。写声卡编程使用ALSA接口编程，采样率 22333，周
期帧数128，帧格式U8,声道数2，计算下来，每个周期大约 5.73ms，每个
周期256bytes。
*/
 
#define ALSA_PCM_NEW_HW_PARAMS_API 
 
#include <alsa/asoundlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <semaphore.h>
#include	<sys/types.h>	/* basic system data types */
#include	<sys/socket.h>	/* basic socket definitions */
#include	<netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include	<arpa/inet.h>	/* inet(3) functions */
 
// 声卡配置参数
#define RATE 22333
#define CHANNEL 2
#define FORMAT	SND_PCM_FORMAT_U8
#define FRAMES	128
 
#define SIZE CHANNEL*FRAMES*1
 
#define NBUFF    4  
#define SEM_MUTEX   "mutex"       
#define SEM_NEMPTY  "nempty"  
#define SEM_NSTORED "nstored"
 
#define PORT 10000
#define	SA	struct sockaddr
 
// 缓冲区及信号量
struct {    
  char   buffer[1024];  
  sem_t mutex, nempty, nstored;  
} shared;
 
// 缓冲区读写指针
char* pRead = shared.buffer;
char* pWrite = shared.buffer;
void* recvData(void *arg);
void* generateSnd(void *arg);
 
// 计数变量
long produce=0;
long consume=0;
long totalTime = 0;
 
int main() 
{ 	
	pthread_t   tid_generateSnd, tid_recvData; 
 
	// 信号量初始化
    sem_init(&shared.mutex, 0, 1);  //未使用
    sem_init(&shared.nempty, 0, NBUFF);  
    sem_init(&shared.nstored, 0, 0);  
   
	// 创建发声线程
    pthread_create(&tid_generateSnd, NULL, generateSnd, NULL);  
	// 创建数据接收线程
    pthread_create(&tid_recvData, NULL, recvData, NULL);  
  
    pthread_join(tid_recvData, NULL);  
    pthread_join(tid_generateSnd, NULL);  
  
    sem_destroy(&shared.mutex);  
    sem_destroy(&shared.nempty);  
    sem_destroy(&shared.nstored);  
    exit(0);  
} 
 
void* recvData(void *arg)
{
	// 配置socket
	int			sockfd;
	struct 		sockaddr_in		servaddr;
	/* initialization for socket */
	const int on = 1;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	bind(sockfd, (SA *) &servaddr, sizeof(servaddr));
	setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	
	int n;
	SA	*preply_addr = (SA*)malloc(sizeof(SA));
	socklen_t len = sizeof(SA);
	
	// 计时器
	printf("\n\n\n\n\nData receiving starts, voice generating ...\n\n\n\n\n\n\n\n\n\n");
	printf("|------------------------------------------------------------|\t0\tmin |\n\033[1A|");
	
	for (;;) 
	{
		// 获取写信号量
		sem_wait(&shared.nempty);
		
		// 监听网络，并将数据写到缓冲区
		n = recvfrom(sockfd, pWrite, 256, 0, preply_addr, &len);
		if (n < 0) {
			if (errno == EINTR)
				break;		/* waited long enough for replies */
		} 
		else if(n != 256) 
		{
			sem_post(&shared.nempty);
			continue;
		}
		else
		{
			// 更新写指针
			pWrite += 256;
			if(pWrite-1024 == shared.buffer)
				pWrite = shared.buffer;
			// 释放读信号量
			sem_post(&shared.nstored);
			if(0 == ++produce % 175)
			{
				++totalTime;
				printf("-");
				fflush(stdout);
				if(0 == totalTime %60)
					printf("|\t%ld\tmin |\n\033[1A|", totalTime/60),fflush(stdout);
			}
		}
	}
	free(preply_addr);
}
 
void* generateSnd(void *arg)
{
	// 声卡配置变量
	int rc;  
	snd_pcm_t *handle; 
	snd_pcm_hw_params_t *params; 
	unsigned int val = RATE; 
	int dir; 
	snd_pcm_uframes_t frames;  
	
	rc = snd_pcm_open(&handle, "default",SND_PCM_STREAM_PLAYBACK, 0); // 播放模式打开
	
	if (rc < 0) 
	{
		fprintf(stderr, "unable to open pcm device: %s\n",snd_strerror(rc));
		exit(1); 
	}  
	
	// 配置声卡，和发送进程的声卡配置一致
	snd_pcm_hw_params_alloca(¶ms); 
	snd_pcm_hw_params_any(handle, params);  
	snd_pcm_hw_params_set_access(handle, params,SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(handle, params,SND_PCM_FORMAT_U8); 
	snd_pcm_hw_params_set_channels(handle, params, 2); 
	snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir); 
	snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir); 
	rc = snd_pcm_hw_params(handle, params); 
	if (rc < 0)
	{     
		fprintf(stderr,"unable to set hw parameters: %s\n", snd_strerror(rc)); 
		exit(1); 
	}  
	snd_pcm_hw_params_get_period_size(params, &frames, &dir); 
	snd_pcm_hw_params_get_period_time(params, &val, &dir);
 
	while (1) 
	{     
		frames = FRAMES;
		// 获取读信号量
		sem_wait(&shared.nstored);
    
		// 向声卡写数据
		rc = snd_pcm_writei(handle, pRead, frames);
		if (rc != (int)frames) 
		{
			usleep(1000);
			snd_pcm_prepare(handle);
			sem_post(&shared.nstored);
			continue;
		}
		
		// 更新读指针
		pRead += 256;
		if(pRead-1024 == shared.buffer)
			pRead = shared.buffer;
		// 释放写信号量
		sem_post(&shared.nempty);
		++consume;
	}  
	snd_pcm_drain(handle); 
	snd_pcm_close(handle);
}