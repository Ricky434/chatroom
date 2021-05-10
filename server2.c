#include <sys/socket.h> //probabilmente non serve perche incluso da in.h
#include <netinet/in.h>
//#include <arpa/inet.h>
#include <stdio.h> //IO
#include <string.h> //gestione stringhe
#include <unistd.h> //chiamate di sistema
#include <time.h> //time
#include <sys/types.h> //serve per il tipo del socket
#include <stdlib.h> //serve per gli errori (in questo caso)
#include <pthread.h> //threads
#include <signal.h> //signal handler

//utenti massimi ammessi
#define MAX_USERS 3

//informazioni da passare al thread che gestisce il client
struct clinfo {
    int *users; //utenti attualmente collegati
    int clifd; //file descriptor del client
    FILE *logfile; //file di log
    pthread_t thread_id;
};

//====THREADS====

static void *parla_con_client(void *clinfo)
{
    //FORSE 1KB E' TROPPO? inizializzo i buffer del messaggio da inviare e ricevere
    char sendBuff[1024];
    char recvBuff[1024];

    //creo una variabile per salvare il numero di byte letti
    int nRead;

    //salvo le informazioni
    struct clinfo info = *((struct clinfo *)clinfo);

    //inizializzo la variabile in cui mettere il timestamp dell messaggio
    time_t ticks;

    //"pulisco" le locazioni di memoria dei buffer dei messaggi
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));

    //TODO chiedi nome utente

    //comincio il ciclo in cui ogni volta che il client manda un messaggio, io lo leggo e lo rimando indietro con attaccato il timestamp. Quando ricevo un messaggio lungo solo un byte (per es. uno \n, cioe' quando il client preme invio senza scrivere niente) interrrompo il ciclo
    while ((nRead = read(info.clifd, recvBuff, sizeof(recvBuff))) > 1) {
        //salvo il timestamp
        ticks = time(NULL);

        //scrivo il timestamp nel messaggio da inviare
        snprintf(sendBuff, sizeof(sendBuff), "%.24s: ", ctime(&ticks));

        //attacco il messaggio ricevuto dopo il timestamp, stando attento a non andare oltre alla fine del buffer e lasciando un byte per aggiungere un carattere finale
        strncat(sendBuff, recvBuff, sizeof(sendBuff)-strlen(sendBuff)-1);

        //aggiungo un carattere di ritorno al messaggio
        strcat(sendBuff, "\n");

        fputs(sendBuff, info.logfile);
        fflush(info.logfile);

        //invio il messaggio
        write(info.clifd, sendBuff, strlen(sendBuff));

        //DA FAR DIVENTARE LOG: scrivo il messaggio anche su stdout perche' posso
        write(1, sendBuff, strlen(sendBuff)-1);

        //pulisco i buffer per inviare e ricevere il messaggio
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(recvBuff, 0, sizeof(recvBuff));
    }
    
    //una volta finito di parlare con il client chiudo la connessione

    close(info.clifd);
    *(info.users) -= 1;
    return 0;
    //exit?
}

static void *gestisci_messaggi(void *arg)
{

}

//====FUNZIONI====

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void chiudi()
{
    exit(0);
}

void accetta_client(int listenfd, int *users, FILE *logfile)
{
    struct clinfo client_info;
    int clifd;

    //preparo un messaggio da usare in caso il server sia pieno
    char message[] = "Il server e' pieno, riprovare piu' tardi";

    memset(&client_info, 0, sizeof(client_info));

    //accetto la connessione in arrivo e apro un socket con file descriptor connfd per comunicare
    clifd = accept(listenfd, (struct sockaddr*)NULL, NULL); //forse cli_addr e clilen sono inutili

    //se il server e' pieno manda un messaggio al client che sta provando a connettersi, chiudi il suo file descriptor e torna all'inizio del ciclo
    //SINCRONIZZAZIONE?
    if (*users >= MAX_USERS) {
        write(clifd, message, strlen(message));
        close(clifd);
        sleep(1);
        return;
    }

    //preparo le informazioni d
    client_info.clifd = clifd;
    client_info.users = users;
    client_info.logfile = logfile;

    //creo un thread per gestire il client appena accettato e aumento il contatore degli utenti
    pthread_create(&(client_info.thread_id), 0, &parla_con_client, &client_info);
    (*users)++;
}

//====MAIN====

int main(int argc, char *argv[])
{
    //creo le variabili dei file descriptors, del numero di utenti collegati, delle informazioni del socket del server e del client, della lunghezza del socket client e delle informazioni da passare al thread che gestisce il client
    int listenfd, users = 0;
    struct sockaddr_in serv_addr;
    FILE *logfile;

    if(argc != 2) {
        //MAGARI TRASFORMARLO IN ERROR
        printf("\n Usage: %s <port>\n",argv[0]);
        return 1;
    }

    //CAMBIARE IN SIGACTION handler CTRL+C
    signal(SIGINT, chiudi);

    //creo il socket ascoltatore e mi salvo il suo file descriptor
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) 
        error("ERROR opening socket");

    //"pulisco" le locazioni di memoria
    memset(&serv_addr, 0, sizeof(serv_addr));

    //imposto la famiglia, la porta e l'indirizzo (ANY in modo da accettare messaggi da qualunque indirizzo) al serv_addr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //associo le informazioni di serv_addr al socket
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    //metto in ascolto il socket, permettendo una coda di 10 richieste
    listen(listenfd, 10);

    //apro il file di log
    logfile = fopen("log.txt", "w+");
    if (logfile == NULL)
        error("ERRORE apertura file");

    //creo thread che si occupa della gestione dei messaggi
    //pthread_create(&msgthread_id, 0, &gestisci_messaggi, &arg);

    //il server comincia ad accettare client
    while(1) {

        accetta_client(listenfd, &users, logfile);
        sleep(1);
        //thread join dove?
    }
}