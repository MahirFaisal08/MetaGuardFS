#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#define DEBUG 0
#define LOG(fmt, ...) do { if (DEBUG) fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while (0)
#define IMG_NAME "vsfs.img"
#define BLOCK_SIZE 4096
#define INODE_SIZE 128
#define NAME_LEN 28


#define FS_MAGIC      0x56534653U
#define JOURNAL_MAGIC 0x4A524E4CU
#define JOURNAL_BLOCKS 16
#define JOURNAL_SIZE (JOURNAL_BLOCKS * BLOCK_SIZE)
#define REC_DATA   1
#define REC_COMMIT 2


#define TYPE_FREE 0
#define TYPE_FILE 1
#define TYPE_DIR  2


struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t journal_block;
    uint32_t inode_bitmap;
    uint32_t data_bitmap;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t  pad[128 - 9*4];
    
};


struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t  pad[128 - (2+2+4 + 8*4 + 4+4)];
};


struct dirent {
    uint32_t inode;
    char name[NAME_LEN];
};


struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[BLOCK_SIZE];
};

struct commit_record {
    struct rec_header hdr;
};


static int fd = -1;

static struct superblock sb;

static void die(const char *msg) { perror(msg); exit(1); }

static void read_block(uint32_t block, void *buf) {
    if (pread(fd, buf, BLOCK_SIZE, (off_t)block * BLOCK_SIZE) != BLOCK_SIZE) die("read_block");
}

static void write_block(uint32_t block, const void *buf) {
    if (pwrite(fd, buf, BLOCK_SIZE, (off_t)block * BLOCK_SIZE) != BLOCK_SIZE) die("write_block");
}

static void load_super(void) {
    uint8_t blk[BLOCK_SIZE];
    read_block(0, blk);
    memcpy(&sb, blk, sizeof(sb));
}


// Free inode allocation Function
static int find_free_bit(const uint8_t *bm, int max) {
    for (int i = 0; i < max; i++) {
        if (!(bm[i/8] & (1 << (i%8)))) return i;
    }
    
    return -1;
    
}

static void set_bit(uint8_t *bm, int bit) {
    bm[bit/8] |= (1 << (bit%8));
}



// F-6 Free directory entry slot Function (Find free slot in root directory)

static int find_free_dir_slot(const struct dirent *de) {
    size_t n = BLOCK_SIZE / sizeof(*de);
    
    for (size_t i = 0; i < n; i++) {
        if (de[i].name[0] == '\0') return (int)i;
    }
    
    return -1;
}

static void jhdr_read(struct journal_header *jh) {
    if (pread(fd, jh, sizeof(*jh), (off_t)sb.journal_block * BLOCK_SIZE) != (ssize_t)sizeof(*jh))
        die("pread journal_header");
}

static void jhdr_write(const struct journal_header *jh) {
    if (pwrite(fd, jh, sizeof(*jh), (off_t)sb.journal_block * BLOCK_SIZE) != (ssize_t)sizeof(*jh))
        die("pwrite journal_header");
}


// F-3 Journal initialize (set-magic, nbytes used, empty journal )

static void init_journal_if_needed(void) {
    struct journal_header jh;
    
    jhdr_read(&jh);
    
    if (jh.magic == JOURNAL_MAGIC) return;

    uint8_t zero[BLOCK_SIZE];
    
    memset(zero, 0, sizeof(zero));
    
    jh.magic = JOURNAL_MAGIC;
    jh.nbytes_used = sizeof(struct journal_header); //Clear Journal
    
    memcpy(zero, &jh, sizeof(jh));

    for (int i = 0; i < JOURNAL_BLOCKS; i++)
        write_block(sb.journal_block + (uint32_t)i, zero);
}



static int journal_append(const void *rec, uint16_t sz) {
    struct journal_header jh;
    jhdr_read(&jh);

    if (jh.magic != JOURNAL_MAGIC) {
        fprintf(stderr, "Journal not initialized\n");
        
        return -1;
        
    }
    
    
// Handling Journal Full Condition (Transaction Refusal)

    if ((uint32_t)jh.nbytes_used + sz > JOURNAL_SIZE) {
        fprintf(stderr, "Journal full, run install first\n");
        
        return -1;
        
    }
    
    
    off_t off = (off_t)sb.journal_block * BLOCK_SIZE + (off_t)jh.nbytes_used;
    
    if (pwrite(fd, rec, sz, off) != (ssize_t)sz) die("journal append");


    jh.nbytes_used += sz;
    
    jhdr_write(&jh);
    
    return 0;
    
    
}


