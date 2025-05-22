
# TCP Memory Cache

This is a simple TCP server/client that serves like a simple memory cache daemon, implementing sockets, parallelism, and concurrency. It creates a server which listens to many different clients and utilizes multithreading to speed up response times. At it's core, the server is a hash table which can be accessed by any number of clients. Clients can add, delete, and query the server’s hash table. The server supports multiple simultaneous requests as well. 





## Usage
The client itself is precoded to send requests based on number of client threads. The server (mcached) uses threads to accept multiple simultaneous clients. 

```
./client <server_ip_address> <server_port> <number of client threads>

./mcached <port> <number of server threads>
