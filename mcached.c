/* modifed code from
  https://www.ibm.com/docs/en/zos/2.4.0?topic=programs-c-socket-tcp-server:
 */ 


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "mcached.h"


/*
 * Server Main.
 */


kv_pair *table[HASH_TABLE_SIZE] = {0};
pthread_mutex_t hash_table_lock = PTHREAD_MUTEX_INITIALIZER;




void print_hex(const char *label, const unsigned char *data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

uint32_t hash_key(const uint8_t *key, uint32_t key_len) {
    uint32_t hash = 0;
    for (uint32_t i = 0; i < key_len; ++i) {
        hash += key[i];
    }
    return hash % HASH_TABLE_SIZE;
}

// Search function: Searches for the key in the hash table and returns the entire node
kv_pair *search(kv_pair *table[], const uint8_t *key, uint32_t key_len) {
    uint32_t index = hash_key(key, key_len); // Calculate the index in the hash table
    kv_pair *entry = table[index];           // Get the first entry in the linked list for this index

    while (entry) {
        // Check if this entry's key matches the search key
        if (entry->key_len == key_len && memcmp(entry->key, key, key_len) == 0) {
            return entry;  // Return the entire kv_pair node
        }
        entry = entry->next;  // Move to the next entry in the linked list (if any)
    }

    return NULL;  // Key not found
}

void handle_client(int new_socket){
    memcache_req_header_t header;
    while (recv(new_socket, &header, sizeof(memcache_req_header_t), 0) > 0){
            
        // Print out received header (debugging)
        //printf("Received header: Magic: 0x%02x, Opcode: 0x%02x, Key Length: %d, Extras Length: %d, Body Length: %d\n", 
               //header.magic, header.opcode, ntohs(header.key_length), header.extras_length, ntohl(header.total_body_length));
       

        uint16_t key_len = ntohs(header.key_length);
        uint32_t total_body_len = ntohl(header.total_body_length);
        uint8_t extras_len = header.extras_length;
        uint32_t value_len = total_body_len - extras_len - key_len;

        // set header magic
        header.magic = 0x81; 

        // Allocate memory for extras, key, and value
        uint8_t *extras = malloc(extras_len);
        uint8_t *key = malloc(key_len);
        uint8_t *value = malloc(value_len);

        if (!extras || !key || !value) {
            perror("malloc");
            close(new_socket);
            continue;
        }

        if (extras_len > 0 && recv(new_socket, extras, extras_len, 0) <= 0) {
            perror("Error receiving extras");
            close(new_socket);
            continue;
        }
        if (key_len > 0 && recv(new_socket, key, key_len, 0) <= 0) {
            perror("Error receiving key");
            close(new_socket);
            continue;
        }
        if (value_len > 0 && recv(new_socket, value, value_len, 0) < 0) {
            perror("Error receiving value");
            close(new_socket);
            continue;
        }
      
        memcache_req_header_t * res = malloc(sizeof(memcache_req_header_t));
        res->magic = 0x81;               
        res->key_length = htons(0);       
        res->extras_length = 0;         
        res->vbucket_id = htons(0);       
        res->total_body_length = htonl(0); 
        res->opcode = header.opcode;
        // get operation
        if(header.opcode == CMD_GET){
            kv_pair* entry = search(table, key, key_len);
            if (entry != NULL){
                 pthread_mutex_lock(&entry->lock);
                 // send
                 res->vbucket_id = htons(RES_OK);
                 res->total_body_length = htonl(entry->value_len);
                 // First send the header
                 send(new_socket, res, sizeof(memcache_req_header_t), 0);
                 // Then send the value
                 send(new_socket, entry->value, entry->value_len, 0);

                 pthread_mutex_unlock(&entry->lock);
            }
            else{
                res->vbucket_id = htons(RES_NOT_FOUND);
                send(new_socket, res, sizeof(memcache_req_header_t), 0);
            }
        }    

       // add operation
        else if(header.opcode == CMD_ADD || header.opcode == CMD_SET){
        pthread_mutex_lock(&hash_table_lock);
        kv_pair* entry = search(table, key, key_len);

        if(header.opcode == CMD_ADD && entry != NULL){
            //printf("Key exists. Skipping ADD operation.\n");
            
            pthread_mutex_unlock(&hash_table_lock);
            //(new_socket, &header, sizeof(header), 0);

            // Send only the header
            send(new_socket, res, sizeof(memcache_req_header_t), 0);

            continue;
        }

        if(entry == NULL){
            entry = (kv_pair *)malloc(sizeof(kv_pair));
            entry->key = (uint8_t *)malloc(key_len);
            memcpy(entry->key, key, key_len);
            entry->key_len = key_len;
            // Allocate and copy the value
            entry->value = (uint8_t *)malloc(value_len);
            memcpy(entry->value, value, value_len);
            entry->value_len = value_len;

            // Initialize the lock for the entry
            pthread_mutex_init(&entry->lock, NULL);
            // Insert the entry into the hash table (insert at the beginning of the linked list)
            uint32_t index = hash_key(key, key_len);
            entry->next = table[index];  // Point to the existing entry (if any)
            table[index] = entry;  
            //printf("Added new key-value pair.\n");

        }
        
        
        // If the operation is SET, we update the value
        pthread_mutex_lock(&entry->lock); // Lock the individual entry to modify it
        //printf("Updating value for the existing key.\n");
        
        
        // Free the existing value if needed
        free(entry->value);

        // Allocate and copy the new value
        entry->value = malloc(value_len);
        memcpy(entry->value, value, value_len);
        entry->value_len = value_len;

        
        

        pthread_mutex_unlock(&entry->lock);
        pthread_mutex_unlock(&hash_table_lock);

        // Send only the header
        res->vbucket_id = htons(RES_OK);
        send(new_socket, res, sizeof(memcache_req_header_t), 0);

       }

        else if (header.opcode == CMD_DELETE) {
            pthread_mutex_lock(&hash_table_lock);

            uint32_t index = hash_key(key, key_len);
            kv_pair *entry = table[index];
            kv_pair *prev = NULL;

            while (entry != NULL) {
                if (entry->key_len == key_len && memcmp(entry->key, key, key_len) == 0) {
                    // Key found
                    pthread_mutex_lock(&entry->lock);

                    // Remove from the linked list
                    if (prev == NULL) {
                        table[index] = entry->next;
                    } else {
                        prev->next = entry->next;
                    }

                    pthread_mutex_unlock(&entry->lock);
                    pthread_mutex_destroy(&entry->lock);

                    // Free key, value, and entry
                    free(entry->key);
                    free(entry->value);
                    free(entry);

                    pthread_mutex_unlock(&hash_table_lock);

                    // Send Success response
                    res->vbucket_id = htons(RES_OK);
                    send(new_socket, res, sizeof(memcache_req_header_t), 0);

                    break;
                }
                prev = entry;
                entry = entry->next;
            }

            if (entry == NULL) {
                pthread_mutex_unlock(&hash_table_lock);

                // Send Not Found response
                res->vbucket_id = htons(RES_NOT_FOUND);
                send(new_socket, res, sizeof(memcache_req_header_t), 0);
            }
        }



        // Handle the specific "dump" opcode
        
        else if (header.opcode == CMD_OUTPUT) {
            // Acquire the global lock on the hash table
            pthread_mutex_lock(&hash_table_lock);

            // Get the current timestamp
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint32_t seconds = (uint32_t)ts.tv_sec;
            uint32_t nanoseconds = (uint32_t)ts.tv_nsec;

            // Iterate through the hash table and print each key-value pair
            for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
                kv_pair *entry = table[i];
                while (entry) {
                    // Print seconds and nanosecond
                    printf("%0x:%0x:", seconds, nanoseconds);

                    // Print key in hex
                    for (uint32_t k = 0; k < entry->key_len; ++k) {
                        printf("%02x", (unsigned char)entry->key[k]);
                    }

                    printf(":");

                    // Print value in hex
                    for (uint32_t v = 0; v < entry->value_len; ++v) {
                        printf("%02x", (unsigned char)entry->value[v]);
                    }

                    printf("\n");

                    entry = entry->next;
                }
            }

            // Release the global lock after printing
            pthread_mutex_unlock(&hash_table_lock);

            // Send the success response
            send(new_socket, res, sizeof(memcache_req_header_t), 0);
    


        }
        
        else if(header.opcode == CMD_VERSION){
            char * x = "C-Memcached 1.0";
            size_t string_length = strlen(x);
            res->vbucket_id = htons(RES_OK);
            res->total_body_length = htonl((uint32_t)string_length);
            // First send the response header
            send(new_socket, res, sizeof(memcache_req_header_t), 0);
            // Then send the version string as the body
            send(new_socket, x, string_length, 0);
        }
        else{
            res->vbucket_id = htons(RES_ERROR);
            send(new_socket, res, sizeof(memcache_req_header_t), 0);
        }
        //printf("Operation Code: %u\n", (res->opcode));
        free(extras);
        free(key);
        free(value);
        //  0.438447 s

        

        
        
    }
    close(new_socket);
    return;

}

