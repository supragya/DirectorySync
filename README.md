# Directory Sync
A simple program that keeps two folders over a network in sync.

## Setting up and running
The build process of the system is simple, however does require you to have 
- Linux (program developed on Manjaro 17.1)
- GNU make 4.2.1
- G++/GCC 7.3.0
- inotify-tools

To build the system, clone the repository to you local disk using
```
git clone https://github.com/supragya/DirectorySync.git
```
After this, the following set of commands will build the system:
```
cd DirectorySync
mkdir bin
make
```
The execuatable should be built in `bin/` folder of your present working directory.

Now, we need to create a directory that will be watched by the program. This can be done using:
```
mkdir watchdir
```

Repeat the same process of setup on another computer (client).

Now, the clients need to be connected. This can be done using LAN connection. Once setup, make sure both client know their IP addresses and can also ping each other.

Also, make sure that the directory being watched is empty at the start. The sync programs should keep running as long as both directories have data.

On each client, run the program using the following template
```
bin/DirectorySync {FolderName} {droid_IP} {SendPORT} {RecvPORT}
```

For example, if you wish Client A connects to Client B and syncs on folder (alpha) at Client A and (beta) at Client B with Client A having IP of 8.1.2.3 and Client B having IP of 2.3.1.1, think of two open port numbers you can use to comminicate. Client A for example may send data on port 8080, where Client B will listen and Client B may send it's data on port 8082 and Client A may recieve data on that port. This example configuration can be done by running the program using the parameters as follows:

CLIENT A
```
bin/DirectorySync alpha 2.3.1.1 8080 8082
```
CLIENT B
```
bin/DirectorySync beta 8.1.2.3 8082 8080
```
Once both have initiated and provide the prompt `[RC] Ready to listen. Ping the other droid!`, it's time to put a character on console and press ENTER on both the clients. The programs will give confirmation of correct working.

For shutting the system down, never use KeyboardInterrupts like Ctrl+C or Ctrl+V. To stop the sync process, type "shut" anytime after the prompt `NOTICE: You can shutdown the system by typing <shut>`. This will terminate the systems on both ends.

### May the force be with us.
```
                                 +---------------------+
                                 | I am in sync        |
                                 +------------------+--+
                                                    |
                                                    |
  _                                                 |
  \\                                                |
   \\_          _.-._                               |
    X:\        (_/ \_)     <------------------------+
    \::\       ( ==  )
     \::\       \== /
    /X:::\   .-./`-'\.--.
    \\/\::\ / /     (    l
     ~\ \::\ /      `.   L.
       \/:::|         `.'  `
       /:/\:|          (    `.
       \/`-'`.          >    )
              \       //  .-'
               |     /(  .'
               `-..-'_ \  \
               __||/_ \ `-'
              / _ \ #  |
             |  #  |#  |   B-SD3 Security Droid
          LS |  #  |#  |      - Front View -

```
