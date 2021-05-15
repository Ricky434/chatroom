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
#include <sys/stat.h> //fifo
#include <fcntl.h> //filecontrol (open)

#define FIFOFILE "clientfifo" //necessario? ma si dai

//====FUNZIONI====
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void quit(int socket)
{
    write(socket, "!exit", 5);
    close(socket); //dovrei chiudere anche gli altri file ma non posso, comunque con exit si chiudono in teoria
    exit(0);
}
//TROVARE MODO PER SEPARARE INPUT E OUTPUT
//permettere hostnames oltre a indirizzi ip
//LOGFILE APPEND O NO?, VIENE MESSO ANCHE IL NOME NEL LOGFILE
//FORSE CONVIENE LEVARE A OGNI MESSAGGIO IL \n FINALE DA INVIARE AL SERVER PER COERENZA

//====THREADS====

//aggiungere commenti
void *leggi_chat(void *sockfd)
{
    int n, fd;
    int *socket;
    char recvBuff[1024];
    struct stat res;

    socket = sockfd;
    memset(recvBuff, 0, sizeof(recvBuff));

    //copiato e incollato spudoratamente
    if (stat(FIFOFILE, &res)) { //controlla se gia esiste una fifo
        if (mkfifo(FIFOFILE, S_IRUSR | S_IWUSR) == -1 )
                error("ERROR creating fifo");
    }
    else if (!S_ISFIFO(res.st_mode)) { //controlla se il file che esiste gia e' una fifo o no
        fprintf(stderr, "File already exists and is not a named pipe\n");
        exit(5);
    }
    
    fd = open("clientfifo", O_WRONLY); //non riesco a chiuderlo in nessun modo?

    while (1) {
        //ricevo il messaggio
        if ((n = read(*socket, recvBuff, sizeof(recvBuff)-1)) == 0) {
            printf("\rERROR reading from server, it might be down\n");
            exit(1); //vedi che error code metterci
        }

        //SERVE? aggiungo un carattere di fine stringa al messaggio
        recvBuff[n] = 0;

        //FPUTS O WRITE? SERVE ERRORE? scrivo su sdout il messaggio ricevuto
        if(write(fd, recvBuff, strlen(recvBuff)) == 0)
        {
            printf("ERROR writing to fifo\n"); //ERROR HANDLING
        }

        memset(recvBuff, 0,sizeof(recvBuff));
    }
}

//====MAIN====

int main(int argc, char *argv[])
{
    int sockfd; //creo la variabile per il file descriptor del socket
    char sendBuff[1024], readBuff[1024]; //preparo il buffer per mandare messaggi
    struct sockaddr_in serv_addr; //creo la variabile delle informazioni del socket del server
    pthread_t thread_id; //creo la variabile per salvare l'id del thread che verra' creato
    FILE *clilog;
    time_t now;

    //se il programma viene chiamato senza specificare indirizzo e porta del server il programma termina spiegando cosa inserire
    if(argc != 3) {
        //MAGARI TRASFORMARLO IN ERROR
        printf("\n Usage: %s <ip of server> <port>\n",argv[0]);
        return 1;
    }

    //"pulisco" il buffer e serv_addr
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(readBuff, 0, sizeof(readBuff));
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

    clilog = fopen("clilog.txt", "w+");

    //write(1, "Username: ", strlen("Username: "));
    printf("Username: ");
    fflush(stdout);

    //invio e ricevo messaggi al server finche' non premo invio senza aver scritto niente (da levare e trovare un altro metodo per chiudere)
    //DA FARE: capire bene come gestire la cosa di omettere l'ultimo byte dei messaggi come fa il prof negli esempi
    while (1){
        read(0, readBuff, sizeof(readBuff)); //error handling? READ O SCANF?

        if (strcmp(readBuff, "!exit\n") == 0) { //termina programma, define?
            fclose(clilog);
            quit(sockfd);
        }
        if (strcmp(readBuff, "\n") == 0) {
            printf("> "); //ripetitivo?
            fflush(stdout);
            continue;
        }

        //creo timestamp
        now = time(NULL);

        fprintf(clilog, "[%.24s] %s", ctime(&now), readBuff); //logfile
        fflush(clilog);

        //aggiungo timestamp
        snprintf(sendBuff, sizeof(sendBuff), "%ld:%s", now, readBuff);

        //levo il carattere di ritorno - mi sa che non ne vale la pena PIUTTOSTO DOVREI AGGIUNGERE \0 DOPO \n? O COME MINIMO NELL'ULTIMO BYTE DEL BUFFER PER ESSERE SICURO CHE LA STRINGA TERMINI?
        //sendBuff[strlen(sendBuff)-1] = 0; //modo piu efficace di settare l'ultimo carattere a \0?, forse e' meglio lasciare lo \n
        //mando il messaggio
        if (write(sockfd, sendBuff, strlen(sendBuff)) == 0) { //forse devo inviare tutti e 1024 i byte dek buffer, visto che il server provera' a leggerne 1024...
            printf("\rERROR writing from server, it might be down\n");
            exit(1); //vedi che error code metterci
        }

        //pulisco il buffer
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(readBuff, 0, sizeof(readBuff));

        printf("> "); //prompt
        fflush(stdout);
    }
}