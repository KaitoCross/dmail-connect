#include <iostream>
#include <cstdio>
#include <exception>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include<arpa/inet.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>
#include <sys/stat.h>
#include <syslog.h>
#include <mutex>
#include <chrono>
#include <err.h>
#include <errno.h>
#define BUFLEN 10

using namespace std;

void udp_listens_locally(int *sockfd, int* timeshift, bool* endMyLife, bool* mailsent, mutex* mTimeshift, mutex* mutMailArr)
{
    *sockfd = socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in serv, client;
    int olength=sizeof(client);
    serv.sin_family = AF_INET;
    serv.sin_port = htons(1337);
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");
    socklen_t l = sizeof(client);
    socklen_t m = sizeof(serv);
    char buffer[10] = "";
    bind(*sockfd, (struct sockaddr*)&serv, sizeof(serv));
    char *ptr;
    while(*endMyLife == false){
        recvfrom(*sockfd,buffer,10,0,(struct sockaddr *)&client,&l);
        if (strncmp(buffer, "REQTO", 5) == 0) {
            //cout << "RECEIVED TIME REQUEST\n";
            mTimeshift->lock();
            sprintf(buffer, "RCVTO %04d", *timeshift);
            mTimeshift->unlock();
            int sent = (int)sendto(*sockfd, buffer, 10, 0, (struct sockaddr *) &client, m);
        }
        if (strncmp(buffer, "MAILD", 5) == 0) {
            //cout << "MAIL PROCESSED\n";
            mutMailArr->lock();
            *mailsent=true;
            mutMailArr->unlock();
            strcpy(buffer,"REKKT");
        }
    }
}

void tcp_writes_globally(int *new_socket, int* timeshift, bool *endMyLife, bool* mailsent,bool* repliedToKeepalive, bool* conndead, mutex* mTimeshift, mutex* mutMailArr, mutex* connDeadMutex)
{
    typedef chrono::high_resolution_clock timerexact;
    auto t2 = timerexact::now();
    auto t1 = timerexact::now();
    bool connDeadtemp=true;
    bool killProgram=false;
    int errorcode=1337;
    int timeouts = 0;
    /**bool conndead = false;*/
    char buffer[10]="";

    do {
        connDeadMutex->lock();
        connDeadtemp=*conndead;
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        while (!killProgram && !connDeadtemp) {
            mutMailArr->lock();
            if (*mailsent) {
                send(*new_socket, "MAILD", strlen("MAILD"), 0);
                *mailsent = false;
            }
            mutMailArr->unlock();
            //check for timeout - keepalive packet
            t1 = timerexact::now();
            long seconds_passed = chrono::duration_cast<std::chrono::seconds>(t1-t2).count();
            if (seconds_passed >= 5)
            {
                connDeadMutex->lock();
                if (!*repliedToKeepalive)
                {
                    timeouts++;
                    if(timeouts>3) {
                        *conndead = true;
                        /*cout << */ syslog(REG_ERR,"CONNECTION LOST - NO REPLY");
                        timeouts = 0;
                    }
                } else {
                    *repliedToKeepalive = false;
                }
                connDeadMutex->unlock();
                int tempo=(int)send(*new_socket, "ELPSY", strlen("ELPSY"),0);
                if (tempo<1)
                {
                    errorcode=errno;
                    syslog(REG_ERR,"TCP KEEP ALIVE ERROR %d",errorcode);
                    //perror("send");
                    //cout << " BYTES SENT "<<tempo<<"\n TIMEOUT CHECK ERRNO "<< errorcode << "\n" << strerror(errorcode) <<" " << stderr <<"\n";
                    if (errorcode >= 100)
                    {
                        shutdown(*new_socket, SHUT_RDWR);
                        close(*new_socket);
                        connDeadMutex->lock();
                        *conndead = true;
                        connDeadMutex->unlock();
                        /*cout <<*/ syslog(REG_ERR,"CONNECTION LOST - KEEP ALIVE FAILED");
                    }
                }
                t2=timerexact::now();
            }
            connDeadtemp=*conndead;
            killProgram=*endMyLife;
            connDeadMutex->unlock();
        }
        connDeadMutex->lock();
        killProgram=*endMyLife;
        connDeadMutex->unlock();
    }while (killProgram==false);
}

