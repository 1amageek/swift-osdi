#include "../../Sources/OSDIPrecisionWorker/ProgramEvaluation/Frontier.h"
#include "../../Sources/OSDIPrecisionWorker/Arena/Arena.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Frontier verification requires assertions"
#endif
typedef struct {
  void *program, *setup;
  cs_precision_program p;
  cs_osdi_precision_control_v1 control;
  cs_precision_frontier_scratch scratch;
  cs_precision_frontier_result result;
  cs_precision_frontier_usage usage;
} model;
static size_t product(size_t a,size_t b){assert(!b || a<=SIZE_MAX/b);return a*b;}
static void *allocate(size_t n,size_t stride){void *p=calloc(1,product(n?n:1,stride));assert(p);return p;}
static void *read_file(const char *path,size_t *size){FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<67108864 && !fseek(f,0,SEEK_SET));*size=(size_t)n;void *p=allocate(*size,1);assert(fread(p,1,*size,f)==*size && !fclose(f));return p;}
static void open_model(model *m,const char *directory,const char *name){
  memset(m,0,sizeof(*m));char pp[4096],sp[4096];
  int n=snprintf(pp,sizeof(pp),"%s/%s.program",directory,name);assert(n>0 && (size_t)n<sizeof(pp));
  n=snprintf(sp,sizeof(sp),"%s/%s.setup",directory,name);assert(n>0 && (size_t)n<sizeof(sp));
  size_t pn,sn;m->program=read_file(pp,&pn);m->setup=read_file(sp,&sn);
  const CSOsdiPrecisionProgramHeader *h=m->program;const CSOsdiPrecisionSetupHeader *s=m->setup;
  m->control.protocol_version=2;m->control.active_count=s->active_unknown_count;m->control.raw_node_count=h->raw_node_count;
  m->control.minimum_precision_bits=128;m->control.maximum_precision_bits=256;m->control.requested_precision_bits=192;
  m->control.maximum_work_units=100000000;m->control.candidate.mode=m->control.reference.mode=2;
  assert(!cs_precision_program_open(&m->p,m->program,pn,m->setup,sn,&m->control));
  m->scratch.usage=&m->usage;
  m->scratch.event_capacity=1024;m->scratch.events=allocate(1024,sizeof(*m->scratch.events));
  for(unsigned i=0;i<8;++i){size_t count=i<4?h->raw_node_count:product(h->raw_node_count,h->raw_node_count);m->scratch.formulas.planes[i]=allocate(count,sizeof(double));m->scratch.formulas.plane_capacities[i]=count;}
}
static void close_model(model *m){free(m->program);free(m->setup);free(m->scratch.events);for(unsigned i=0;i<8;++i)free(m->scratch.formulas.planes[i]);}
static int explore(model *m,double lower,double upper,const cs_precision_source_decision *prefix,size_t count,uint64_t steps){
  double zero=0;cs_precision_frontier_request request={prefix,count,steps,100,1025};
  cs_precision_frontier_result before=m->result;
  int status=cs_precision_program_explore_frontier(&m->p,&m->control,&lower,&zero,&upper,&zero,&request,&m->scratch,&m->result);
  assert(!m->p.chart && !m->p.frontier);if(status)assert(!memcmp(&before,&m->result,sizeof(before)));return status;
}
static cs_precision_cell_integer integer(int64_t value){cs_precision_cell_integer out={0};out.negative=value<0;uint64_t magnitude=value<0?(uint64_t)(-(value+1))+1:(uint64_t)value;if(magnitude){out.count=1;out.limbs[0]=magnitude;}return out;}
static void equality(const char *dir){
  model m;open_model(&m,dir,"equality");assert(!explore(&m,1,2,NULL,0,1000));
  assert(m.result.state==CS_PRECISION_FRONTIER && m.result.event_count==1 && !m.result.planes[0]);
  cs_precision_source_decision prefix=m.result.events[0].decision;
  assert(prefix.site.kind==CS_PRECISION_SOURCE_RELATION && m.result.events[0].allowed_mask==14 && prefix.opcode==58);
  uint64_t prior=m.p.math.work;
  for(uint32_t choice=1;choice<=3;++choice){prefix.site.choice=choice;assert(!explore(&m,1,2,&prefix,1,1000));assert(m.result.state==CS_PRECISION_TERMINAL && m.result.event_count==2 && m.p.math.work>prior);prior=m.p.math.work;
    if(choice==2)assert(m.result.planes[0][0]==7 && m.result.planes[1][0]==7);else assert(m.result.planes[0][0]<=1 && m.result.planes[1][0]>=2);
  }
  prefix.site.choice=1;assert(!explore(&m,1.5,1.5,&prefix,1,1000));assert(m.result.state==CS_PRECISION_EXCLUDED && !m.result.planes[0]);
  cs_precision_source_decision surplus[2]={prefix,prefix};
  assert(explore(&m,1.5,1.5,surplus,2,1000)==CS_PRECISION_PREFIX_MALFORMED);
  prefix.site.instruction++;assert(explore(&m,1,2,&prefix,1,1000)==CS_PRECISION_PREFIX_MALFORMED);prefix.site.instruction--;
  assert(explore(&m,1,2,NULL,0,2)==CS_PRECISION_CHART_STEP_LIMIT);assert(m.usage.steps==2);
  size_t capacity=m.scratch.event_capacity;m.scratch.event_capacity=0;assert(explore(&m,1,2,NULL,0,1000)==CS_PRECISION_FRONTIER_CAPACITY);m.scratch.event_capacity=capacity;
  m.p.math.limit=m.p.math.work;assert(explore(&m,1,2,NULL,0,1000)==CS_OSDI_PRECISION_WORK_EXHAUSTED);close_model(&m);
}
static void cells(const char *dir){
  for(unsigned primitive=0;primitive<4;++primitive){const char *names[]={"floor","ceil","cast","trunc"};model m;open_model(&m,dir,names[primitive]);
    assert(!explore(&m,-1.25,1.25,NULL,0,1000));assert(m.result.state==CS_PRECISION_FRONTIER && m.result.events[0].decision.site.kind==CS_PRECISION_SOURCE_CELL);
    cs_precision_source_decision prefix=m.result.events[0].decision;int first=primitive==0?-2:-1,last=primitive==1?2:1;
    assert(m.result.events[0].cell_count==(uint64_t)(last-first+1));
    for(int i=first;i<=last;++i){prefix.cell=integer(i);assert(!explore(&m,-1.25,1.25,&prefix,1,1000));assert(m.result.state==CS_PRECISION_TERMINAL && m.result.planes[0][0]==i && m.result.planes[1][0]==i);}
    prefix.cell=integer(100);assert(!explore(&m,-1.25,1.25,&prefix,1,1000));assert(m.result.state==CS_PRECISION_EXCLUDED);
    double point=primitive==2?.5:1;assert(!explore(&m,point,point,NULL,0,1000));assert(m.result.state==CS_PRECISION_TERMINAL && m.result.planes[0][0]==1);
    if(primitive==2){assert(!explore(&m,-.5,-.5,NULL,0,1000));assert(m.result.planes[0][0]==-1);}
    if(primitive==3){assert(!explore(&m,-.75,.75,NULL,0,1000));assert(m.result.state==CS_PRECISION_TERMINAL && m.result.planes[0][0]==0);}
    close_model(&m);
  }
}
static void loops_and_undefined(const char *dir){
  model m;open_model(&m,dir,"loop");assert(!explore(&m,0,0,NULL,0,1000));assert(m.result.state==CS_PRECISION_TERMINAL && m.result.event_count==4 && m.result.planes[0][0]==21);
  cs_precision_source_decision prefix[4];for(unsigned i=0;i<4;++i)prefix[i]=m.result.events[i].decision;
  assert(prefix[0].site.instruction==prefix[2].site.instruction && prefix[0].site.ordinal!=prefix[2].site.ordinal);
  prefix[2].site.ordinal=0;assert(explore(&m,0,0,prefix,4,1000)==CS_PRECISION_PREFIX_MALFORMED);close_model(&m);
  open_model(&m,dir,"inactive");assert(!explore(&m,0,0,NULL,0,1000) && m.result.state==CS_PRECISION_TERMINAL);close_model(&m);
  open_model(&m,dir,"live_invalid");assert(explore(&m,0,0,NULL,0,1000)==CS_OSDI_PRECISION_DOMAIN);close_model(&m);
  open_model(&m,dir,"sqrt");assert(explore(&m,0,1,NULL,0,1000)==CS_OSDI_PRECISION_NONSMOOTH);close_model(&m);
}
int main(int argc,char **argv){assert(argc==2);cs_precision_arena arena;assert(cs_precision_arena_open(&arena,33554432));equality(argv[1]);cells(argv[1]);loops_and_undefined(argv[1]);puts("source frontier equality, exact arithmetic cells, prefix, loop and refusal tests passed");mpfr_free_cache();cs_precision_arena_close(&arena);return 0;}