static void replay_committed_to_virtual(uint8_t *v_inodebm,
                                       uint8_t v_itbl[2][BLOCK_SIZE],
                                       uint8_t *v_rootinodeblk,
                                       uint8_t *v_rootdirblk,
                                       uint32_t *v_rootdir_blockno) {
    read_block(sb.inode_bitmap, v_inodebm);
    read_block(sb.inode_start + 0, v_itbl[0]);
    read_block(sb.inode_start + 1, v_itbl[1]);
    read_block(sb.inode_start, v_rootinodeblk);
    struct inode *r = (struct inode *)v_rootinodeblk;
    *v_rootdir_blockno = r->direct[0];
    read_block(*v_rootdir_blockno, v_rootdirblk);
    struct journal_header jh;
    jhdr_read(&jh);
    
    
    if (jh.magic != JOURNAL_MAGIC) return;
    
    
    if (jh.nbytes_used <= sizeof(struct journal_header)) return;
    
    
    if (jh.nbytes_used > JOURNAL_SIZE) return;
    
    
    uint8_t *buf = malloc(jh.nbytes_used);
    
    if (!buf) die("malloc journal");
    
    if (pread(fd, buf, jh.nbytes_used, (off_t)sb.journal_block * BLOCK_SIZE) != (ssize_t)jh.nbytes_used)
        die("pread journal bytes");
    size_t off = sizeof(struct journal_header);
    
    
    size_t last_commit = 0;
    
    while (off + sizeof(struct rec_header) <= jh.nbytes_used) {
        struct rec_header *rh = (struct rec_header *)(buf + off);
        if (rh->size < sizeof(struct rec_header)) break;
        if (off + rh->size > jh.nbytes_used) break;
        if (rh->type == REC_COMMIT) last_commit = off + rh->size;
        
        
        off += rh->size;
    }
    
    
    off = sizeof(struct journal_header);
    
    while (off < last_commit) {
        struct rec_header *rh = (struct rec_header *)(buf + off);
        if (rh->size < sizeof(struct rec_header)) break;
        
        if (off + rh->size > last_commit) break;

        if (rh->type == REC_DATA) {
        
            struct data_record *dr = (struct data_record *)(buf + off);

            if (dr->block_no == sb.inode_bitmap) {
                memcpy(v_inodebm, dr->data, BLOCK_SIZE);
                
            } else if (dr->block_no == sb.inode_start + 0) {
                memcpy(v_itbl[0], dr->data, BLOCK_SIZE);
            } else if (dr->block_no == sb.inode_start + 1) {
                memcpy(v_itbl[1], dr->data, BLOCK_SIZE);
            } else if (dr->block_no == sb.inode_start) {
                memcpy(v_rootinodeblk, dr->data, BLOCK_SIZE);
                r = (struct inode *)v_rootinodeblk;
                *v_rootdir_blockno = r->direct[0];
            } else if (dr->block_no == *v_rootdir_blockno) {
            
                memcpy(v_rootdirblk, dr->data, BLOCK_SIZE);
                
                
            }
        }
        
        
        off += rh->size;
    }


    free(buf);
    
    
}



