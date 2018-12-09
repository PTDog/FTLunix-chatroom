#include <stdio.h> 
#include <stdlib.h>
#include <sys/types.h> //数据类型定义
#include <sys/stat.h>
#include <netinet/in.h> //定义数据结构sockaddr_in
#include <arpa/inet.h> //定义数据结构sockaddr_in
#include <sys/socket.h> //提供socket函数及数据结构
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <errno.h>
#include <sys/shm.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <getopt.h>
#include <sys/wait.h>
#include <pthread.h>
#define PERM S_IRUSR|S_IWUSR 
#define MYPORT 3490 //宏定义定义通信端口
#define BACKLOG 10 //宏定义，定义服务程序可以连接的最大客户数量
#define MAXHOST 32//maximum host name characters
#define BUFSIZE 1024
#define WELCOME "|----------Welcome to the chat room! ----------|" //宏定义，当客户端连接服务端时，想客户发送此欢迎字符串
const char *pathName = "log.txt";
// checkpoint sent flag
volatile sig_atomic_t ck_ready = 0;
/* SIGALRM signal handler */
/* SIGALRM signal handler, set hb_ready when alarmed */
void sigalrm_handler(){
//    alarm(heartbeat_itv);
    ck_ready = 1;
    return;
}
void sigint_handler();
void sigchld_handler();
void *thread(void *p);
//转换函数，将int类型转换成char *类型
void itoa(int i,char*string)
{
	int power,j;
	j=i;
	for(power=1;j>=10;j/=10)
		power*=10;
	for(;power>0;power/=10)
	{
		*string++='0'+i/power;
		i%=power;
	}
	*string='\0';
}

//得到当前系统时间
void get_cur_time(char * time_str)
{
	time_t timep;
	struct tm *p_curtime;
	char *time_tmp;
	time_tmp=(char *)malloc(2);
	memset(time_tmp,0,2);

	memset(time_str,0,20);
	time(&timep);
	p_curtime = localtime(&timep);
	strcat(time_str," (");
	itoa(p_curtime->tm_hour,time_tmp);
	strcat(time_str,time_tmp);
	strcat(time_str,":");
	itoa(p_curtime->tm_min,time_tmp);
	strcat(time_str,time_tmp);
	strcat(time_str,":");
	itoa(p_curtime->tm_sec,time_tmp);
	strcat(time_str,time_tmp);
	strcat(time_str,")");
	free(time_tmp);
}
//创建共享存储区，进程间通讯
key_t shm_create()
{
	key_t shmid;
//shmid = shmget(IPC_PRIVATE,1024,PERM);
	if((shmid = shmget(IPC_PRIVATE,1024,PERM)) == -1)
	{
		fprintf(stderr,"Create Share Memory Error:%s\n\a",strerror(errno));
		exit(1);
	}
	return shmid;
}

int checkpoint_freq = 60;

//端口绑定函数,创建套接字，并绑定到指定端口
int bindPort(unsigned short int port)
{ 
	int sockfd;
	struct sockaddr_in my_addr;
	sockfd = socket(AF_INET,SOCK_STREAM,0);//创建基于流套接字
	my_addr.sin_family = AF_INET;//IPv4协议族
	my_addr.sin_port = htons(port);//端口转换
	my_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(my_addr.sin_zero),0);//置空
	int on = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(1);
	}
	if(bind(sockfd,(struct sockaddr*)&my_addr,sizeof(struct sockaddr)) == -1)//绑定本地IP
	{
		perror("bind");
		exit(1);
	}
	printf("bing success!\n");
	return sockfd;
}


