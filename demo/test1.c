/* 
本程序维护一个 256bytes*4 缓冲区，两个信号量保护（读和写）。创建两
个线程，一个用于采集声卡数据并写到缓冲区，数据采集线程使用ALSA接口
编程，设置采样率 22333，周期帧数 128，帧格式 U8,声道数 2，每个周期
大约 5.73ms，每个周期 256bytes。另外一个将缓冲区数据广播到网络，每
次发送 256bytes。
*/
 
#define ALSA_PCM_NEW_HW_PARAMS_API 
 
#include <alsa/asoundlib.h>
//#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/types.h>	/* basic system data types */
#include <sys/socket.h>	/* basic socket definitions */
#include <netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>	/* inet(3) functions */
 
#define RATE 22333
#define CHANNEL 2
#define FORMAT	SND_PCM_FORMAT_U8
#define FRAMES	128
 
#define SIZE CHANNEL*FRAMES*1
 
#define NBUFF    4  
 
// 套接字端口
#define PORT 10000	
#define	SA	struct sockaddr
 
// 数据缓冲区及信号量
struct {    
	char   buffer[1024];  
	sem_t mutex, nempty, nstored;  
} shared;
 
char* pRead = shared.buffer; //读指针
char* pWrite = shared.buffer; //写指针
void* sendData(void *arg); //线程函数，广播数据
void* generateData(void *arg); //线程函数，读声卡
 
// 计数变量
long produce=0;
long consume=0;
long totalTime = 0;
 
int main() 
{ 	
	pthread_t   tid_generateData, tid_sendData;
 
	// 初始化信号量
    sem_init(&shared.mutex, 0, 1);  //未用到
    sem_init(&shared.nempty, 0, NBUFF);  
    sem_init(&shared.nstored, 0, 0);  
   
    // 创建读声卡线程，将数据保存到缓冲区
    pthread_create(&tid_generateData, NULL, generateData, NULL);
	// 创建广播线程，将缓冲区数据发送到网络
    pthread_create(&tid_sendData, NULL, sendData, NULL);  
  
    pthread_join(tid_sendData, NULL);  
    pthread_join(tid_generateData, NULL);  
  
    sem_destroy(&shared.mutex);  
    sem_destroy(&shared.nempty);  
    sem_destroy(&shared.nstored);  
    exit(0);  
} 
 
void* sendData(void *arg)
{
	int			sockfd;
	struct 		sockaddr_in		servaddr;
	
	/* socket 初始化 */
	const int on = 1;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	inet_pton(AF_INET, "192.168.1.105", &servaddr.sin_addr);
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	
	int n;
	const SA *pservaddr = (SA*)(&servaddr);	
	socklen_t servlen = sizeof(SA);
	printf("\n\n\n\n\nData generating starts, Broadcasting ...\n\n\n\n\n\n\n\n\n\n");
	printf("|------------------------------------------------------------|\t0\tmin |\n\033[1A|");
	while(1)
	{
		// 获取nstored信号量
		sem_wait(&shared.nstored);
		
		// 发送数据
		n = sendto(sockfd, pRead, 256, 0, pservaddr, servlen);
		if(n!=256) //printf("send short: send %d\n",n)
		{
			sem_post(&shared.nstored);
			continue;
		}
		
		// 更新缓冲区读指针
		pRead += 256;
		if(pRead-1024 == shared.buffer)
			pRead = shared.buffer;
			
		// 释放nempty信号量
		sem_post(&shared.nempty);
		
		// 计数器
		if(0 == ++consume % 175)
		{
			++totalTime;
			printf("-");
			fflush(stdout);
			if(0 == totalTime %60)
				printf("|\t%ld\tmin |\n\033[1A|", totalTime/60),fflush(stdout);
		}
	}
}
 
void* generateData(void *arg)
{
	// 设备打开初始化配置
	int rc;  
	snd_pcm_t *handle; 
	snd_pcm_hw_params_t *params; 
	unsigned int val = RATE;  // 采样率 22333
	int dir; 
	snd_pcm_uframes_t frames;  
	 
	rc = snd_pcm_open(&handle, "default",SND_PCM_STREAM_CAPTURE, 0); // 打开方式为“抓取数据”
	
	if (rc < 0) 
	{
		fprintf(stdout, "unable to open pcm device: %s\n",snd_strerror(rc));
		exit(1); 
	}  
	snd_pcm_hw_params_alloca(&params); 
	snd_pcm_hw_params_any(handle, params);  
	snd_pcm_hw_params_set_access(handle, params,SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(handle, params,SND_PCM_FORMAT_U8); //帧数据格式，每帧1byte
	snd_pcm_hw_params_set_channels(handle, params, 2);  			// 声道数 2
	snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);  	// 获取真实采样率 22333
	snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir); // 获取每周期帧数 64
	rc = snd_pcm_hw_params(handle, params); 
	if (rc < 0)
	{     
		fprintf(stdout,"unable to set hw parameters: %s\n", snd_strerror(rc)); 
		exit(1); 
	}  
	snd_pcm_hw_params_get_period_size(params, &frames, &dir); 
	snd_pcm_hw_params_get_period_time(params, &val, &dir);
 
	while (1) 
	{     
		frames = FRAMES; // 更改每周期的帧数，设置为128
		sem_wait(&shared.nempty);
		
		rc = snd_pcm_readi(handle, pWrite, frames); // 获取256 Bytes 数据
		
		if (rc != (int)frames) 
		{
			usleep(1000);   
			snd_pcm_prepare(handle);
			sem_post(&shared.nempty);
			continue;
		}
 
		// 更新缓冲区写指针
		pWrite += 256;
		if(pWrite-1024 == shared.buffer)
			pWrite = shared.buffer;
		sem_post(&shared.nstored);
		// 计数器
		++produce;
	}  
	snd_pcm_drain(handle); 
	snd_pcm_close(handle);
}