#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdint.h>

#define CHUNK_SIZE              (4*1024*1024)
#define ZONE_OFFSET             8
#define MIN_SPARE               24

typedef struct _fnode {
    uint64_t sz;
    struct _fnode* fnext;
    struct _fnode* fprev;
} fnode;

fnode *head;
void *chunk_start;
uint32_t chunk_count;

// Node added always on head
static void addNode(fnode *adN)
{
    adN->fprev = NULL;
    adN->fnext = head;

    if(head != NULL)
        head->fprev = adN;
    
    head = adN;
}

static void remNode(fnode *rmN)
{
    if (rmN == NULL)
        return;

    if (rmN == head)
        head = rmN->fnext;
    if (rmN->fnext != NULL)
        rmN->fnext->fprev = rmN->fprev;
    if (rmN->fprev != NULL)
        rmN->fprev->fnext = rmN->fnext;
}

// assuming mmap call never fails despite contiguous alloc requirement
static void* getChunk(uint32_t count, void *loc)
{
    chunk_count += count;
    if (loc == NULL) {
        return mmap(NULL, count*CHUNK_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    0, 0);
    } else {
        return mmap(loc, count*CHUNK_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                    0, 0);
    }
}

static uint64_t getPaddsz(uint64_t sz)
{
    return ((sz + (ZONE_OFFSET - 1)) & (-ZONE_OFFSET));
}

static uint32_t getChunkCount(uint64_t sz)
{
    return (sz % CHUNK_SIZE) ? ((sz / CHUNK_SIZE) + 1) :
                                (sz / CHUNK_SIZE);

}

// puts suitable size fnode in arg 'freeAddr'; return status
// allocation is always 8-byte alligned (multiple of 8 bytes)
// 'suitable alloc size' => 8byte_size + req_size + 8B_padding
// if a free node is found with exact above size then we alloc
// if a free node is found i.e. larger by 'b'Bytes, then we
// split by Rules:
// if b<24, becomes 'extra padding' and the node is alloc as is
// if b>=24, then split into 2 fnodes, left alloc + right added
// to the fnode list
// if no such fnode is found mmap is used to get minimum chunks
// of 4MB from the OS to satify that request
// New 4MB fnode created after a mmap is used instead of first
// merging it with the last found insufficient fnode
// NO merging of free node happens during memalloc
static int getFnode(uint64_t sz, fnode** freeAddr)
{
    // find suitable node from free list
    uint64_t ssz = getPaddsz(sz + ZONE_OFFSET);

    void *tchunk = NULL;
    uint32_t curr_chunk_cnt = chunk_count;
    fnode *split = NULL;
    fnode *t = head;

    if (head == NULL) {
        if (chunk_count == 0) {
            chunk_start = getChunk(getChunkCount(ssz), NULL);
            if (chunk_start == (void *) -1) {
                printf("%s:%d mmap failed\n", __FILE__, __LINE__);
                return -1;
            }
            if ((getChunkCount(ssz)*CHUNK_SIZE) - ssz >= MIN_SPARE) {
                split = (fnode *)((char *)chunk_start + ssz);
                split->sz = chunk_count*CHUNK_SIZE - ssz; 
                addNode(split);
                ((fnode *)chunk_start)->sz = ssz;
            } else {
                // as the spare bytes size is less than min spare
                // whole of alloced chunk is used as allocated space
                ((fnode *)chunk_start)->sz = (uint64_t)(getChunkCount(ssz)*CHUNK_SIZE);
            }
            *freeAddr = (fnode *)chunk_start;
            return 0;
        } else {
            curr_chunk_cnt = chunk_count;
            tchunk = getChunk(getChunkCount(ssz),
                              ((char *)chunk_start + chunk_count*CHUNK_SIZE));
            if (tchunk == (void *) -1) {
                printf("%s:%d mmap failed\n", __FILE__, __LINE__);
                return -1;
            }
            if ((getChunkCount(ssz)*CHUNK_SIZE) - ssz >= MIN_SPARE) {
                split = (fnode *)((char *)chunk_start + (curr_chunk_cnt*CHUNK_SIZE) + ssz);
                split->sz = chunk_count*CHUNK_SIZE - ((curr_chunk_cnt*CHUNK_SIZE) + ssz); 
                addNode(split);
                ((fnode *)tchunk)->sz = ssz;
            } else {
                // as the spare bytes size is less than min spare
                // whole of alloced chunk is used as allocated space
                ((fnode *)tchunk)->sz = (uint64_t)(getChunkCount(ssz)*CHUNK_SIZE);
            }
            *freeAddr = (fnode *)tchunk;
            return 0;
        }

    } else {
        while(t) {
            if(t->sz >= ssz) {
                if (t->sz - ssz >= MIN_SPARE) {
                    split = (fnode *)((char *)t + ssz);
                    split->sz = (t->sz - ssz);
                    addNode(split);
                    remNode(t);
                    t->sz = ssz;
                    *freeAddr = t;
                } else {
                    remNode(t);
                    *freeAddr = t;
                }
                return 0;
            }
            t = t->fnext;
        }
        // couldn't find a node with suitable size
        curr_chunk_cnt = chunk_count;
        tchunk = getChunk(getChunkCount(ssz),
                          ((char *)chunk_start + chunk_count*CHUNK_SIZE));
        if (tchunk == (void *) -1) {
            printf("%s:%d mmap failed\n", __FILE__, __LINE__);
            return -1;
        }
        if ((getChunkCount(ssz)*CHUNK_SIZE) - ssz >= MIN_SPARE) {
            split = (fnode *)((char *)chunk_start + (curr_chunk_cnt*CHUNK_SIZE) + ssz);
            split->sz = chunk_count*CHUNK_SIZE - ((curr_chunk_cnt*CHUNK_SIZE) + ssz); 
            addNode(split);
            ((fnode *)tchunk)->sz = ssz;
        } else {
            // as the spare bytes size is less than min spare
            // whole of alloced chunk is used as allocated space
            ((fnode *)tchunk)->sz = (uint64_t)(getChunkCount(ssz)*CHUNK_SIZE);
        }
        *freeAddr = (fnode *)tchunk;
        return 0;
    }
}

