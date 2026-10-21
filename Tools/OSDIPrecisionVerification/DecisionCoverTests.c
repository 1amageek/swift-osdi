#include "../../Sources/OSDIPrecisionWorker/DecisionCover/DecisionCover.h"
#include "../../Sources/OSDIPrecisionWorker/Arena/Arena.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Decision cover verification requires assertions"
#endif
typedef struct {
  void *program,*setup;
  cs_precision_program p;
  cs_osdi_precision_control_v1 control;
  cs_precision_cover_limits limits;
  cs_precision_cover_workspace w;
  cs_precision_cover_counters counters;
  cs_precision_decision_cover result;
} model;
static size_t product(size_t a,size_t b){assert(!b || a<=SIZE_MAX/b);return a*b;}
static void *allocate(size_t n,size_t stride){void *p=calloc(1,product(n?n:1,stride));assert(p);return p;}
static void *read_file(const char *path,size_t *size){FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<67108864 && !fseek(f,0,SEEK_SET));*size=(size_t)n;void *p=allocate(*size,1);assert(fread(p,1,*size,f)==*size && !fclose(f));return p;}
static void open_model(model *m,const char *program,const char *setup,uint32_t nodes,uint32_t leaves,uint64_t work,uint64_t steps,uint32_t precision){
  memset(m,0,sizeof(*m));size_t pn,sn;m->program=read_file(program,&pn);m->setup=read_file(setup,&sn);
  const CSOsdiPrecisionProgramHeader *h=m->program;const CSOsdiPrecisionSetupHeader *s=m->setup;
  m->control.protocol_version=2;m->control.descriptor_index=h->descriptor_index;
  m->control.active_count=s->active_unknown_count;m->control.raw_node_count=h->raw_node_count;
  m->control.minimum_precision_bits=m->control.maximum_precision_bits=m->control.requested_precision_bits=precision;
  m->control.maximum_work_units=work;m->control.candidate.mode=m->control.reference.mode=2;
  assert(!cs_precision_program_open(&m->p,m->program,pn,m->setup,sn,&m->control));
  m->limits=(cs_precision_cover_limits){steps,steps,UINT64_C(268435456),100,nodes,leaves,nodes,nodes,1025};
  cs_precision_cover_workspace *w=&m->w;
  w->node_capacity=nodes;w->nodes=allocate(nodes,sizeof(*w->nodes));
  w->pending_capacity=nodes;w->pending=allocate(nodes,sizeof(*w->pending));
  w->prefix_capacity=nodes;w->prefix=allocate(nodes,sizeof(*w->prefix));
  w->replay.event_capacity=nodes;w->replay.events=allocate(nodes,sizeof(*w->replay.events));
  size_t n=h->raw_node_count,matrix=product(n,n);
  w->leaf_plane_capacity=product(leaves,4*n+4*matrix);w->leaf_planes=allocate(w->leaf_plane_capacity,sizeof(double));
  w->domain_capacity=4*m->p.active_count;if(!w->domain_capacity)w->domain_capacity=1;w->domain=allocate(w->domain_capacity,sizeof(double));
  w->node_visit_capacity=nodes;w->node_visits=allocate(nodes,sizeof(*w->node_visits));
  w->leaf_visit_capacity=leaves;w->leaf_visits=allocate(leaves,sizeof(*w->leaf_visits));
  for(unsigned i=0;i<8;++i){size_t count=i<4?n:matrix;w->replay.formulas.planes[i]=allocate(count,sizeof(double));w->replay.formulas.plane_capacities[i]=count;}
}
static void fixture(model *m,const char *dir,const char *name){char pp[4096],sp[4096];int n=snprintf(pp,sizeof(pp),"%s/%s.program",dir,name);assert(n>0 && (size_t)n<sizeof(pp));n=snprintf(sp,sizeof(sp),"%s/%s.setup",dir,name);assert(n>0 && (size_t)n<sizeof(sp));open_model(m,pp,sp,256,64,UINT64_C(100000000),100000,192);}
static void close_model(model *m){free(m->program);free(m->setup);free(m->w.nodes);free(m->w.pending);free(m->w.prefix);free(m->w.replay.events);free(m->w.leaf_planes);free(m->w.domain);free(m->w.node_visits);free(m->w.leaf_visits);for(unsigned i=0;i<8;++i)free(m->w.replay.formulas.planes[i]);}
static int construct(model *m,double lower,double upper){double zero=0;cs_precision_decision_cover before=m->result;int status=cs_precision_decision_cover_construct(&m->p,&m->control,&lower,&zero,&upper,&zero,&m->limits,&m->w,&m->counters,&m->result);assert(!m->p.chart && !m->p.frontier);if(status)assert(!memcmp(&before,&m->result,sizeof(before)));return status;}
static int check(model *m,double lower,double upper){double zero=0;return cs_precision_decision_cover_validate(&m->p,&m->control,&lower,&zero,&upper,&zero,&m->limits,&m->w,&m->counters,&m->result);}
static void equality(const char *dir){
  const char *names[]={"equality","strict","nonstrict"};
  for(unsigned k=0;k<3;++k){model m;fixture(&m,dir,names[k]);assert(!construct(&m,1,2));
    assert(m.result.node_count==7 && m.result.leaf_count==3 && m.w.nodes[0].child_count==3);
    assert(m.w.nodes[0].event.allowed_mask==14 && !m.w.nodes[0].event.decision.site.choice);
    for(uint32_t i=0;i<m.result.node_count;++i)if(m.w.nodes[i].kind==CS_PRECISION_COVER_TERMINAL){
      const cs_precision_cover_node *selection=&m.w.nodes[m.w.nodes[i].parent];
      uint32_t relation=selection->incoming.site.choice;
      int selected=k==0?relation==2:k==1?relation==1:relation!=1;
      size_t offset=(size_t)m.w.nodes[i].leaf*m.result.plane_stride;
      assert(selected?(m.w.leaf_planes[offset]==7 && m.w.leaf_planes[offset+1]==7):
        (m.w.leaf_planes[offset]<=1 && m.w.leaf_planes[offset+1]>=2));
    }
    uint64_t prior=m.counters.steps;assert(!check(&m,1,2) && m.counters.steps>prior);close_model(&m);
  }
}
static void forgeries(const char *dir){
  model m;fixture(&m,dir,"equality");assert(!construct(&m,1,2));
  cs_precision_cover_node saved=m.w.nodes[0];
  m.w.nodes[0].child_count--;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  m.w.nodes[0].child_count++;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  m.w.nodes[0].kind=CS_PRECISION_COVER_PENDING;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  m.w.nodes[0].event.allowed_mask=6;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  m.w.nodes[0].event.decision.site.instruction++;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  m.w.nodes[0].event.operands.a_integer++;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[0]=saved;
  uint32_t first=saved.first_child;cs_precision_cover_node child=m.w.nodes[first];
  m.w.nodes[first].incoming=m.w.nodes[first+1].incoming;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[first]=child;
  m.w.nodes[first].parent=first;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.nodes[first]=child;
  uint32_t terminal=0;for(uint32_t i=0;i<m.result.node_count;++i)if(m.w.nodes[i].kind==CS_PRECISION_COVER_TERMINAL){terminal=i;break;}
  child=m.w.nodes[terminal];m.w.nodes[terminal].kind=CS_PRECISION_COVER_EXCLUSION;m.w.nodes[terminal].leaf=UINT32_MAX;
  /* Keep leaf incidence otherwise valid so forged exclusion reaches replay. */
  uint32_t old_leaves=m.result.leaf_count;uint32_t removed=child.leaf;
  for(uint32_t i=0;i<m.result.node_count;++i)if(m.w.nodes[i].kind==CS_PRECISION_COVER_TERMINAL && m.w.nodes[i].leaf>removed)m.w.nodes[i].leaf--;
  m.result.leaf_count--;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);
  for(uint32_t i=0;i<m.result.node_count;++i)if(m.w.nodes[i].kind==CS_PRECISION_COVER_TERMINAL && m.w.nodes[i].leaf>=removed)m.w.nodes[i].leaf++;
  m.result.leaf_count=old_leaves;m.w.nodes[terminal]=child;
  double value=m.w.leaf_planes[0];m.w.leaf_planes[0]=123;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.w.leaf_planes[0]=value;
  assert(check(&m,1,3)==CS_PRECISION_COVER_INVALID);
  m.result.control.candidate.time_high=1;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.result.control.candidate.time_high=0;
  const CSOsdiPrecisionSetupHeader *setup=m.result.setup_bytes;m.result.setup_bytes=NULL;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.result.setup_bytes=setup;
  const CSOsdiPrecisionProgramHeader *program=m.result.program_bytes;m.result.program_bytes=NULL;assert(check(&m,1,2)==CS_PRECISION_COVER_INVALID);m.result.program_bytes=program;
  assert(!check(&m,1,2));close_model(&m);
}
static void cells_loops_and_refusals(const char *dir){
  const char *names[]={"floor","ceil","cast","trunc"};
  for(unsigned k=0;k<4;++k){model m;fixture(&m,dir,names[k]);assert(!construct(&m,-1.25,1.25));assert(m.result.leaf_count==(k<2?4:3));assert(!check(&m,-1.25,1.25));close_model(&m);}
  model m;fixture(&m,dir,"loop");assert(!construct(&m,0,0) && m.result.leaf_count==1 && m.result.node_count==5);close_model(&m);
  fixture(&m,dir,"inactive");assert(!construct(&m,0,0));close_model(&m);
  fixture(&m,dir,"correlated_invalid");assert(construct(&m,-1,1)==CS_OSDI_PRECISION_DOMAIN);assert(m.counters.steps && m.counters.guard_records);close_model(&m);
  fixture(&m,dir,"sqrt");assert(construct(&m,0,1)==CS_OSDI_PRECISION_NONSMOOTH);assert(m.counters.steps);close_model(&m);
  fixture(&m,dir,"equality");cs_precision_cover_node *nodes=m.w.nodes;
  m.w.nodes=(void *)m.w.replay.events;assert(construct(&m,1,2)==CS_OSDI_PRECISION_INVALID_INPUT);m.w.nodes=nodes;
  double zero=0,one=1;
  cs_precision_decision_cover *overlap=(void *)m.w.nodes;
  uint64_t before=m.p.math.work;
  assert(cs_precision_decision_cover_construct(&m.p,&m.control,&zero,&zero,&one,&zero,&m.limits,&m.w,&m.counters,overlap)==CS_OSDI_PRECISION_INVALID_INPUT);
  assert(cs_precision_decision_cover_validate(&m.p,&m.control,&zero,&zero,&one,&zero,&m.limits,&m.w,&m.counters,overlap)==CS_OSDI_PRECISION_INVALID_INPUT);
  assert(m.p.math.work==before);
  cs_precision_decision_cover input_alias={0};
  const double *overlapping_input=(const void *)&input_alias;
  assert(cs_precision_decision_cover_construct(&m.p,&m.control,overlapping_input,&zero,&one,&zero,&m.limits,&m.w,&m.counters,&input_alias)==CS_OSDI_PRECISION_INVALID_INPUT);
  assert(cs_precision_decision_cover_validate(&m.p,&m.control,overlapping_input,&zero,&one,&zero,&m.limits,&m.w,&m.counters,&input_alias)==CS_PRECISION_COVER_INVALID);
  assert(m.p.math.work==before);
  assert(cs_precision_decision_cover_construct(&m.p,&m.control,m.w.domain,&zero,&one,&zero,&m.limits,&m.w,&m.counters,&m.result)==CS_OSDI_PRECISION_INVALID_INPUT);
  assert(!construct(&m,0,1));
  const double *candidate_input=(const void *)&m.result.control.completed_work_units;
  before=m.p.math.work;
  assert(cs_precision_decision_cover_validate(&m.p,&m.control,candidate_input,&zero,&one,&zero,&m.limits,&m.w,&m.counters,&m.result)==CS_OSDI_PRECISION_INVALID_INPUT);
  assert(m.p.math.work==before);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_nodes=2;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_pending=2;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_depth=1;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_leaves=2;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_copied_bytes=10;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_guard_records=1;assert(construct(&m,1,2)==CS_PRECISION_COVER_CAPACITY);assert(m.counters.guard_records==1);close_model(&m);
  fixture(&m,dir,"equality");m.limits.maximum_steps=2;assert(construct(&m,1,2)==CS_PRECISION_CHART_STEP_LIMIT);assert(m.counters.steps==2);assert(construct(&m,1,2)==CS_PRECISION_CHART_STEP_LIMIT);close_model(&m);
  fixture(&m,dir,"equality");m.counters.steps=m.limits.maximum_steps;assert(construct(&m,1,2)==CS_PRECISION_CHART_STEP_LIMIT);close_model(&m);
  fixture(&m,dir,"equality");assert(!construct(&m,1,2));m.limits.maximum_copied_bytes=m.counters.copied_bytes;assert(check(&m,1,2)==CS_PRECISION_COVER_CAPACITY);close_model(&m);
}
static uint64_t number(const char *s){char *end;uint64_t n=strtoull(s,&end,10);assert(n && end!=s && !*end);return n;}
static void actual(char **argv){
  size_t bytes;void *data=read_file(argv[4],&bytes);assert(bytes>=sizeof(cs_osdi_precision_control_v1));
  cs_osdi_precision_control_v1 original;memcpy(&original,data,sizeof(original));
  uint64_t capacity=number(argv[8]);assert(capacity<UINT32_MAX);
  model m;open_model(&m,argv[2],argv[3],(uint32_t)capacity,16,number(argv[6]),number(argv[7]),original.maximum_precision_bits);
  m.control.candidate=m.control.reference=original.candidate;m.control.minimum_precision_bits=original.minimum_precision_bits;
  m.control.requested_precision_bits=original.requested_precision_bits;
  size_t n=(size_t)m.p.active_count;assert(bytes==sizeof(original)+product(4*n,sizeof(double)));
  const double *v=(const void *)((const unsigned char *)data+sizeof(original));
  int status=cs_precision_decision_cover_construct(&m.p,&m.control,v,v+n,v,v+n,&m.limits,&m.w,&m.counters,&m.result);
  if(status)fprintf(stderr,"actual cover refusal: status=%d instruction=%u steps=%" PRIu64 " guards=%" PRIu64 " copies=%" PRIu64 " work=%" PRIu64 "\n",status,m.p.detail,m.counters.steps,m.counters.guard_records,m.counters.copied_bytes,m.p.math.work);
  assert(!status && m.result.leaf_count>=1);
  printf("actual complete cover: %u nodes, %u leaves, %" PRIu64 " steps, %" PRIu64 " guards, %" PRIu64 " copied bytes, %" PRIu64 " work\n",m.result.node_count,m.result.leaf_count,m.counters.steps,m.counters.guard_records,m.counters.copied_bytes,m.p.math.work);
  free(data);close_model(&m);
}
int main(int argc,char **argv){assert(argc==2 || argc==9);cs_precision_arena arena;assert(cs_precision_arena_open(&arena,argc==9?number(argv[5]):UINT64_C(67108864)));equality(argv[1]);forgeries(argv[1]);cells_loops_and_refusals(argv[1]);puts("decision cover independent source replay, partition, identity, refusal and quota fixtures passed");if(argc==9)actual(argv);mpfr_free_cache();cs_precision_arena_close(&arena);return 0;}
