/* Invalid-response generator only. Source computation uses the real worker;
 * mutations after its successful checker are never numerical evidence. */
#include "../../Sources/OSDIPrecisionWorker/WorkerProtocol/Worker.h"
#include "../../Sources/COSDI/include/OSDIPrecisionProtocol.h"
#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
static cs_osdi_cover_control_v4 *control;
ssize_t write(int fd,const void *bytes,size_t count){
  ssize_t (*actual)(int,const void *,size_t)=(ssize_t (*)(int,const void *,size_t))dlsym(RTLD_NEXT,"write");
  if(!actual)return -1;
  if(fd==5 && count==1 && control && !control->base.response_status){
    uint64_t sequence=__atomic_load_n(&control->base.response_sequence,__ATOMIC_ACQUIRE);
    if(sequence && sequence!=CS_OSDI_PRECISION_INITIAL_RESPONSE){
      unsigned char *base=(void *)control;cs_osdi_cover_node_v4 *nodes=(void *)(base+control->tree_region.byte_offset);
      double *planes=(void *)(base+control->leaf_region.byte_offset);unsigned mode=control->base.program_sha256[0];
      uint32_t first=nodes[0].first_child,terminal=0;
      for(uint32_t i=0;i<control->node_count;++i)if(nodes[i].kind==CS_OSDI_COVER_TERMINAL){terminal=i;break;}
      if(mode==1)nodes[0].kind=0;
      if(mode==2)nodes[0].child_count--;
      if(mode==3)nodes[first].incoming=nodes[first+1].incoming;
      if(mode==4)nodes[0].event.allowed_mask=6;
      if(mode==5)planes[0]=NAN;
      if(mode==6)nodes[first].parent=first;
      if(mode==7){for(uint32_t i=terminal+1;i<control->node_count;++i)if(nodes[i].kind==CS_OSDI_COVER_TERMINAL){nodes[i].leaf=nodes[terminal].leaf;break;}}
      if(mode==8)nodes[0].event.first_cell.limbs[0]=1;
      if(mode==9){nodes[0].event.operands.a_type=CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY;nodes[0].event.operands.a_integer=UINT32_MAX;nodes[0].event.operands.a_lower=nodes[0].event.operands.a_upper=INFINITY;}
      if(mode==10)control->limits.maximum_nodes++;
      if(mode==11){double *lower=(void *)(base+control->base.regions[2].byte_offset);lower[0]=123;}
      if(mode==12)control->completed_steps=0;
      if(mode==13){nodes[0].event.decision.site.instruction++;nodes[0].event.operands.decision.instruction++;}
      if(mode==14)nodes[0].event.first_cell.negative=1;
      if(mode==15)control->leaf_count--;
      if(mode==16){uint32_t removed=nodes[terminal].leaf;nodes[terminal].kind=3;nodes[terminal].leaf=UINT32_MAX;
        for(uint32_t i=0;i<control->node_count;++i)if(nodes[i].kind==CS_OSDI_COVER_TERMINAL && nodes[i].leaf>removed)nodes[i].leaf--;
        uint64_t n=control->base.raw_node_count,stride=4*n+4*n*n;
        memmove(planes+(uint64_t)removed*stride,planes+(uint64_t)(removed+1)*stride,(size_t)(control->leaf_count-removed-1)*(size_t)stride*sizeof(double));
        control->leaf_count--;}
      if(mode==17)control->completed_guard_records=0;
      if(mode==18)control->completed_copied_bytes=1;
      if(mode==19)control->base.completed_work_units=1;
      __atomic_store_n(&control->base.response_sequence,sequence,__ATOMIC_RELEASE);
    }
  }
  return actual(fd,bytes,count);
}
int main(void){struct stat st;if(fstat(3,&st) || st.st_size<(off_t)sizeof(*control))return 82;
  void *mapping=mmap(NULL,(size_t)st.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,3,0);if(mapping==MAP_FAILED)return 82;
  control=mapping;if(control->base.protocol_version!=CS_OSDI_COVER_PROTOCOL_VERSION)return 82;
  int status=cs_osdi_precision_worker_run(3,4,5);control=NULL;munmap(mapping,(size_t)st.st_size);return status;
}
