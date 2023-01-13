/*
Il client invia messaggi al server nel formato <timestamp:messaggio>, in cui il timestamp e' il numero di secondi passati dall'epoch.
Inoltre riceve tutti i messaggi inviati dal server e li passa a clientout tramite fifo per stamparli sul terminale
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_MSG_LEN 512 //lunghezza massima del messaggio
#define MAX_UNAME_LEN 24 //lunghezza massima dell'username (senza includere il carattere finale \0)
#define EXIT_CMD "!exit" //comando usato da un utente per disconnettersi dal server
#define EXIT_CMDR "!exit\n" //comando usato da un utente per disconnettersi dal server piu' il carattere di ritorno

int sockfd; //variabile per il file descriptor del socket, globale cosi` puo` essere usata dall'handler del sigaction

//====FUNZIONI====

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void quit()
{
    write(sockfd, EXIT_CMD, 5); //invia al server il comando di disconnessione
    close(sockfd); //chiudi il socket del server
    exit(0);
}

//====THREADS====

//riceve continuamente i messaggi inviati dal server
void *leggi_chat()
{
    int fd; //file descriptor della fifo per comunicare con clientout
    char recvBuff[MAX_MSG_LEN]; //buffer per il messaggio da ricevere
    struct stat res; //risultato della stat

    //pulisco il buffer per il messaggio da ricevere
    memset(recvBuff, 0, sizeof(recvBuff));

    if (stat("clientfifo", &res)) { //controlla se gia esiste un file chiamato clientfifo
        if (mkfifo("clientfifo", S_IRUSR | S_IWUSR) == -1 ) //se non esiste provo a creare la fifo
                error("ERROR creating fifo");
    }
    else if (!S_ISFIFO(res.st_mode)) { //se esiste controlla se il file che esiste gia e' una fifo o no
        fprintf(stderr, "File already exists and is not a named pipe\n"); //se non e' una fifo termina con errore
        exit(5);
    }
    
    //apro la fifo
    fd = open("clientfifo", O_WRONLY);
    if (fd < 0) {
        error("ERROR opening fifo");
    }

    while (1) {
        //ricevo il messaggio
        if ((read(sockfd, recvBuff, sizeof(recvBuff))) < 0) { 
            printf("\rERROR reading from server, it might be down\n");
            exit(1);
        }

        //scrivo sulla fifo il messaggio ricevuto
        if(write(fd, recvBuff, sizeof(recvBuff)) < 0)
        {
            error("ERROR writing to fifo\n"); 
        }

        //pulisco il buffer per il prossimo messaggio
        memset(recvBuff, 0, sizeof(recvBuff));
    }
}

//====MAIN====

int main(int argc, char *argv[])
{
    char sendBuff[MAX_MSG_LEN]; //buffer per mandare messaggi
    char readBuff[MAX_MSG_LEN-15-MAX_UNAME_LEN]; //buffer per leggere input [il messaggio verra' inviato ai client dal server nel formato <(hh:mm:ss) [username]: messaggio>, quindi 15+MAX_UNAME_LEN caratteri (max) del messaggio vengono sprecati]
    struct sockaddr_in serv_addr; //informazioni del socket del server
    struct hostent *server; //variabile per contenere il risultato di gethostbyname
    pthread_t thread_id; //id del thread che verra' creato
    FILE *clilog; //file di log
    time_t now; //timestamp che verra' assegnato al messaggio
    struct sigaction act; //informazioni da dare al sigaction
    sigset_t set; //insieme di segnali da bloccare durante l'esecuzione del gestore

    //se il programma viene chiamato senza specificare indirizzo e porta del server il programma termina spiegando cosa inserire
    if(argc != 3) {
        printf("\n Usage: %s <ip of server> <port>\n",argv[0]);
        return 1;
    }

    //"pulisco" le locazioni di memoria
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
    server = gethostbyname(argv[1]);
    //se gethostbyname non restituisce niente vuol dire che non ha trovato il server
    if (server == NULL) {
        printf("ERROR, no such host\n");
        exit(0);
    }
    //aggiungo l'indirizzo del server alle informazioni per il socket
    strncpy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

    //provo a connettermi al sever e lancio un errore in caso di fallimento
    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR failed connecting");


    //imposto SIGTERM come segnale da bloccare durante l'esecuzione dell'handler
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);

    act.sa_flags = 0; //imposto i flags a 0
    act.sa_mask = set; //aggiungo set alle informazioni da dare al sigaction
    act.sa_handler = &quit; //imposto la funzione quit come handler

    //sigaction su SIGINT
    sigaction(SIGINT, &act, NULL);


    //creo un thread che legge costantemente i messaggi che arrivano dal server
    pthread_create(&thread_id, 0, &leggi_chat, NULL);

    //apro il file di log
    clilog = fopen("clilog.txt", "w+");

    //Scrivo il prompt per l'username sul client
    printf("Username: ");
    fflush(stdout);

    //invio e ricevo messaggi al server finche' non ricevo il comando per terminare la chat (EXIT_CMD)
    while (1){
        //leggo dallo stdin
        if (read(0, readBuff, sizeof(readBuff)-2) < 0) { //lascio due byte per lo \n e lo \0
            error("\rERROR reading input\n");
        }

        //se leggo EXIT_CMDR (il comando contiene anche il carattere di ritorno) chiudo il file di log e chiudo il programma
        if (strcmp(readBuff, EXIT_CMDR) == 0) {
            fclose(clilog);
            quit();
        }

        //se leggo solo "\n" vuol dire che l'utente ha premuto invio senza scrivere niente, quindi faccio ricominciare il ciclo riscrivendo il prompt
        if (strcmp(readBuff, "\n") == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        //se l'utente ha scritto un messaggio troppo lungo, e questo e' stato troncato, aggiungo un carattere di ritorno al messaggio
        if (readBuff[sizeof(readBuff)-3] != '\0' && readBuff[sizeof(readBuff)-3] != '\n') {
            readBuff[sizeof(readBuff)-2] = '\n';
        }

        //creo il timestamp del messaggio
        now = time(NULL);

        //scrivo il messaggio nel file di log
        fprintf(clilog, "[%.24s] %s", ctime(&now), readBuff);
        fflush(clilog);

        //creo la stringa da inviare, con formato <timestamp:messaggio>
        snprintf(sendBuff, sizeof(sendBuff), "%ld:%s", now, readBuff);

        //mando il messaggio
        if (write(sockfd, sendBuff, sizeof(sendBuff)) < 0) {
            error("\rERROR writing to server, it might be down\n");
        }

        //pulisco i buffer
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(readBuff, 0, sizeof(readBuff));

        //riscrivo il prompt
        printf("> ");
        fflush(stdout);
    }
}