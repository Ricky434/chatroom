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

//USARE QUESTO INVECE DI error
#define handle_error_en(en, msg) \
       { errno = en; perror(msg); exit(EXIT_FAILURE); }

void error(char *msg);
void *leggi_chat(void *sockfd);

//DEVO CREARE UN THREAD PER RICEVERE+LOG E UNO PER MANDARE + probabilmente fare un fork per avere i messaggi ricevuti a parte


int main(int argc, char *argv[])
{
    //creo la variabile per il file descriptor del socket
    int sockfd;

    //preparo il buffer per mandare messaggi
    char sendBuff[1024];

    //creo la variabile delle informazioni del socket del server
    struct sockaddr_in serv_addr;
 
    //creo la variabile per salvare l'id del thread che verra' creato
    pthread_t thread_id;

    //se il programma viene chiamato senza specificare indirizzo e porta del server il programma termina spiegando cosa inserire
    if(argc != 3) {
        //MAGARI TRASFORMARLO IN ERROR
        printf("\n Usage: %s <ip of server> <port>\n",argv[0]);
        return 1;
    }

    //"pulisco" il buffer e serv_addr
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(&serv_addr, 0, sizeof(serv_addr)); 

    //creo il socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
       error("ERROR opening socket");

    //imposto la famiglia e la porta al serv_addr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));

    //trasformo l'indirizzo dato come argomento nel giusto formato e lo assegno al serv_addr
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    //provo a connettermi al sever e lancio un errore in caso di fallimento
    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR failed connecting");

    //creo un thread che legge costantemente i messaggi che arrivano dal server
    pthread_create(&thread_id, 0, &leggi_chat, &sockfd);

    //invio e ricevo messaggi al server finche' non premo invio senza aver scritto niente 
    //DA FARE: capire bene come gestire la cosa di omettere l'ultimo byte dei messaggi come fa il prof negli esempi
    while (read(0, sendBuff, sizeof(sendBuff)) > 1){
        //mando il messaggio
        write(sockfd, sendBuff, strlen(sendBuff)); //error handling?

        //pulisco il buffer
        memset(sendBuff, 0, sizeof(sendBuff));
    }
    return 0;
}

//FARE IN MODO CHE CI SIA UN THREAD CHE ASCOLTA SEMPRE E PRINTA I MESSAGGI RICEVUTI SIA SU STDOUT CHE SU LOG, E UN THREAD CHE RICEVE IN INPUT I MESSAGGI DA INVIARE AL SERVER
//aggiungere commenti
void *leggi_chat(void *sockfd)
{
    int n;
    char recvBuff[1024];
    memset(recvBuff, 0, sizeof(recvBuff));

    while (1) {
        //ricevo il messaggio
        n = read(*(int *)sockfd, recvBuff, sizeof(recvBuff)-1); //error handling?

        //SERVE? aggiungo un carattere di fine stringa al messaggio
        recvBuff[n] = 0;

        //FPUTS O WRITE? SERVE ERRORE? scrivo su sdout il messaggio ricevuto
        if(fputs(recvBuff, stdout) == EOF)
        {
            printf("\n Error : Fputs error\n");
        }

        memset(recvBuff, 0,sizeof(recvBuff));
    }
}

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}