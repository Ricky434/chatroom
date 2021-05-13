#include <sys/socket.h> //probabilmente non serve perche incluso da in.h
#include <netinet/in.h>
#include <arpa/inet.h> //serve per inet_pton
#include <stdio.h> //IO
#include <string.h> //gestione stringhe
#include <unistd.h> //chiamate di sistema
#include <time.h> //time
#include <sys/types.h> //serve per il tipo del socket
#include <stdlib.h> //serve per gli errori
#include <pthread.h> //threads
#include <fcntl.h> //open

#define FIFOFILE "clientfifo"

//TROVARE UN MODO PER CHIUDERSI GENTILMENTE QUANDO IL CLIENT SI CHIUDE

int main(int argc, char *argv[])
{
    int fd;
    char recvBuff[1024];

    memset(recvBuff, 0, sizeof(recvBuff));
    fd = open(FIFOFILE, O_RDONLY);

    while (1) {
        //ricevo il messaggio
        if (read(fd, recvBuff, sizeof(recvBuff)) == 0) {
            printf("ERROR reading from fifo\n"); //error handling?
            exit(1);
        }

        printf(recvBuff);

        memset(recvBuff, 0,sizeof(recvBuff));
    }
}