int main(int argc, char *argv[])
{
	int sockfd,clientfd,sin_size,recvbytes; //定义监听套接字、客户套接字
	pid_t pid,ppid,pidd; //定义父子线程标记变量
	char *buf, *r_addr, *w_addr, *temp, *time_str;//="\0"; //定义临时存储区
	struct sockaddr_in their_addr; //定义地址结构
	key_t shmid;
//	int checkpoint_freq = 60;
	int backup = 0;
	char backup_host[MAXHOST]="192.168.0.0";
	char opt;
	pthread_t tid;
	
	
	signal(SIGCHLD,sigchld_handler);
	while((opt = getopt(argc,argv,"hB:f:"))!= -1){
        switch(opt){
            case 'h':
            	printf("\t-h\t\tprint this message\n");
                printf("\t-B <host>\tassign backup server host.Default none.\n");
                printf("\t-f <frequency>\tconfig checkpointing interval(60 secs by defalut)\n");
                return 0;
            case 'B':
                backup = 1;
                strncpy(backup_host,optarg,MAXHOST);
                break;
            case 'f':
                // max hb 5 mins
                checkpoint_freq = atoi(optarg) > 300 ? 300 :atoi(optarg);
                break;
            default:
                printf("check %s -h for help\n",argv[0]);
                return 0;

        }
    }
	shmid = shm_create(); //创建共享存储区

	temp = (char *)malloc(255);
	time_str=(char *)malloc(20);
	sockfd = bindPort(MYPORT);//绑定端口
	int out = open(pathName, O_RDWR | O_CREAT| O_APPEND, S_IRWXU); //open record file
//	char *start = "Records:\n";
	write(out, '\n', strlen(start));
	pthread_create(&tid, NULL, thread, backup_host);
	
	signal(SIGINT,sigint_handler);
		while(1)
		{ 		
		if(listen(sockfd,BACKLOG) == -1)//在指定端口上监听
		{
			perror("listen");
			exit(1);
		}
		printf("listening......\n");
		sin_size = (socklen_t)sizeof(struct sockaddr*);
		if((clientfd = accept(sockfd,(struct sockaddr*)&their_addr,&sin_size)) == -1)//接收客户端连接
		{
			perror("accept");
			kill(-pidd,SIGKILL);
			exit(1);
		}
		printf("accept from:%s\n",inet_ntoa(their_addr.sin_addr));
		send(clientfd,WELCOME,strlen(WELCOME),0);//发送问候信息
		buf = (char *)malloc(255);

		ppid = fork();//创建子进程
		if(ppid == 0)
		{
		//printf("ppid=0\n");
			pid = fork(); //创建子进程 
			while(1)
			{
				if(pid > 0)
				{
				//父进程用于接收信息
					memset(buf,0,255);
				//printf("recv\n");
				//sleep(1);
					if((recvbytes = recv(clientfd,buf,255,0)) <= 0)
					{
						perror("recv1");
						close(clientfd);
						kill(-pid,SIGKILL);
						exit(1);
					}
				//write buf's data to share memory
					w_addr = shmat(shmid, 0, 0);
					memset(w_addr, '\0', 1024);
					strncpy(w_addr, buf, 1024);
					get_cur_time(time_str);
					strcat(buf,time_str);
//					printf(" %s\n",buf);
					if (strcmp(w_addr, "Are you alive?") != 0) {
						strcat(buf, "\n");
						write(out, buf, strlen(buf)); //write records into file
					}

				}
				else if(pid == 0)
				{
				//子进程用于发送信息
				//scanf("%s",buf);
					
					sleep(1);
					r_addr = shmat(shmid, 0, 0);
				//printf("---%s\n",r_addr);
				//printf("cmp:%d\n",strcmp(temp,r_addr));
					if(strcmp(temp,r_addr) != 0)
					{
						//char *mess = strchr(r_addr, ':');
						
						if (strcmp(r_addr, "Are you alive?") == 0) {
							//printf("%s\n", r_addr);
							//strcpy(temp, "I am alive!");
							strcpy(r_addr, "I am alive!");

							//strcpy(temp,r_addr);
						}
						else {
								
								//printf("common message\n");
								get_cur_time(time_str); 
								strcat(r_addr,time_str);	
							}
						//strcpy(temp,r_addr);
//						printf("%s\n", r_addr);
					//printf("discriptor:%d\n",clientfd);
					//if(send(clientfd,buf,strlen(buf),0) == -1)
						if(send(clientfd,r_addr,strlen(r_addr),0) == -1)
						{
							perror("send");
						}
						memset(r_addr, '\0', 1024);
						strcpy(r_addr,temp);
					}
					
					//used to send checkpoint
						
				}
				else
				perror("fork");
			}
		}
	}
	printf("------------------------------\n");
	free(buf);
	free(temp);
	free(time_str);
	close(sockfd);
	close(clientfd);
	close(out);
	return 0;
}

void sigint_handler(){
	kill(-getpid(),SIGINT);
	printf("SIGINT sent to child\n");
	while ((waitpid(-1,NULL,0))>0);
	exit(1);
}

void sigchld_handler()
{
	while ((waitpid(-1,NULL,0))>0);
}

void *thread(void *vargp){
		int fd;
		char *ck_buf;
		int port = 3491;
		int sendbytes;
		struct hostent *host;
		struct sockaddr_in clientaddr;//定义地址结构
		
		pthread_detach(pthread_self());
		if(signal(SIGALRM,sigalrm_handler) == SIG_ERR){
            perror("checkpoint initialization");
            exit(1);
        }
        host = gethostbyname((char*)vargp);
		if((fd = socket(AF_INET,SOCK_STREAM,0)) == -1){ //创建客户端套接字
				perror("socket\n");
				exit(1);
		}
		//绑定客户端套接字
		clientaddr.sin_family = AF_INET;
		clientaddr.sin_port = htons((uint16_t)port);
		clientaddr.sin_addr = *((struct in_addr *)host->h_addr);
		bzero(&(clientaddr.sin_zero),0);
		printf("Connectting...\n");
		while(connect(fd,(struct sockaddr *)&clientaddr,sizeof(struct sockaddr)) == -1){ //连接服务端
//			perror("connect");
//			exit(1);
		}
		if((ck_buf = (char *)malloc(BUFSIZE * sizeof(char))) == NULL){
			perror("malloc");
			exit(1);
		}
		/*
		if(recv(fd,ck_buf,BUFSIZE,0) == -1)
		{
			perror("recv");
			exit(1);
		}
		*/
		ck_ready = 0;
		alarm(checkpoint_freq);
		while(1) {
		if (ck_ready == 1) {
			ck_ready = 0;
			alarm(checkpoint_freq);
			printf("History:\n");
			char linebuf[1024];
			FILE *fp = fopen(pathName, "r");
			memset(ck_buf,0,BUFSIZE);
			while(fgets(linebuf, 1024, (FILE *)fp)) {
				strcat(ck_buf, linebuf);
			}
			if((sendbytes = send(fd,ck_buf,strlen(ck_buf),0)) == -1){
            perror("send\n");
            //break;
           	}
			printf("%s\n", ck_buf);
			fclose(fp);
			
			}
			
		}
		free(ck_buf);

		}