/* Implementation assumes:
 *    - valid addresses are passed
 *    - no double free
 */
void getInMemAdjNode(void *faddr, uint64_t fsz, fnode *adjNs)
{
    fnode *t = head;
    while (t) {
        if ((void *)((char *)t + t->sz) == faddr) {
            // fnode is in-mem-left of free addr
            adjNs->fprev = t;
            // remove it from the free list
            remNode(t);
        } else if ((void *)((char *)faddr + fsz) == (void *)t) {
            // fnode is in-mem-right of free addr
            adjNs->fnext = t;
            // remove it from the free list
            remNode(t);
        }
        t = t->fnext;
    }
}

// add fnode to free list, returns status
// Rules:
// Newly freed nodes are always added as head
// in-mem-adjacent free nodes are merged
static int addFnode(void* faddr)
{
    if(faddr == NULL || head == NULL)
        return -1;

    void* fa = (void *)((char *)faddr - ZONE_OFFSET);
    uint64_t fsz = *((uint64_t*)fa);

    fnode adjNs = { 0 };
    getInMemAdjNode(fa, fsz, &adjNs);

    if (adjNs.fprev && adjNs.fnext) {
        adjNs.fprev->sz += fsz + adjNs.fnext->sz;
        addNode(adjNs.fprev);
    } else if (adjNs.fprev && adjNs.fnext == NULL) {
        adjNs.fprev->sz += fsz;
        addNode(adjNs.fprev);
    } else if (adjNs.fprev == NULL && adjNs.fnext) {
        ((fnode *)fa)->sz = fsz + adjNs.fnext->sz;
        addNode((fnode *)fa);
    } else if (adjNs.fprev == NULL && adjNs.fnext == NULL) {
        addNode((fnode *)fa);
    }

	return 0;
}


/* Takes number of bytes as argument */
void* memalloc(unsigned long size) 
{
	printf("memalloc() called\n");
    if (size == 0)
        return NULL;    // if this func can't satify the req

    fnode *res = NULL;
    if (getFnode((uint64_t)size, &res) == -1)
        return NULL;

    return (void*)((char *)res + ZONE_OFFSET);
}

/* Implementation assumes:
 *    - valid addresses are passed
 *    - no double free
 */
int memfree(void *ptr)
{
	printf("memfree() called\n");
    return addFnode(ptr);
}