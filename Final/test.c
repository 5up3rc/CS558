#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define MONITOR_RESULT "monitoroutput.txttmp"

int main(int argc, char **argv)
{
    char *type, *msg;
    FILE *fp;
    if(argc == 3){
        type = argv[1];
        msg = argv[2];
        
        fp=fopen(CMD_RESULT,"a+");
        fwrite(type, strlen(type), 1, fp);
        fwrite(msg, strlen(msg), 1, fp);
    }
}