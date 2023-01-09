/*
Clientout riceve i messaggi messi da client sulla fifo e li stampa sul terminale.
Va lanciato da un altro terminale rispetto a client, in modo da separare input e output e quindi non rischiare di sovrascrivere il messaggio che si sta scrivendo con i messaggi che arrivano dal server
*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#define MAX_MSG_LEN 512

int main(int argc, char *argv[])
{
    int fd; //file descriptor della fifo
    char recvBuff[MAX_MSG_LEN]; //buffer del messaggio da leggere dalla fifo

    //pulisco il buffer
    memset(recvBuff, 0, sizeof(recvBuff));
    
    //provo ad aprire la fifo, se non ci riesco probabilmente e' perche' ho avviato prima clientout di client e quindi non e' ancora stata creata dal client
    if ((fd = open("clientfifo", O_RDONLY)) == -1) {
        perror("ERROR opening fifo file");
        printf("Avvia prima il client e poi %s\n", argv[0]);
        exit(1);
    }

    while (1) {
        //ricevo il messaggio
        if (read(fd, recvBuff, sizeof(recvBuff)) <= 0) {
            printf("Chat terminated\n"); //se non riesce a leggere il messaggio e' perche' la pipe e' stata chiusa dal client e quindi la chat e' terminata
            close(fd);
            exit(0);
        }

        //stampa a schermo il messaggio ricevuto
        printf(recvBuff);

        //pulisci il buffer
        memset(recvBuff, 0,sizeof(recvBuff));
    }
}