static void cmd_create(const char *name) {

    init_journal_if_needed();

    uint8_t v_inodebm[BLOCK_SIZE];
    
    uint8_t v_itbl[2][BLOCK_SIZE];
    
    uint8_t v_rootinodeblk[BLOCK_SIZE];
    
    uint8_t v_rootdirblk[BLOCK_SIZE];
    
    
    uint32_t v_rootdir_blockno = 0;
    

    replay_committed_to_virtual(v_inodebm, v_itbl, v_rootinodeblk, v_rootdirblk, &v_rootdir_blockno);
    

    struct inode *vroot = (struct inode *)v_itbl[0];
    
    v_rootdir_blockno = vroot->direct[0];


// Free inode allocation create

    int ino = find_free_bit(v_inodebm, (int)sb.inode_count);
    
    if (ino < 0) die("no free inode");
    
    set_bit(v_inodebm, ino);

    uint32_t itblk = sb.inode_start + (uint32_t)(ino * INODE_SIZE) / BLOCK_SIZE;
    
    uint32_t itoff = (uint32_t)(ino * INODE_SIZE) % BLOCK_SIZE;
    
    uint8_t *itbuf = (itblk == sb.inode_start) ? v_itbl[0] : v_itbl[1];
    

    uint32_t now = (uint32_t)time(NULL);
    

    struct inode *ip = (struct inode *)(itbuf + itoff);
    
    memset(ip, 0, sizeof(*ip));
    
    ip->type = TYPE_FILE;
    
    ip->links = 1;
    
    ip->ctime = ip->mtime = now;
    

    struct dirent *de = (struct dirent *)v_rootdirblk;
    
    int slot = find_free_dir_slot(de);
    
    if (slot < 0) die("no free dirent slot");
    
    


    de[slot].inode = (uint32_t)ino;
    
    strncpy(de[slot].name, name, NAME_LEN - 1);
    
    de[slot].name[NAME_LEN - 1] = '\0';
    


    vroot->mtime = now;
    
    vroot->size += (uint32_t)sizeof(struct dirent);
   
   
   
// F-7 DATA record log Metadata (Done full 4096-byte block log)
    struct data_record dr;
    
    dr.hdr.type = REC_DATA;
    
    dr.hdr.size = (uint16_t)sizeof(struct data_record);
    dr.block_no = sb.inode_bitmap;
    
    memcpy(dr.data, v_inodebm, BLOCK_SIZE);
    
    if (journal_append(&dr, sizeof(dr)) < 0) return;
    


    dr.block_no = itblk;
    
    memcpy(dr.data, itbuf, BLOCK_SIZE);
    
    if (journal_append(&dr, sizeof(dr)) < 0) return;
    dr.block_no = sb.inode_start;
    
    memcpy(dr.data, v_itbl[0], BLOCK_SIZE);
    
    if (journal_append(&dr, sizeof(dr)) < 0) return;
    


    dr.block_no = v_rootdir_blockno;
    
    memcpy(dr.data, v_rootdirblk, BLOCK_SIZE);
    
    if (journal_append(&dr, sizeof(dr)) < 0) return;


// F-8 COMMIT record (Transaction seal)

    struct commit_record cr;
    
    cr.hdr.type = REC_COMMIT;
    
    cr.hdr.size = (uint16_t)sizeof(struct commit_record);
    
    if (journal_append(&cr, sizeof(cr)) < 0) return;
    

    printf("Logged create('%s') inode %d. Run install.\n", name, ino);
}
static void cmd_install(void) {

    struct journal_header jh;
    
    jhdr_read(&jh);
    

    if (jh.magic != JOURNAL_MAGIC) { fprintf(stderr, "Journal not initialized\n"); return; }
    
    if (jh.nbytes_used <= sizeof(struct journal_header)) { fprintf(stderr, "Journal empty\n"); return; }
    
    
    if (jh.nbytes_used > JOURNAL_SIZE) { fprintf(stderr, "Corrupt journal\n"); return; }

    uint8_t *buf = malloc(jh.nbytes_used);
    if (!buf) die("malloc");
    if (pread(fd, buf, jh.nbytes_used, (off_t)sb.journal_block * BLOCK_SIZE) != (ssize_t)jh.nbytes_used)
        die("pread journal");

    size_t off = sizeof(struct journal_header);
    
    size_t last_commit = 0;

    while (off + sizeof(struct rec_header) <= jh.nbytes_used) {
        struct rec_header *rh = (struct rec_header *)(buf + off);
        
        if (rh->size < sizeof(struct rec_header)) break;
        if (off + rh->size > jh.nbytes_used) break;
        
        if (rh->type == REC_COMMIT) last_commit = off + rh->size;
        off += rh->size;
        
        
    }


    off = sizeof(struct journal_header);
    
    while (off < last_commit) {
        struct rec_header *rh = (struct rec_header *)(buf + off);
        
        if (rh->size < sizeof(struct rec_header)) break;
        if (off + rh->size > last_commit) break;
        

        if (rh->type == REC_DATA) {
            struct data_record *dr = (struct data_record *)(buf + off);
            
            write_block(dr->block_no, dr->data); //Install replay
            
        }
        
        off += rh->size;
        
    }
    
    

    free(buf);
    struct journal_header empty = { .magic = JOURNAL_MAGIC, .nbytes_used = sizeof(struct journal_header) };
    
    uint8_t zero[BLOCK_SIZE];
    
    memset(zero, 0, sizeof(zero));
    
    memcpy(zero, &empty, sizeof(empty));
    
    for (int i = 0; i < JOURNAL_BLOCKS; i++)
    
        write_block(sb.journal_block + (uint32_t)i, zero);
        

    printf("Installed committed transactions and cleared journal.\n");
    
    
}


// F-2 Argument validation

int main(int argc, char **argv) {

    if (argc < 2) {
    
        fprintf(stderr, "Usage: %s create <name> | install\n", argv[0]); // Focus how user give command
        
        return 1;
        
    }


    fd = open(IMG_NAME, O_RDWR);
    
    if (fd < 0) die("open");
    

    load_super();
    
    if (sb.magic != FS_MAGIC || sb.block_size != BLOCK_SIZE) {
    
        fprintf(stderr, "Not a VSFS image\n");
        
        close(fd);
        
        return 1;
        
    }

// F-1 Command dispatch (create and install)

    if (!strcmp(argv[1], "create")) {
        if (argc != 3) { fprintf(stderr, "Usage: %s create <name>\n", argv[0]); return 1; } //check which command user check
        cmd_create(argv[2]);
        
    } else if (!strcmp(argv[1], "install")) { // put data journal to real disk
        cmd_install();
        
    } else {
        fprintf(stderr, "Unknown command\n");
        
        return 1;
        
    }
    
    

    close(fd);
    
    return 0;
    
    
}




