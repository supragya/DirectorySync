#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <limits.h>
#include <mutex>
#include <thread>

#define MAX_EVENTS 1024 /*Max. number of events to process at one go*/
#define LEN_NAME 16 /*Assuming that the length of the filename won't exceed 16 bytes*/
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME )) /*buffer to store the data of events*/

// Data Structures

std::mutex mtx;

enum messagetype{
    TRY_SEND,
    READY_RECIEVE,
    SENDING,
    ACK_RECIEVE
};

enum filechangetype{
    MODIFY,
    DELETE,
    CREATE
};

enum filetype{
    FILE,
    DIRECTORY
};

struct message{
    messagetype mtype,
    filechangetype fctype,
    filetype ftype,
    char filename[1000],
    unsigned long long filesize
};

void reader(int portno){
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char *hello = "Hello from server";

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                                                  &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( portno );

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address,
                                 sizeof(address))<0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
    {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    message *incoming = malloc(sizeof(message));
    message *outgoing = malloc(sizeof(message));
    char *filedata;

    while(1){
        valread = read( new_socket , incoming, sizeof(message));
        if(incoming->mtype == TRY_SEND){
            std::cout<<"[RECV] Recieved try send message from other droid... "<<std::endl;
            if(incoming->filechangetype == CREATE || incoming->filechangetype == MODIFY){
                if(incoming->filetype == DIRECTORY){
                    int dir_err = mkdir(incoming->filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    if (-1 == dir_err){
                        std::cout<<"[RECV] Error creating directory "<<incoming->filename<<std::endl;
                        exit(1);
                    }
                    break;
                }
                else{
                    filedata = malloc(sizeof(char)*incoming->filesize);
                    if(!filedata){
                        std::cout<<"[RECV] Error allocating space for transfer of "<<incoming->filename<<std::endl;
                        exit(1);
                    }
                    outgoing->mtype = READY_RECIEVE;
                    send(new_socket, outgoing , strlen(outgoing) , 0);
                    recv(new_socket, filedata, sizeof(filedata));

                    //Write the file
                    ofstream filedump(incoming->filename, ios::binary);
                    if(!filedump){
                        std::cout<<"[RECV] Error opening file "<<incoming->filename<<std::endl;
                        exit(1);
                    }
                    filedump<<filedata;
                    filedump.close();
                    free(filedata);
                }
            }
            else if(incoming->filechangetype == DELETE){
                
            }
        }
    }
    return 0;
}
void filewatcher(char *dirname);

int main( int argc, char **argv )
{

    return 0;
}

void filewatcher(char *dirname){
    int length, i = 0, wd;
    int fd;
    char buffer[BUF_LEN];

    /* Initialize Inotify*/
    fd = inotify_init();
    if ( fd < 0 ) {
        std::cout<<"Could not initialise inotify, quitting\n";
        exit(1);
    }

    /* add watch to starting directory */
    wd = inotify_add_watch(fd, dirname, IN_CREATE | IN_MODIFY | IN_DELETE);

    if (wd == -1){
        std::cout<<"Could not add watch to "<<dirname<<", quitting\n";
        exit(1);
    }
    else
        std::cout<<"[filewatcher] Started watching "<<dirname<<"\n";

    /* do it forever*/
    while(1){
        i = 0;
        length = read( fd, buffer, BUF_LEN );
        if ( length < 0 )
            perror( "read" );

        while ( i < length ) {
            struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
            if (event->len ) {
                if (event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR)
                        printf( "The directory %s was Created.\n", event->name );
                    else
                        printf( "The file %s was Created with WD %d\n", event->name, event->wd );
                    }

                if ( event->mask & IN_MODIFY) {
                    if (event->mask & IN_ISDIR)
                        printf( "The directory %s was modified.\n", event->name );
                    else
                        printf( "The file %s was modified with WD %d\n", event->name, event->wd );
                }

                if ( event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR)
                        printf( "The directory %s was deleted.\n", event->name );
                    else
                        printf( "The file %s was deleted with WD %d\n", event->name, event->wd );
                }

                i += EVENT_SIZE + event->len;
            }
        }
    }

    /* Clean up*/
    inotify_rm_watch( fd, wd );
    close( fd );

    return 0;
}
