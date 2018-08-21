//
// Created by chris on 16.05.2018.
//

#ifndef OFFSETFORWARDER_SIGNALHANDLING_H
#define OFFSETFORWARDER_SIGNALHANDLING_H

#include <stdexcept>
#include <mutex>
#include <thread>
#include <semaphore.h>


using std::runtime_error;

class SignalException:public runtime_error {

public:
    SignalException(const std::string& _message)
            : std::runtime_error(_message)
    {}
};

using namespace std;

class SignalHandling
{
protected:
    static bool mbGotExitSignal;
    static bool *endMyLife;
    static bool *conndead;
    static int *new_socket;
    static int *socketfd;
    static int sockfds_open;
    static sem_t *connSem, *sendsmthSem;
    static thread *openThreads[4];

public:
    SignalHandling();
    SignalHandling(int sockfd[], int sockfdamount, int *connfd, bool* endMyLife, std::thread* firstThread, std::thread* secondThread, std::thread* thirdThread, std::thread* fourthThread, sem_t* connSema, sem_t* sendSigSem);
    ~SignalHandling();

    static bool gotExitSignal();
    static void setExitSignal(bool _bExitSignal);

    void        setupSignalHandlers();
    static void exitSignalHandler(int _ignored);


};


#endif //OFFSETFORWARDER_SIGNALHANDLING_H
