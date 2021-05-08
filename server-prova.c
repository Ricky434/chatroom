#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 

/* Per lanciarli: prima ./server, poi ./client localhost 123456 */

int main(int argc, char *argv[])
{
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr; 

    char sendBuff[1025];
    time_t ticks; 

    char recvBuff[1024];
    int nRead;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0,sizeof(recvBuff));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(42069); /*converte uno short int in formato network
										utile per numeri di porta*/

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 10); 

    while(1)
    {
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); /* non ci interessa
							il descrittore del nuovo  socket creato e quindi
							passiamo NULL come indirizzo e come  dimensione  della 
							struttura dati */ 
        
        while ( (nRead = read(connfd, recvBuff, sizeof(recvBuff)-1)) > 0)
        {
            recvBuff[nRead] = 0;
            ticks = time(NULL);
            snprintf(sendBuff, sizeof(sendBuff), "%.24s: ", ctime(&ticks));
            strncat(sendBuff, recvBuff, sizeof(sendBuff)-strlen(sendBuff)-1);
            strcat(sendBuff, "\n");
            write(connfd, sendBuff, strlen(sendBuff)); 
        }

        close(connfd);
        sleep(1);
     }
}