void stopdaemon(int sockfd[], int sockfdamount, int *connfd, bool* endMyLife, thread* firstThread, thread* secondThread, thread* thirdThread)
{
    shutdown(*connfd,SHUT_RDWR);
    for (int i = 0; i < sockfdamount; i++) {
        shutdown(sockfd[i],SHUT_RDWR);
    }
    sleep(2);
    close(*connfd);
    for (int i = 0; i < sockfdamount; i++) {
        close(sockfd[i]);
    }
    *endMyLife = true;
    firstThread->join();
    secondThread->join();
    thirdThread->join();
    /*cout << */syslog(LOG_NOTICE,"SHUTDOWN FINISHED");
}

void tcp_reads_global(int *new_socket, int* timeshift, bool *endMyLife, bool* mailsent, bool* repliedToKeepalive, bool* conndead, mutex* mTimeshift, mutex* mutMailArr, mutex* connDeadMutex)
{
    bool connDeadTemp = false;
    bool killProgram=false;
    char buffer[10]="";
    char buffer2[10]="";
    int whaterror=1337;
    do {
        connDeadMutex->lock();
        connDeadTemp=*conndead;
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        while (!connDeadTemp && !killProgram) {
            int valread = (int) read(*new_socket, buffer, 10);
            if (valread == 0) {
                connDeadMutex->lock();
                *conndead = true;
                connDeadMutex->unlock();
                /*cout << */syslog(LOG_NOTICE,"CLIENT DISCONNECTED, 0 PACKET");
                shutdown(*new_socket,SHUT_RDWR);
                close(*new_socket);
            }
            if (strncmp(buffer, "RCVTO", 5) == 0) {
                char *endOfBuffer = &buffer[10];
                mTimeshift->lock();
                *timeshift = (int) strtol(buffer + 6, &endOfBuffer, 10);
                /*cout <<*/ syslog(LOG_NOTICE,"RECEIVED NEW TIME");
                mTimeshift->unlock();
                sprintf(buffer2,"RCVOK");
                int rcvok = (int)send(*new_socket, buffer2, strlen(buffer2), 0);
                if (rcvok==-1)
                {
                    connDeadMutex->lock();
                    *conndead = true;
                    connDeadMutex->unlock();
                    whaterror = errno;
                    //cout << stderr << "STDERR\n";
                    /*cout << */syslog(REG_ERR,"RECEIVE NOT OK ERRNO %d",whaterror);
                }
            }
            if (strncmp(buffer, "SHTDW", 5) == 0) {
                int bytes_sent = (int)send(*new_socket, "SDOWN", strlen("SDOWN"), 0);
                connDeadMutex->lock();
                *endMyLife = true;
                connDeadMutex->unlock();
            }
            if (strncmp(buffer, "ELPSY", strlen("ELPSY")) == 0) {
                int bytes_wtf = (int)send(*new_socket, "KONGROO", strlen("KONGROO"), 0);
            }
            if (strncmp(buffer, "KONGROO", strlen("KONGROO")) == 0) {
                connDeadMutex->lock();
                *repliedToKeepalive=true;
                connDeadMutex->unlock();
            }
            connDeadMutex->lock();
            connDeadTemp=*conndead;
            killProgram=*endMyLife;
            connDeadMutex->unlock();
        }
        connDeadMutex->lock();
        killProgram=*endMyLife;
        connDeadMutex->unlock();
    } while (!killProgram);
}

