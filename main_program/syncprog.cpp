#include <iostream>
#include <string.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <sys/inotify.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

#define WORKCMDLEN 10000
#define MAXTRANSACTIONS 100
#define MAXFILENAMELEN  1000
#define INOTIFYBUFLEN   1000
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/

enum FileChangeType_t{
    FCT_NONE,
    MODIFY,
    DELETE,
    CREATE
};
enum FileType_t{
    FT_NONE,
    _FILE,
    DIRECTORY
};
struct message{
    int                     valid;
    char                    FileName[MAXFILENAMELEN];
    enum FileChangeType_t   FCType;
    enum FileType_t         FType;
    unsigned long           FileSize;
};
class TransactionHistory_t{
    bool    ValidTransactionHistory[MAXTRANSACTIONS];
    message TransactionHistory[MAXTRANSACTIONS];
    std::mutex mtx;
public:
    TransactionHistory_t(){
        for(int i=0; i<MAXTRANSACTIONS; i++){
            ValidTransactionHistory[i] = false;
            strcpy(TransactionHistory[i].FileName, "");
            TransactionHistory[i].FCType = FCT_NONE;
            TransactionHistory[i].FType = FT_NONE;
            TransactionHistory[i].FileSize = 0;
        }
    }
    bool is_recorded(message m){
        mtx.lock();
        bool FoundRecord = false;
        // Pass 1: Hard
        for(int i=0; i<MAXTRANSACTIONS; i++){
            if(!ValidTransactionHistory[i])
                continue;
            if( strcmp(m.FileName, TransactionHistory[i].FileName) == 0 &&
                m.FType == TransactionHistory[i].FType &&
                m.FileSize == TransactionHistory[i].FileSize &&
                m.FCType == TransactionHistory[i].FCType){
                    FoundRecord = true;
                    ValidTransactionHistory[i] = false;
                    break;
            }
        }
        // Pass 2: Soft
        if(!FoundRecord){
            for(int i=0; i<MAXTRANSACTIONS; i++){
                if(!ValidTransactionHistory[i])
                    continue;
                if( strcmp(m.FileName, TransactionHistory[i].FileName) == 0 &&
                    m.FType == TransactionHistory[i].FType ){
                    FoundRecord = true;
                    break;
                }
            }
        }
        mtx.unlock();
        return FoundRecord;
    }
    bool record_message(message m){
        mtx.lock();
        bool RecordingSuccess = false;
        int i = 0;
        while(!RecordingSuccess && i<MAXTRANSACTIONS){
            if(ValidTransactionHistory[i]){
                i++;
                continue;
            }
            else{
                strcpy(TransactionHistory[i].FileName, m.FileName);
                TransactionHistory[i].FCType = m.FCType;
                TransactionHistory[i].FileSize = m.FileSize;
                TransactionHistory[i].FType = m.FType;
                ValidTransactionHistory[i] = true;
                RecordingSuccess = true;
                break;
            }
        }
        mtx.unlock();
        return RecordingSuccess;
    }
    bool already_processed(message m){
        mtx.lock();
        bool FoundRecord = false;
        // Pass 1: Hard
        for(int i=0; i<MAXTRANSACTIONS; i++){
            if(!ValidTransactionHistory[i])
                continue;
            if( strcmp(m.FileName, TransactionHistory[i].FileName) == 0 &&
                m.FType == TransactionHistory[i].FType &&
                m.FileSize == TransactionHistory[i].FileSize &&
                m.FCType == TransactionHistory[i].FCType){
                FoundRecord = true;
                break;
            }
        }
        mtx.unlock();
    }
};

/* Globals */
TransactionHistory_t    Transactions;
std::mutex              FileAccess;
int                     FCsock, RCsock;

/* Function declarations */

void set_reader_connection(int portno, char* ipaddr);
void set_filewatcher_connection(int portno, char* ipaddr);
void reader(char *dirname, int new_socket);
void filewatcher(char *dirname, char* intermediate, int sock);
void closeallsystems();
message* FormMessage(char *filename, FileChangeType_t fctype, FileType_t ftype, unsigned long filesize);


/* Main driver function
 * Parameters:
 *      1.  Folder to watch
 *      2.  IP address of other droid
 *      3.  Port no for filewatcher
 *      4.  Port no for reciever
 */



