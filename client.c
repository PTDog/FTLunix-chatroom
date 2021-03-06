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
    char hostname[MAXHOST]="192.168.0.0";
    char backup_host[MAXHOST]="192.168.0.0";
    char chatname[MAXNAME]="Client";

    /* heartbeat relevant */
    int heartbeat_itv = 60; //default heatbeat interval (secs)
    /* acknowledgement requirement flag, increment after
     * sending heartbeat, reset on received acknowledgement */
    int ack_request = 0;
    int verbose = 0;
    int backup = 0; // backup existence flag, set if a backup server is assigned

    int port = 3490;
    strcpy(chatname,"Client");
    while((opt = getopt(argc,argv,"hH:B:p:n:b:v"))!= -1){
        switch(opt){
            case 'h':
                printf("\t-h\t\tprint this message\n");
                printf("\t-H <host>\tdesignate server host\n");
                printf("\t-B <host>\tassign backup server host.Default none.\n");
                printf("\t-p <port>\tdesignate port number(3490 by default)\n");
                printf("\t-n <chatname>\tEnter your chat name\n");
                printf("\t-b <heartbeat>\tconfig heartbeat interval(60 secs by defalut)\n");
                printf("\t-v\tbeing verbose,print out heartbeat related infos\n");
                return 0;
            case 'H':
                strncpy(hostname,optarg,MAXHOST);
                break;
            case 'B':
                backup = 1;
                strncpy(backup_host,optarg,MAXHOST);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'n':
                strncpy(chatname,optarg,MAXNAME);
                break;
            case 'b':
                // max hb 5 mins
                heartbeat_itv = atoi(optarg) > 300 ? 300 :atoi(optarg);
                break;
            case 'v':
                verbose = 1;
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
    if (pid){
        if(signal(SIGALRM,sigalrm_handler) == SIG_ERR){
            perror("Heartbeat initialization");
            exit(1);
        }
        alarm(1); //send heartbeat almost immediately
    }
    while(1){
        if(!pid){
        //子进程用于发送信息

            memset(buf,0,BUFSIZE);
            strcpy(buf,chatname);
            strcat(buf,":");
            fgets(msg_in,BUFSIZE,stdin);
            strncat(buf,msg_in,strlen(msg_in)-1);
            if((sendbytes = send(clientfd,buf,strlen(buf),0)) == -1){
                perror("send\n");
                break;
            }
        }
        else{
        //父进程用于接收信息以及收发心跳
            signal(SIGPIPE, SIG_IGN);
            memset(buf,0,BUFSIZE);
            if(hb_ready == 1){
                strcpy(buf,"Are you alive?");
                if((sendbytes = send(clientfd,buf,strlen(buf),0)) == -1){
                    // perror("send\n");
                }
//                printf("hb sent\n");
                memset(buf,0,BUFSIZE);
                /* heartbeat sent but acknowledgement not received*/
                if(ack_request == 2)
                    printf("Server unreachable, trying to reconnect...\n");
                /* notify very 100 heartbeat interval */
                ack_request = (ack_request < 5)? ack_request + 1 : ack_request;
                /* timeout, bring backup */

                if(backup && ack_request == 5){
                    printf("Connect to backup server %s in  ",backup_host);
                    for(int i = 5;i >= 0;i--){
                        sleep(1);
                        printf("\b%d",i);
                        fflush(stdout);
                    }
                    printf("\n");
                    if(kill(pid,SIGINT) == -1){ //kill parent
                        perror("Failed to terminate parent process");
                    }
                    // restarting client with new host
                    char _hb[4];
                    snprintf(_hb,4,"%d",heartbeat_itv);
                    char *myargv[60]={argv[0],"-H",backup_host,"-b",_hb,NULL};
                    free(buf);
                    free(msg_in);
                    close(clientfd);
                    if(execve(argv[0], myargv, 0)<0)
                        perror("Bringing backup");
                    exit(0);
                }
                hb_ready = 0;
                alarm(heartbeat_itv); //reset timer
            }
            if(recv(clientfd,buf,BUFSIZE,MSG_DONTWAIT) <= 0){
                /* normally continue if no msg received*/
                if(errno == EAGAIN)
                    continue;
                continue;
            }
            /* acknowledgement received */
            if(!strcmp(buf,"I am alive!")){ //temporarily "get rid of" unexpected time stamp
                ack_request = 0;
                if(verbose)
                printf("%s\n",buf);
            }
            else
            {
                hb_ready = 0;
                alarm(heartbeat_itv); //reset timer
                printf("%s\n",buf);
            }
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
