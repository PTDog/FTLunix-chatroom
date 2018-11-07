/* 18749 project Client version 3 by Yan 
 * implemented heartbeat and acknowledgement checking 
 * via SIGALRM and alarm()
 * 11/7/2018 14:08
 */
#include <stdio.h>
#include <netinet/in.h> //定义数据结构sockaddr_in
#include <sys/socket.h> //提供socket函数及数据结构
#include <sys/types.h> //数据类型定义
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#define MAXNAME 32//maximum name characters
#define MAXHOST 32//maximum host name characters
#define BUFSIZE 1024

//int heartbeat_itv = 60; //default heartbeat interval

/* heartbeat ready flag, set in sigalrm handler */
volatile sig_atomic_t hb_ready = 0; 


/* SIGALRM signal handler */
void sigalrm_handler();

int main(int argc, char *argv[])
{
    struct sockaddr_in clientaddr;//定义地址结构
    pid_t pid;
    int clientfd,sendbytes,recvbytes;//定义客户端套接字
    struct hostent *host;
    
    char opt;
    
    char *buf,*msg_in; // send/receive buf and input buf
    char hostname[MAXHOST]={"192.168.0.0"};
    char chatname[MAXNAME]={"Client"};
    
    /* heartbeat relevant */
    int heartbeat_itv = 60; //default heatbeat interval (secs)
    /* acknowledgement requirement flag, increment after 
     * sending heartbeat, reset on received acknowledgement */
    int ack_request = 0;
    
    int port = 3490;
    strcpy(chatname,"Client");
    while((opt = getopt(argc,argv,"hH:p:n:b:"))!= -1){
        switch(opt){
            case 'h':
                printf("\t-h\t\tprint this message\n");
                printf("\t-H <host>\tdesignate server host\n");
                printf("\t-p <port>\tdesignate port number(3490 by default)\n");
                printf("\t-n <chatname>\tEnter your chat name\n");
                printf("\t-b <heartbeat>\tconfig heartbeat interval(60 secs by defalut)\n");
                return 0;
            case 'H':
                strncpy(hostname,optarg,MAXHOST);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'n':
                strncpy(chatname,optarg,MAXNAME);
                break;
            case 'b':
                heartbeat_itv = atoi(optarg);
                break;
            default:
                printf("check %s -h for help\n",argv[0]);
                return 0;
        
        }
    }
    
    host = gethostbyname(hostname);
    if((clientfd = socket(AF_INET,SOCK_STREAM,0)) == -1){ //创建客户端套接字
        perror("socket\n");
        exit(1);
    }
    //绑定客户端套接字
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons((uint16_t)port);
    clientaddr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(clientaddr.sin_zero),0);
    printf("Connecting...\n");
    if(connect(clientfd,(struct sockaddr *)&clientaddr,sizeof(struct sockaddr)) == -1){ //连接服务端
        perror("");
        exit(1);
    }
    if((buf = (char *)malloc(BUFSIZE * sizeof(char))) == NULL){
        perror("malloc");
        exit(1);
    }
    if((msg_in = (char *)malloc(BUFSIZE * sizeof(char))) == NULL){
        perror("malloc");
        exit(1);
    }

    if( recv(clientfd,buf,BUFSIZE,0) == -1)
    {
        perror("recv");
        exit(1);
    }
    printf("\n%s\n",buf);
    /* 创建子进程 */
    if((pid = fork()) == -1){
        perror("fork");
        exit(1);
    }
//    install sigalm handler for child process. 
    if (pid == 0){
        if(signal(SIGALRM,sigalrm_handler) == SIG_ERR){
            perror("Heartbeat initialization");
            exit(1);
        }
        alarm(heartbeat_itv); //set timer
    } 
    while(1){
        if(pid){
        //父进程用于发送信息

//        get_cur_time(time_str);
        
            memset(buf,0,BUFSIZE);
            strcpy(buf,chatname);
            strcat(buf,":");
            fgets(msg_in,BUFSIZE,stdin);
            strncat(buf,msg_in,strlen(msg_in)-1);
            //strcat(buf,time_str);
            //printf("---%s\n",buf);
            if((sendbytes = send(clientfd,buf,strlen(buf),0)) == -1){
                perror("send\n");
                //kill child
                if(!kill(pid,SIGINT)){
                    perror("Failed to terminate child process");
                }
                break;
            }
        }
        else{
        //子进程用于接收信息以及收发心跳
            memset(buf,0,BUFSIZE);
            if(hb_ready == 1){
                strcpy(buf,"Are you alive?");
//                printf("sending\n");
                if((sendbytes = send(clientfd,buf,strlen(buf),0)) == -1){
                    perror("send\n");
                }
//                printf("hb sent\n");
                memset(buf,0,BUFSIZE);
                /* heartbeat sent but acknowledgement not received*/
                if(ack_request > 1)
                    printf("Server unreachable\n");
                ack_request += 1;
                hb_ready = 0;
                alarm(heartbeat_itv); //reset timer
            }
            if(recv(clientfd,buf,BUFSIZE,MSG_DONTWAIT) <= 0){
                /* normally continue if no msg received*/
                if(errno == EAGAIN)
                    continue;
                
                perror("recv");
                //kill parent
                if(!kill(getppid(),SIGINT)){
                    perror("Failed to terminate parent process:");
                }
                break;
            }
            /* acknowledgement received */
            if(!strcmp(buf,"I am alive!")){
                ack_request = 0;
            }
            printf("%s\n",buf);
        }
    }
    free(buf);
    free(msg_in);
    close(clientfd);
    return 0; 
}
/* SIGALRM signal handler, set hb_ready when alarmed */
void sigalrm_handler(){
//    alarm(heartbeat_itv);
    hb_ready = 1;
    return;
}