int main( int argc, char **argv)
{
    if(argc != 5){
        std::cout<<"[MAIN] Usage: <programexec> <FolderName> <droid_IP> <SendPORT> <RecvPORT>"<<std::endl;
        exit(-1);
    }
    std::cout<<"    __________________________________________"<<std::endl;
    std::cout<<"  /_________________________________________ |"<<std::endl;
    std::cout<<" /----------------------------------------+ ||"<<std::endl;
    std::cout<<"|                                         | ||"<<std::endl;
    std::cout<<"|              DIRECTORY SYNC             | |+"<<std::endl;
    std::cout<<"|                                         |-+"<<std::endl;
    std::cout<<"+-----------------------------------------+"<<std::endl;

    std::cout<<"[MAIN] Setting up connections "<<std::endl;
    std::thread RConnect(set_reader_connection, atoi(argv[4]), argv[2]);
    std::thread FConnect(set_filewatcher_connection, atoi(argv[3]), argv[2]);
    RConnect.join();
    FConnect.join();
    std::cout<<"[MAIN] Connections setup!"<<std::endl;
    char empty[] = "";
    std::thread listener(reader, argv[1], RCsock);
    std::thread watcher(filewatcher, argv[1], empty, FCsock);
    std::thread closer(closeallsystems);
    listener.join();
    watcher.join();
    closer.join();
    return 0;
}