void do_heartbeat()
    {
        int setbackHours = 0;
        bool endMyLife = false;
        bool mailed = false;
        bool conndead = true;
        bool ClientAliveConfirmed = false;
        int new_socket = 0;
        int socketfd[2];
        mutex mArrive;
        mutex mutSetbackHours;
        mutex mutConnDead;
        thread udplocal(udp_listens_locally, &socketfd[0], &setbackHours, &endMyLife, &mailed, &mutSetbackHours,
                        &mArrive);
        thread tcpglobal(tcp_writes_globally, &new_socket, &setbackHours, &endMyLife, &mailed, &ClientAliveConfirmed,
                         &conndead, &mutSetbackHours, &mArrive, &mutConnDead);
        thread tcpreadglobal(tcp_reads_global, &new_socket, &setbackHours, &endMyLife, &mailed, &ClientAliveConfirmed,
                             &conndead, &mutSetbackHours, &mArrive, &mutConnDead);

        struct sockaddr_in server;
        socketfd[1] = socket(AF_INET, SOCK_STREAM, 0);
        int trueFlag = 1;
        setsockopt(socketfd[1], SOL_SOCKET, SO_REUSEADDR, &trueFlag, sizeof(int));
        setsockopt(socketfd[1], SOL_SOCKET, SO_REUSEPORT, &trueFlag, sizeof(int));
        if (socketfd[1] < 0) {
            syslog(REG_ERR, "CANT CREATE SOCKET!");
            return;
        }
        int addrlen = sizeof(server);
        /**bool conndead = false;*/
        char buffer[10] = "";
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port = htons(4242);
        syslog(LOG_NOTICE, "ABOUT TO BIND TCP");
        int bindsuc = (int) bind(socketfd[1], (struct sockaddr *) &server, addrlen);
        if (bindsuc < 0) {
            syslog(REG_ERR, "ERROR BINDING %d", bindsuc);
            int errcode = errno;
            mutConnDead.lock();
            conndead = false;
            endMyLife = true;
            mutConnDead.unlock();
            syslog(REG_ERR, "ERRNO %d", errcode);
            return;
        } else {
            syslog(LOG_NOTICE, "BOUND TCP");
        }
        int listensuc = listen(socketfd[1], 1);
        if (listensuc) {
            syslog(REG_ERR, "ERROR LISTENING %d", listensuc);
            return;
        } else {
            syslog(LOG_NOTICE, "LISTENING TCP");
        }
        do {
            int i = 1;
            sleep(1);
            //mutConnDead.lock();
            while (conndead) {
                new_socket = accept(socketfd[1], (struct sockaddr *) &server, (socklen_t *) &addrlen);
                syslog(LOG_NOTICE, "CONNECTION ESTABLISHED!");
                mutConnDead.lock();
                conndead = false;
                ClientAliveConfirmed = true;
                mutConnDead.unlock();
            }
            //mutConnDead.unlock();
        } while (endMyLife == false);
        endMyLife = true;
        stopdaemon(socketfd, 2, &new_socket, &endMyLife, &udplocal, &tcpglobal, &tcpreadglobal);
    }

// For security purposes, we don't allow any arguments to be passed into the daemon
int main(void)
{
    // Define variables
    pid_t pid, sid;

    // Fork the current process
    pid = fork();
    // The parent process continues with a process ID greater than 0
    if(pid > 0)
    {
        exit(EXIT_SUCCESS);
    }
        // A process ID lower than 0 indicates a failure in either process
    else if(pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    // The parent process has now terminated, and the forked child process will continue
    // (the pid of the child process was 0)

    // Since the child process is a daemon, the umask needs to be set so files and logs can be written
    umask(0);

    // Open system logs for the child process
    openlog("dmail-connector", LOG_NOWAIT | LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Successfully started dmail-connect");

    // Generate a session ID for the child process
    sid = setsid();
    // Ensure a valid SID for the child process
    if(sid < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR, "Could not generate session ID for child process");

        // If a new session ID could not be generated, we must terminate the child process
        // or it will be orphaned
        exit(EXIT_FAILURE);
    }

    // Change the current working directory to a directory guaranteed to exist
    if((chdir("/")) < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR, "Could not change working directory to /");

        // If our guaranteed directory does not exist, terminate the child process to ensure
        // the daemon has not been hijacked
        exit(EXIT_FAILURE);
    }

    // A daemon cannot use the terminal, so close standard file descriptors for security reasons
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Daemon-specific intialization should go here
    const int SLEEP_INTERVAL = 5;

        //execute daemon heartbeat - infinite loop in function
    do_heartbeat();


    // Close system logs for the child process
    syslog(LOG_NOTICE, "Stopping dmail-connect");
    closelog();

    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}