

/* header file for the mcached protocol.
 */ 
#ifdef _CACHED_
#define _CACHED_
#else

#define CMD_GET     0x00
#define CMD_SET     0x01
#define CMD_ADD     0x02
#define CMD_DELETE  0x04
#define CMD_VERSION 0x0b
#define CMD_OUTPUT  0x0c
#define RES_OK         0x0000
#define RES_NOT_FOUND  0x0001
#define RES_EXISTS     0x0002
#define RES_ERROR      0x0004

typedef struct {
    uint8_t magic;
    uint8_t opcode;
    uint16_t key_length;
    uint8_t extras_length;
    uint8_t data_type;
    uint16_t vbucket_id;
    uint32_t total_body_length;
    uint32_t opaque;
    uint64_t cas;
} __attribute__((packed)) memcache_req_header_t;

#define HASH_TABLE_SIZE 1024

typedef struct kv_pair {
    uint8_t *key;
    uint32_t key_len;
    uint8_t *value;
    uint32_t value_len;
    struct kv_pair *next;
    pthread_mutex_t lock; // lock on individual entry (optional)
} kv_pair;





#endif

