/* Invalid-response generator only. It never supplies numerical success evidence. */
#include "../../Sources/COSDI/include/OSDIPrecisionProtocol.h"
#include "../../Sources/COSDI/include/OSDICompilerPrecision.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
    struct stat st;if(fstat(3,&st) || st.st_size<(off_t)sizeof(cs_osdi_chart_control_v3))return 82;
    void *mapping=mmap(NULL,(size_t)st.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,3,0);if(mapping==MAP_FAILED)return 82;
    cs_osdi_chart_control_v3 *c=mapping;
    if(c->base.protocol_version!=CS_OSDI_CHART_PROTOCOL_VERSION || c->base.header_bytes!=sizeof(*c))return 82;
    const unsigned char *program=(const unsigned char *)mapping+c->base.regions[0].byte_offset;
    CSOsdiPrecisionProgramHeader h;memcpy(&h,program,sizeof(h));uint32_t positive=UINT32_MAX;
    CSOsdiPrecisionConstant primitive={0};
    for(uint32_t i=0;i<h.constants.count;++i) {
        memcpy(&primitive,program+h.constants.offset+(uint64_t)i*h.constants.stride,sizeof(primitive));
        if(primitive.type==1 && primitive.bits==UINT64_C(0x7ff0000000000000)){positive=i;break;}
    }
    if(positive==UINT32_MAX)return 82;
    c->base.response_status=0;c->base.completed_work_units=0;
    __atomic_store_n(&c->base.response_sequence,0,__ATOMIC_RELEASE);unsigned char wake=1;
    if(write(5,&wake,1)!=1 || read(4,&wake,1)!=1)return 82;
    for(unsigned i=6;i<14;++i)memset((unsigned char *)mapping+c->base.regions[i].byte_offset,0,(size_t)c->base.regions[i].element_count*8);
    cs_osdi_chart_guard_v3 *g=(void *)((unsigned char *)mapping+c->guard_region.byte_offset);memset(g,0,sizeof(*g));
    g->decision.stage=3;g->decision.kind=1;g->a=primitive.destination;g->b=UINT32_MAX;
    g->opcode=54;g->formula_truth_known=1;g->a_type=CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY;g->a_integer=positive;
    g->a_lower=g->a_upper=INFINITY;
    unsigned mode=c->base.program_sha256[0];
    if(mode==1)g->a_type=1;
    if(mode==2)g->a_lower=NAN;
    if(mode==3)g->a_integer=UINT32_MAX;
    if(mode==4){g->a_type=CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY;g->a_lower=g->a_upper=-INFINITY;}
    if(mode==5){double *f=(void *)((unsigned char *)mapping+c->base.regions[6].byte_offset);f[0]=INFINITY;}
    if(mode==6)g->a_integer=0;
    c->guard_count=1;c->completed_steps=1;c->base.completed_work_units=1;
    c->base.used_precision_bits=c->base.requested_precision_bits;c->base.response_status=0;
    uint64_t sequence=__atomic_load_n(&c->base.request_sequence,__ATOMIC_ACQUIRE);
    __atomic_store_n(&c->base.response_sequence,sequence,__ATOMIC_RELEASE);
    if(write(5,&wake,1)!=1)return 82;
    while(read(4,&wake,1)==1){}return 0;
}