void *worker(void *arg) {
    int orig_socket = *(int *)arg;
    struct sockaddr_in client_addr;
    socklen_t namelen = sizeof(client_addr);


    

    while (1) {
        //pthread_mutex_lock(&accept_lock);
        //int new_socket = accept(orig_socket, (struct sockaddr *)&client_addr, &namelen);
        //pthread_mutex_lock(&accept_lock);

        //pthread_mutex_lock(&accept_lock);
        //printf("Thread %ld: waiting to accept\n", pthread_self());
        int new_socket = accept(orig_socket, (struct sockaddr *)&client_addr, &namelen);
        //printf("Thread %ld: accepted client\n", pthread_self());

        //pthread_mutex_unlock(&accept_lock);

        if (new_socket == -1) {
            perror("Accept()");
            continue; 
        }

        handle_client(new_socket);
        

    }
    return NULL;
}


/* very OG way to pass arguments in C -- name args then provide the types */ 
int main(argc, argv)
int argc;
char **argv;
{
    unsigned short port;       /* port server binds to                */
    //char buf[MSG_SIZE];              /* buffer for sending & receiving data */
    //struct sockaddr_in client_addr; /* client address information          */
    struct sockaddr_in server_addr; /* server address information          */
    int orig_socket;                     /* socket for accepting connections    */
    //int new_socket;                    /* socket connected to client          */
    //int namelen;               /* length of client name               */
    //int sleep_time;
    //int keep_going;            /* flag to keep the server accepting connections from clients */ 
    /*
     * Check arguments. Should be only one: the port number to bind to.
     */

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s port Thread count: \n", argv[0]);
        exit(1);
    }

    /*
     * First argument should be the port.
     */
    port = (unsigned short) atoi(argv[1]);

    /*
     * Get a socket for accepting connections.
     */
    if ((orig_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket()");
        exit(2);
    }

    /*
     * Bind the socket to the server address.
     */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;  

    if (bind(orig_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind()");
        exit(3);
    }

    /*
     * Listen for connections. Specify the backlog as 1.
     */
    if (listen(orig_socket, 50) != 0)
    {
        perror("Listen()");
        exit(4);
    }
    int threadcount = atoi(argv[2]);

    


    pthread_t *threads = malloc((size_t)threadcount * sizeof(pthread_t));
    if (!threads) {
        perror("malloc");
        exit(5);
    }

    for (int i = 0; i < threadcount; i++) {
        if (pthread_create(&threads[i], NULL, worker, &orig_socket) != 0) {
            perror("pthread_create");
            exit(6);
        }
    }

    for (int i = 0; i < threadcount; i++) {
        pthread_join(threads[i], NULL);
    }

    close(orig_socket);
    free(threads);

    printf("Server ended successfully\n");
    exit(0);

}


// 1: 2.22, 2: 1.33, 5: .4675, 7: .4517, 9: .44