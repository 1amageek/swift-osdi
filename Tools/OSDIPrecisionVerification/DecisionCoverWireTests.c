/* Direct real-worker protocol falsifiers. Numeric success is tested separately. */
#include "../../Sources/COSDI/include/OSDIPrecisionProtocol.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef NDEBUG
#error "Cover wire verification requires enabled assertions"
#endif
extern char **environ;
static unsigned char *read_file(const char *path,size_t *bytes) {
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);
    assert(n>0 && n<1048576 && !fseek(f,0,SEEK_SET));*bytes=(size_t)n;
    unsigned char *data=malloc(*bytes);assert(data && fread(data,1,*bytes,f)==*bytes && !fclose(f));return data;
}
static void region(cs_osdi_precision_region_v1 *out,uint64_t count,uint64_t stride,size_t *bytes) {
    assert(*bytes<=SIZE_MAX-7);*bytes=(*bytes+7)&~(size_t)7;
    assert(!stride || count<=(SIZE_MAX-*bytes)/stride);
    *out=(cs_osdi_precision_region_v1){*bytes,count};*bytes+=(size_t)(count*stride);
}
static int wake(int fd) {
    struct pollfd event={fd,POLLIN,0};int n;
    do {n=poll(&event,1,3000);}while(n<0 && errno==EINTR);
    assert(n==1);unsigned char byte;ssize_t count;
    do {count=read(fd,&byte,1);}while(count<0 && errno==EINTR);
    assert(count==0 || count==1);return count==1;
}
static int reaped(pid_t pid) {
    for(unsigned i=0;i<300;++i) {
        int status;pid_t result=waitpid(pid,&status,WNOHANG);
        if(result==pid) {assert(WIFEXITED(status));return WEXITSTATUS(status);}
        assert(result==0 || (result<0 && errno==EINTR));
        struct timespec delay={0,10000000};nanosleep(&delay,NULL);
    }
    assert(!"worker did not exit within three seconds");return -1;
}
int main(int argc,char **argv) {
    assert(argc==4);size_t pn,sn;unsigned char *program=read_file(argv[2],&pn),*setup=read_file(argv[3],&sn);
    for(unsigned failure=0;failure<=9;++failure) {
        cs_osdi_cover_control_v4 h={0};size_t bytes=sizeof(h);
        h.base.magic=CS_OSDI_PRECISION_MAGIC;h.base.protocol_version=CS_OSDI_COVER_PROTOCOL_VERSION;
        h.base.header_bytes=sizeof(h);h.base.byte_order=CS_OSDI_PRECISION_BYTE_ORDER;
        h.base.raw_node_count=h.base.active_count=1;h.base.arena_bytes=1048576;
        h.base.minimum_precision_bits=128;h.base.maximum_precision_bits=h.base.requested_precision_bits=192;
        h.base.maximum_work_units=1000000;h.base.response_sequence=CS_OSDI_PRECISION_INITIAL_RESPONSE;
        h.base.candidate.mode=h.base.reference.mode=2;h.limits=(cs_osdi_cover_limits_v4){10000,10000,10000000,100,100,10,100,100,1025,0};
        region(&h.base.regions[0],pn,1,&bytes);region(&h.base.regions[1],sn,1,&bytes);
        for(unsigned i=2;i<14;++i)region(&h.base.regions[i],1,8,&bytes);
        region(&h.tree_region,100,sizeof(cs_osdi_cover_node_v4),&bytes);
        region(&h.leaf_region,80,sizeof(double),&bytes);h.base.mapping_bytes=bytes;
        FILE *file=tmpfile();assert(file && !ftruncate(fileno(file),(off_t)bytes));
        void *mapping=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fileno(file),0);assert(mapping!=MAP_FAILED);
        memcpy(mapping,&h,sizeof(h));memcpy((unsigned char *)mapping+h.base.regions[0].byte_offset,program,pn);
        memcpy((unsigned char *)mapping+h.base.regions[1].byte_offset,setup,sn);
        cs_osdi_cover_control_v4 *c=mapping;
        if(failure==1)c->base.protocol_version=5;
        if(failure==2)c->base.header_bytes=sizeof(cs_osdi_precision_control_v1);
        if(failure==3)c->tree_region.byte_offset=c->base.regions[0].byte_offset;
        if(failure==4)c->leaf_region.element_count++;
        if(failure==5)c->tree_region.byte_offset=UINT64_MAX;
        if(failure==6)c->limits.maximum_nodes=UINT32_MAX;
        if(failure==7)c->limits.reserved=1;
        int request[2],response[2];assert(!pipe(request) && !pipe(response));
        int originals[]={fileno(file),request[0],request[1],response[0],response[1]};
        for(unsigned i=0;i<5;++i)assert(!fcntl(originals[i],F_SETFD,FD_CLOEXEC));
        int source[]={fileno(file),request[0],response[1]},copies[3];
        posix_spawn_file_actions_t actions;assert(!posix_spawn_file_actions_init(&actions));
        for(unsigned i=0;i<3;++i) {
            copies[i]=fcntl(source[i],F_DUPFD_CLOEXEC,10);assert(copies[i]>=10);
            assert(!posix_spawn_file_actions_adddup2(&actions,copies[i],(int)i+3));
        }
        char *args[]={argv[1],NULL};pid_t pid;
        assert(!posix_spawn(&pid,argv[1],&actions,NULL,args,environ));
        assert(!posix_spawn_file_actions_destroy(&actions));
        for(unsigned i=0;i<3;++i)close(copies[i]);close(request[0]);close(response[1]);
        int ready=wake(response[0]);
        if(failure>=1 && failure<=7)assert(!ready);
        else {
            assert(ready && __atomic_load_n(&c->base.response_sequence,__ATOMIC_ACQUIRE)==0 && !c->base.response_status);
            if(failure>=8) {
                if(failure==8)c->limits.maximum_steps++;
                if(failure==9)c->leaf_region.byte_offset+=8;
                __atomic_store_n(&c->base.request_sequence,1,__ATOMIC_RELEASE);
                assert(write(request[1],"1",1)==1);int result=wake(response[0]);
                assert(!result);
            }
        }
        close(request[1]);close(response[0]);
        int exit=reaped(pid);assert(exit==((failure>=1 && failure<=9)?CS_OSDI_PRECISION_EXIT_PROTOCOL:0));
        assert(!munmap(mapping,bytes) && !fclose(file));
    }
    free(program);free(setup);puts("complete-cover wire geometry/version/suffix/reserved tests passed");return 0;
}