void set_reader_connection(int portno, char* ipaddr){
    // Network
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1, valread;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        std::cout<<"[RC] Socket Failed"<<std::endl;
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        std::cout<<"[RC] Socket Failed (setsockopt)"<<std::endl;
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( portno );

    // Forcefully attaching socket to the port
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address))<0)
    {
        std::cout<<"[RC] Socket Failed to bind"<<std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        std::cout<<"[RC] Error Listen"<<std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RC] Ready to listen. Ping the other droid!"<<std::endl;

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
    {
        std::cout<<"[RC] Error accepting connection"<<std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RC] Accepted communication at [" <<portno<<"]"<<std::endl;

    RCsock = new_socket;

}
void set_filewatcher_connection(int portno, char* ipaddr){
    // Network setup with other Droid
    struct sockaddr_in address;
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cout<<"[FC] Socket creation error"<<std::endl;
        exit(1);
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, ipaddr, &serv_addr.sin_addr)<=0)
    {
        std::cout<<"[FC] Invalid address/ Address not supported "<<std::endl;
        exit(1);
    }
    std::cout<<"[FC] Other bot ready to accept communication?";
    char ch;
    std::cin>>ch;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        std::cout<<"[FC] Connection Failed"<<std::endl;
        exit(1);
    }
    else
        std::cout<<"[FC] Successfully setup connection with other droid\n";

    FCsock = sock;
}
void reader(char *dirname, int new_socket) {
    std::cout << "#### Began thread [RECV]" <<std::endl;

    // Start receiving
    message *incoming = (message*)malloc(sizeof(message));
    int valread;
    char workCMD[WORKCMDLEN];
    while(1){
        valread = recv(new_socket ,(void*)incoming, sizeof(message), 0);
        if(incoming->valid == 0 || Transactions.already_processed(*incoming)){
            std::cout<<"[RECV] Recieved invalid/duplicate data packet. Probably the connection was closed. Quitting."<<std::endl;

            std::cout<<        "                                 +---------------------+\n";
            std::cout<<        "                                 | I am in sync. Yes   |\n";
            std::cout<<        "                                 +------------------+--+\n";
            std::cout<<        "                                                    |\n";
            std::cout<<        "                                                    |\n";
            std::cout<<        "  _                                                 |\n";
            std::cout<<        "  \\\\                                                |\n";
            std::cout<<        "   \\\\_          _.-._                               |\n";
            std::cout<<        "    X:\\        (_/ \\_)     <------------------------+\n";
            std::cout<<        "    \\::\\       ( ==  )\n";
            std::cout<<        "     \\::\\       \\== /\n";
            std::cout<<        "    /X:::\\   .-./`-'\\.--.\n";
            std::cout<<        "    \\\\/\\::\\ / /     (    l\n";
            std::cout<<        "     ~\\ \\::\\ /      `.   L.\n";
            std::cout<<        "       \\/:::|         `.'  `\n";
            std::cout<<        "       /:/\\:|          (    `.\n";
            std::cout<<        "       \\/`-'`.          >    )\n";
            std::cout<<        "              \\       //  .-'\n";
            std::cout<<        "               |     /(  .'\n";
            std::cout<<        "               `-..-'_ \\  \\\n";
            std::cout<<        "               __||/_ \\ `-'\n";
            std::cout<<        "              / _ \\ #  |\n";
            std::cout<<        "             |  #  |#  |   B-SD3 Security Droid\n";
            std::cout<<        "          LS |  #  |#  |      - Front View -\n";
            exit(0);
        }
        switch(incoming->FCType){
            case CREATE:
                if(incoming->FType == DIRECTORY){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }
                    std::cout<<"[RECV]\tCreated\tDIR\t["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "mkdir -p ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);

                    char *newintermediate = (char *)malloc(sizeof(char)*MAXFILENAMELEN);

                    strcpy(newintermediate, incoming->FileName);
                    std::thread currentrecv(reader, dirname, new_socket);
                    std::thread newfilewatcher(filewatcher, dirname, newintermediate, FCsock);
                    currentrecv.join();
                    newfilewatcher.join();

                }
                else if(incoming->FType == _FILE){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }

                    std::cout<<"[RECV]\tCreated\tFIL\t["<<incoming->FileName<<"]"<<std::endl;

                    FileAccess.lock();
                    strcpy(workCMD, "touch ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);
                    FileAccess.unlock();

                }
                break;

            case MODIFY:
                if(incoming->FType == _FILE){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }

                    std::cout<<"[RECV]\tModified\tFIL\t["<<incoming->FileName<<"] size = "<<incoming->FileSize<<" "<<std::endl;

                    FileAccess.lock();

                    strcpy(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    std::ofstream filedump(workCMD, std::ios::binary);

                    char *filedata = (char *)malloc(incoming->FileSize);
                    read(new_socket, filedata, incoming->FileSize);
                    if(!filedump){
                        std::cout<<"[RECV] Error opening file "<<incoming->FileName<<std::endl;
                        exit(1);
                    }
                    filedump<<filedata;
                    free(filedata);
                    filedump.close();
                    incoming->FileSize = 0;
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }
                    FileAccess.unlock();

                }
                break;


            case DELETE:
                if(incoming->FType == DIRECTORY){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }
                    std::cout<<"[RECV]\tDeleted\tDIR\t["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "rm -r ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);
                }
                else if(incoming->FType == _FILE){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }

                    std::cout<<"[RECV]\tDeleted\tFIL\t["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "rm ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);

                }
                break;
            default:
                std::cout<<"[RECV] Unknown message received, QUIT"<<std::endl;
                exit(0);
        }

    }
}
void filewatcher(char *dirname, char *intermediate, int sock) {

    std::cout << "#### Began thread [FILEWATCHER]"<<std::endl;


    // Initialise FileWatcher
    int fd, wd;
    char workCMD[WORKCMDLEN];

    strcpy(workCMD, dirname);
    if(strlen(intermediate) > 0)
        strcat(workCMD, "/");
    strcat(workCMD, intermediate);

    fd = inotify_init();
    if ( fd < 0 ) {
        std::cout<<"[FW: inotify] Could not initialise inotify, quitting"<<std::endl;
        exit(1);
    }
    wd = inotify_add_watch(fd, workCMD, IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd == -1){
        std::cout<<"[FW: inotify] Could not add watch to ["<<workCMD<<"], quitting\n";
        exit(1);
    }
    else
        std::cout<<"[FW: inotify] Started watching ["<<workCMD<<"]"<<std::endl;


    // Actual FileWatch system
    char *FWINBuffer[INOTIFYBUFLEN];
    int length;
    while(1) {
        int i = 0;
        length = read(fd, FWINBuffer, INOTIFYBUFLEN);
        if (length < 0)
            std::cout << "[FILEWATCHER] Read Error \n";
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &FWINBuffer[i];
            if (event->len) {
                if (event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR) {
                        strcpy(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::cout << "[FW: inotify]\tCreated\tDIR\t" << workCMD << std::endl;
                        message *message_packet = FormMessage(workCMD, CREATE, DIRECTORY, 0);
                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to create [" << event->name << "] Directory"
                                      << std::endl;

                            char *newintermediate = (char *)malloc(sizeof(char)*MAXFILENAMELEN);
                            strcpy(newintermediate, workCMD);
                            std::thread currentfilewatcher(filewatcher, dirname, intermediate, sock);
                            std::thread newfilewatcher(filewatcher, dirname, newintermediate, sock);
                            currentfilewatcher.join();
                            newfilewatcher.join();

                        } else {
                            std::cout << "[FW: inotify]\tDirectory [" << event->name
                                      << "]  created due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                    else {
                        strcpy(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::cout << "[FW: inotify]\tCreated\tFIL\t" << workCMD << std::endl;
                        message *message_packet = FormMessage(workCMD, CREATE, _FILE, 0);

                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to create [" << workCMD << "] file"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tFile [" << event->name
                                      << "]  created due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                }
                else if (event->mask & IN_MODIFY) {
                    if(!(event->mask & IN_ISDIR)){


                        FileAccess.lock();
                        strcpy(workCMD, dirname);
                        strcat(workCMD, "/");
                        strcat(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::ifstream file(workCMD, std::ios::binary);
                        file.seekg(0, file.end);
                        std::cout << "[FW: inotify]\tModified\tFIL\t" << workCMD << std::endl;
                        unsigned int filesize = file.tellg();
                        file.close();
                        FileAccess.unlock();
                        strcpy(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        message *message_packet = FormMessage(workCMD, MODIFY, _FILE, filesize);

                        if(!Transactions.is_recorded(*message_packet)){
                            int sent = send(sock, message_packet, sizeof(message), 0);

                            FileAccess.lock();
                            strcpy(workCMD, dirname);
                            strcat(workCMD, "/");
                            strcat(workCMD, intermediate);
                            if(strlen(intermediate) > 0)
                                strcat(workCMD, "/");
                            strcat(workCMD, event->name);
                            std::cout << "[FW: inotify]\tSent request to modify [" << workCMD << "] filesize = "<< filesize
                                      << std::endl;
                            file.open(workCMD, std::ios::binary);
                            file.seekg(0, file.beg);
                            char *filedata = (char*)malloc(filesize);
                            file.read(filedata, filesize);
                            FileAccess.unlock();
                            send(sock, filedata, filesize, 0);
                            free(filedata);

                        }else{
                            std::cout << "[FW: inotify]\tFile [" << workCMD
                                      << "]  modified due to incoming request, skip request send" << std::endl;
                        }
                    }
                }
                else if (event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR) {
                        strcpy(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::cout << "[FW: inotify]\tDeleted\tDIR\t" << workCMD << std::endl;
                        message *message_packet = FormMessage(workCMD, DELETE, DIRECTORY, 0);
                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to delete [" << workCMD << "] Directory"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tDirectory [" << event->name
                                      << "]  deleted due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                    else {
                        strcpy(workCMD, intermediate);
                        if(strlen(intermediate) > 0)
                            strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::cout << "[FW: inotify]\tDeleted\tFIL\t" << workCMD << std::endl;
                        message *message_packet = FormMessage(workCMD, DELETE, _FILE, 0);

                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to delete [" << workCMD << "] file"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tFile [" << event->name
                                      << "]  delete due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }

}
void closeallsystems(){
    std::cout<<"NOTICE: You can shutdown the system by typing <shut>"<<std::endl;
    char input[10];
    while(1) {
        std::cin >> input;
        if(strcmp(input, "shut") == 0){
            std::cout<<"****Shutting down DirectorySync*****"<<std::endl;
            message* closerequest = (message*)malloc(sizeof(message));
            closerequest->valid = 0;
            int sent = send(FCsock, closerequest, sizeof(message), 0);
            free(closerequest);
            std::cout<<        "                                 +---------------------+\n";
            std::cout<<        "                                 | I am in sync. Yes   |\n";
            std::cout<<        "                                 +------------------+--+\n";
            std::cout<<        "                                                    |\n";
            std::cout<<        "                                                    |\n";
            std::cout<<        "  _                                                 |\n";
            std::cout<<        "  \\\\                                                |\n";
            std::cout<<        "   \\\\_          _.-._                               |\n";
            std::cout<<        "    X:\\        (_/ \\_)     <------------------------+\n";
            std::cout<<        "    \\::\\       ( ==  )\n";
            std::cout<<        "     \\::\\       \\== /\n";
            std::cout<<        "    /X:::\\   .-./`-'\\.--.\n";
            std::cout<<        "    \\\\/\\::\\ / /     (    l\n";
            std::cout<<        "     ~\\ \\::\\ /      `.   L.\n";
            std::cout<<        "       \\/:::|         `.'  `\n";
            std::cout<<        "       /:/\\:|          (    `.\n";
            std::cout<<        "       \\/`-'`.          >    )\n";
            std::cout<<        "              \\       //  .-'\n";
            std::cout<<        "               |     /(  .'\n";
            std::cout<<        "               `-..-'_ \\  \\\n";
            std::cout<<        "               __||/_ \\ `-'\n";
            std::cout<<        "              / _ \\ #  |\n";
            std::cout<<        "             |  #  |#  |   B-SD3 Security Droid\n";
            std::cout<<        "          LS |  #  |#  |      - Front View -\n";
            exit(0);
        }
        else{
            std::cout<<"(To close, type \"shut\")"<<std::endl;
        }
    }
}

message* FormMessage(char *filename, FileChangeType_t fctype, FileType_t ftype, unsigned long filesize){
    message* newmsg = (message *)malloc(sizeof(message));
    strcpy(newmsg->FileName, filename);
    newmsg->FType = ftype;
    newmsg->FCType = fctype;
    newmsg->FileSize = filesize;
    newmsg->valid = 1;
    return newmsg;
}