#include <time.h>
/* Reuse the existing exact program/cover fixture loader, not its proof logic. */
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
static uint64_t verifier_host_allocations;
static void *verifier_calloc(size_t count,size_t stride){
  verifier_host_allocations++;return calloc(count,stride);
}
#define calloc verifier_calloc
#define main cs_existing_cover_verification_main
#include "DecisionCoverTests.c"
#undef main
#undef calloc
#include "../../Sources/OSDIPrecisionWorker/ConstitutiveIdentities/ConstitutiveIdentities.h"

typedef struct {
  CSOsdiPrecisionProgramHeader program;
  CSOsdiPrecisionSetupHeader setup;
  cs_osdi_precision_control_v1 original;
  uint64_t program_bytes,setup_bytes,request_bytes,outside_arena_bytes,identity_fixed_upper;
} identity_capture;
static int header_file(const char *path,void *header,size_t header_size,uint64_t *bytes){
  FILE *f=fopen(path,"rb");if(!f)return CS_IDENTITY_INVALID;
  int status=CS_IDENTITY_INVALID;
  if(!fseek(f,0,SEEK_END)){
    long size=ftell(f);
    if(size>0&&(uint64_t)size>=header_size&&(uint64_t)size<UINT64_C(67108864)&&
        !fseek(f,0,SEEK_SET)&&fread(header,1,header_size,f)==header_size){*bytes=(uint64_t)size;status=0;}
  }
  if(fclose(f))status=CS_IDENTITY_INVALID;return status;
}
static int add_payload(uint64_t *sum,uint64_t count,uint64_t stride,uint64_t ceiling){
  if(stride&&count>UINT64_MAX/stride)return CS_IDENTITY_CAPACITY;
  uint64_t bytes=count*stride;
  if(*sum>ceiling||bytes>ceiling-*sum)return CS_IDENTITY_CAPACITY;
  *sum+=bytes;return 0;
}
static int capture_preflight(const char *program,const char *setup,const char *request,
    uint64_t arena_bytes,uint64_t host_ceiling,uint64_t workspace_ceiling,
    uint32_t nodes,uint32_t atoms,identity_capture *capture){
  memset(capture,0,sizeof(*capture));int status=header_file(program,&capture->program,sizeof(capture->program),&capture->program_bytes);
  if(status)return status;
  status=header_file(setup,&capture->setup,sizeof(capture->setup),&capture->setup_bytes);if(status)return status;
  status=header_file(request,&capture->original,sizeof(capture->original),&capture->request_bytes);if(status)return status;
  const cs_osdi_precision_control_v1 *c=&capture->original;
  const CSOsdiPrecisionProgramHeader *h=&capture->program;
  if(memcmp(h->magic,CS_OSDI_PRECISION_PROGRAM_MAGIC,8)||h->version!=1||h->header_bytes!=sizeof(*h)||
      h->program_bytes!=capture->program_bytes||h->flags!=CS_OSDI_PRECISION_STATELESS||h->native_state_count||
      capture->setup.version!=1||capture->setup.header_bytes!=sizeof(capture->setup)||
      capture->setup.setup_bytes!=capture->setup_bytes)return CS_IDENTITY_INVALID;
  if(!isfinite(c->candidate.time_high)||!isfinite(c->candidate.time_low)||
      !isfinite(c->reference.time_high)||!isfinite(c->reference.time_low)||
      c->candidate.time_high+c->candidate.time_low!=c->candidate.time_high||
      c->reference.time_high+c->reference.time_low!=c->reference.time_high||
      c->candidate.time_high>c->reference.time_high||
      (c->candidate.time_high==c->reference.time_high&&c->candidate.time_low>c->reference.time_low))
    return CS_IDENTITY_INVALID;
  if(c->magic!=CS_OSDI_PRECISION_MAGIC||c->protocol_version!=CS_OSDI_RANGE_PROTOCOL_VERSION||
      c->header_bytes!=sizeof(*c)||c->byte_order!=CS_OSDI_PRECISION_BYTE_ORDER||
      c->raw_node_count!=capture->program.raw_node_count||c->raw_node_count!=capture->setup.raw_to_active.count||
      c->active_count!=capture->setup.active_unknown_count||c->active_count>c->raw_node_count||
      c->descriptor_index!=capture->program.descriptor_index||
      c->minimum_precision_bits<53||c->minimum_precision_bits>c->requested_precision_bits||
      c->requested_precision_bits>c->maximum_precision_bits||
      c->candidate.mode!=CS_OSDI_PRECISION_TRANSIENT||c->reference.mode!=c->candidate.mode||
      c->candidate.reserved||c->reference.reserved)return CS_IDENTITY_INVALID;
  uint64_t expected=sizeof(*c);status=add_payload(&expected,c->active_count,4*sizeof(double),UINT64_MAX);
  if(status||expected!=capture->request_bytes)return CS_IDENTITY_INVALID;
  uint64_t n=c->raw_node_count,m=c->active_count,stride=0;
  status=add_payload(&stride,n,4,UINT64_MAX);if(status)return status;
  if(n&&n>UINT64_MAX/n)return CS_IDENTITY_CAPACITY;
  status=add_payload(&stride,n*n,4,UINT64_MAX);if(status)return status;
  uint64_t outside=0;
#define HOST(count,type) do{status=add_payload(&outside,count,sizeof(type),host_ceiling);if(status)return status;}while(0)
  HOST(capture->program_bytes,unsigned char);HOST(capture->setup_bytes,unsigned char);HOST(capture->request_bytes,unsigned char);
  HOST(20000,cs_precision_cover_node);HOST(20000,uint32_t);HOST(20000,cs_precision_source_decision);
  HOST(20000,cs_precision_source_event);HOST(20000,uint32_t);HOST(64,uint32_t);
  if(stride>UINT64_MAX/64)return CS_IDENTITY_CAPACITY;
  HOST(stride*64,double);HOST(m?4*m:1,double);HOST(stride,double);
  HOST(4*m,double); /* Concurrent identity_with_box immutable input copy. */
#undef HOST
  capture->outside_arena_bytes=outside;
  /* Identity tables and all GMP/MPFR peaks are INSIDE this one admitted arena;
     include that reservation once rather than double-counting its subpools. */
  status=add_payload(&outside,arena_bytes,1,host_ceiling);if(status)return status;
  uint64_t fixed=0,nodes_count=(uint64_t)nodes+1,rows=c->raw_node_count;
#define FIXED(count,type) do{status=add_payload(&fixed,count,sizeof(type),workspace_ceiling);if(status)return status;}while(0)
  FIXED(nodes_count,cs_precision_constitutive_node);FIXED(capture->program.registers.count,uint32_t);
  FIXED(capture->program.phi_edges.count,uint32_t); /* Conservative maximum phi staging. */
  FIXED(capture->program.roots.count,uint32_t);FIXED(atoms,uint32_t);FIXED(nodes_count,uint32_t);
  FIXED(nodes_count,mpq_t);FIXED(nodes_count,mpq_t);
  if(rows&&((uint64_t)atoms+1)>UINT64_MAX/rows)return CS_IDENTITY_CAPACITY;
  FIXED(rows*((uint64_t)atoms+1),mpq_t);
  if(rows&&rows>UINT64_MAX/rows)return CS_IDENTITY_CAPACITY;
  FIXED(rows*rows,mpq_t);FIXED(rows*rows,mpq_t);
  if(rows*rows>UINT64_MAX/2||rows>UINT32_MAX/2)return CS_IDENTITY_CAPACITY;
  FIXED(2*rows*rows,mpq_t);FIXED(2*rows,cs_precision_constitutive_claim);
  FIXED(nodes_count,uint8_t);FIXED(nodes_count,uint8_t);FIXED(rows*((uint64_t)atoms+1),uint8_t);
  FIXED(rows*rows,uint8_t);FIXED(rows*rows,uint8_t);FIXED(2*rows*rows,uint8_t);
  for(unsigned q=0;q<4;++q){FIXED(m?m:1,uint32_t);}
#undef FIXED
  capture->identity_fixed_upper=fixed;
  return fixed>arena_bytes?CS_IDENTITY_CAPACITY:0;
}
static void large_sparse_preflight(const char *dir){
  char program[4096],setup[4096],request[4096];
  assert(snprintf(program,sizeof(program),"%s/sparse1000.program",dir)>0);
  assert(snprintf(setup,sizeof(setup),"%s/sparse1000.setup",dir)>0);
  assert(snprintf(request,sizeof(request),"%s/sparse1000.request.bin",dir)>0);
  identity_capture capture;uint64_t before=verifier_host_allocations;
  assert(capture_preflight(program,setup,request,UINT64_C(536870912),UINT64_C(1073741824),
    UINT64_C(536870912),4096,16,&capture)==CS_IDENTITY_CAPACITY);
  assert(capture.program.raw_node_count==1000&&verifier_host_allocations==before);
}

static cs_precision_constitutive_limits identity_limits(model *m,uint64_t coefficient_bits){
  uint64_t nodes=(uint64_t)m->p.header->instructions.count+m->p.header->constants.count+
    m->p.header->inputs.count+1024;
  assert(nodes<UINT32_MAX);
  return (cs_precision_constitutive_limits){(uint32_t)nodes,4096,
    2*m->p.header->raw_node_count,coefficient_bits,UINT64_C(134217728)};
}
/* Cover validation forbids using its mutable domain pool as caller input. */
static int identity_with_box(model *m,cs_precision_constitutive_workspace *w,
    cs_precision_constitutive_identities *result,int validate){
  size_t n=(size_t)m->p.active_count;double *box=allocate(n*4,sizeof(double));
  memcpy(box,m->w.domain,n*4*sizeof(double));
  int status=validate?cs_precision_constitutive_validate(&m->p,&m->control,box,box+n,box+2*n,box+3*n,
      &m->limits,&m->w,&m->counters,&m->result,w,result):
    cs_precision_constitutive_construct(&m->p,&m->control,box,box+n,box+2*n,box+3*n,
      &m->limits,&m->w,&m->counters,&m->result,w,result);
  free(box);assert(!m->p.source_observer);return status;
}
static void three_row_model(model *m,const char *dir,const char *name){
  char pp[4096],sp[4096];assert(snprintf(pp,sizeof(pp),"%s/%s.program",dir,name)>0);
  assert(snprintf(sp,sizeof(sp),"%s/%s.setup",dir,name)>0);
  open_model(m,pp,sp,256,64,UINT64_C(1000000000),1000000,192);
  size_t n=(size_t)m->p.active_count;
  double *lo=allocate(n,sizeof(double)),*hi=allocate(n,sizeof(double)),*zero=allocate(n,sizeof(double));
  lo[0]=-1;hi[0]=1;
  if(!strcmp(name,"common_kernel"))for(size_t i=0;i<3;++i){lo[i]=-1;hi[i]=1;}
  int status=cs_precision_decision_cover_construct(&m->p,&m->control,lo,zero,hi,zero,&m->limits,&m->w,&m->counters,&m->result);
  if(status){fprintf(stderr,"source-fixture-cover name=%s status=%d detail=%u work=%" PRIu64 " steps=%" PRIu64 "\n",
      name,status,m->p.detail,m->p.math.work,m->counters.steps);fflush(stderr);}
  assert(!status);
  free(lo);free(hi);free(zero);
}
static void selected_claim(model *m,cs_precision_constitutive_workspace *w,
    cs_precision_constitutive_identities *proof,uint32_t kind,long a,long b,long c){
  assert(m->p.header->raw_node_count==3);w->claims[0].kind=kind;
  mpq_set_si(w->claims[0].coefficients[0],a,1);mpq_set_si(w->claims[0].coefficients[1],b,1);
  mpq_set_si(w->claims[0].coefficients[2],c,1);proof->claims=w->claims;proof->claim_count=1;
}
/* Build tiny source variants from the admitted fixture's ABI tables. Only the
   source input/root inventories and exact setup quotient differ; no evaluated
   value or Jacobian is used to choose an identity. */
static void write_probe_fixture(const char *dir,const char *name,unsigned quotient){
  char path[4096];assert(snprintf(path,sizeof(path),"%s/conservation.program",dir)>0);
  size_t original_bytes;unsigned char *original=read_file(path,&original_bytes);
  const CSOsdiPrecisionProgramHeader *old=(const void *)original;
  size_t first=(original_bytes+7)&~(size_t)7;
  size_t registers=(size_t)old->registers.count+7,inputs=(size_t)old->inputs.count+6;
  size_t constant_at=first+product(registers,sizeof(CSOsdiPrecisionRegister));
  size_t constants=(size_t)old->constants.count+1;
  size_t input_at=constant_at+product(constants,sizeof(CSOsdiPrecisionConstant));
  size_t root_at=input_at+product(inputs,sizeof(CSOsdiPrecisionInput));
  uint32_t root_count=(quotient==1||quotient==2)?8:6;
  size_t total=root_at+root_count*sizeof(CSOsdiPrecisionRoot);
  unsigned char *data=allocate(total,1);memcpy(data,original,original_bytes);
  CSOsdiPrecisionProgramHeader *h=(void *)data;h->program_bytes=total;
  if(quotient==3){CSOsdiPrecisionNode *nodes=(void *)(data+h->nodes.offset);nodes[1].is_flow=1;}
  h->registers=(CSOsdiPrecisionSection){first,(uint32_t)registers,sizeof(CSOsdiPrecisionRegister)};
  h->constants=(CSOsdiPrecisionSection){constant_at,(uint32_t)constants,sizeof(CSOsdiPrecisionConstant)};
  h->inputs=(CSOsdiPrecisionSection){input_at,(uint32_t)inputs,sizeof(CSOsdiPrecisionInput)};
  h->roots=(CSOsdiPrecisionSection){root_at,root_count,sizeof(CSOsdiPrecisionRoot)};
  CSOsdiPrecisionRegister *r=(void *)(data+first);
  memcpy(r,original+old->registers.offset,product(old->registers.count,sizeof(*r)));
  uint32_t truth=old->registers.count+6;r[truth]=(CSOsdiPrecisionRegister){CS_OSDI_PRECISION_BOOL,0};
  CSOsdiPrecisionConstant *c=(void *)(data+constant_at);
  memcpy(c,original+old->constants.offset,product(old->constants.count,sizeof(*c)));
  c[old->constants.count]=(CSOsdiPrecisionConstant){truth,CS_OSDI_PRECISION_BOOL,1};
  CSOsdiPrecisionInput *q=(void *)(data+input_at);
  memcpy(q,original+old->inputs.offset,product(old->inputs.count,sizeof(*q)));
  const uint32_t endpoints[6][2]={{0,CS_OSDI_PRECISION_NONE},{0,CS_OSDI_PRECISION_NONE},{CS_OSDI_PRECISION_NONE,0},{0,1},{1,0},{2,2}};
  CSOsdiPrecisionRoot *roots=(void *)(data+root_at);
  for(uint32_t i=0;i<6;++i){uint32_t reg=old->registers.count+i;
    r[reg]=(CSOsdiPrecisionRegister){1,0};
    q[old->inputs.count+i]=(CSOsdiPrecisionInput){2,reg,CS_OSDI_PRECISION_RAW_DIFFERENCE,endpoints[i][0],endpoints[i][1],0};
    roots[i]=(CSOsdiPrecisionRoot){2,i<3?CS_OSDI_PRECISION_ROOT_F:CS_OSDI_PRECISION_ROOT_Q,reg,i%3,CS_OSDI_PRECISION_NONE,0};
  }
  /* The original source must reproduce the setup quotient. Merely modifying
     setup.map without its preparation-only collapse roots is invalid input. */
  if(root_count==8){
    roots[6]=(CSOsdiPrecisionRoot){1,CS_OSDI_PRECISION_ROOT_COLLAPSE,truth,0,quotient==1?1:CS_OSDI_PRECISION_NONE,0};
    roots[7]=(CSOsdiPrecisionRoot){1,CS_OSDI_PRECISION_ROOT_COLLAPSE,truth,2,CS_OSDI_PRECISION_NONE,0};
  }
  assert(snprintf(path,sizeof(path),"%s/%s.program",dir,name)>0);
  FILE *out=fopen(path,"wbx");assert(out&&fwrite(data,1,total,out)==total&&!fclose(out));free(data);free(original);
  assert(snprintf(path,sizeof(path),"%s/conservation.setup",dir)>0);
  size_t setup_bytes;unsigned char *setup=read_file(path,&setup_bytes);
  CSOsdiPrecisionSetupHeader *state=(void *)setup;
  uint32_t *map=(void *)(setup+state->raw_to_active.offset);
  if(quotient==1){state->active_unknown_count=1;map[0]=0;map[1]=0;map[2]=CS_OSDI_PRECISION_NONE;}
  if(quotient==2){state->active_unknown_count=1;map[0]=CS_OSDI_PRECISION_NONE;map[1]=0;map[2]=CS_OSDI_PRECISION_NONE;}
  assert(snprintf(path,sizeof(path),"%s/%s.setup",dir,name)>0);
  out=fopen(path,"wbx");assert(out&&fwrite(setup,1,setup_bytes,out)==setup_bytes&&!fclose(out));free(setup);
}
static void exact_probe_identities(const char *dir){
  const char *names[]={"probe-distinct","probe-alias","probe-ground"};
  for(unsigned variant=0;variant<3;++variant){
    write_probe_fixture(dir,names[variant],variant);model m;three_row_model(&m,dir,names[variant]);
    cs_precision_constitutive_limits l=identity_limits(&m,4096);cs_precision_constitutive_workspace w;
    assert(!cs_precision_constitutive_open(&m.p,&l,&w));cs_precision_constitutive_identities proof={0};
    assert(!identity_with_box(&m,&w,&proof,0));assert(!identity_with_box(&m,&w,&proof,1));
    uint32_t f=0,q=0;for(uint32_t i=0;i<proof.claim_count;++i){f+=proof.claims[i].kind==1;q+=proof.claims[i].kind==2;}
    assert(f==(variant==2?3u:2u)&&q==(variant==1?3u:2u));
    selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_F,1,-1,0);assert(!identity_with_box(&m,&w,&proof,1));
    selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_F,1,0,1);assert(!identity_with_box(&m,&w,&proof,1));
    selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,1,0);assert(!identity_with_box(&m,&w,&proof,1));
    selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,0,0,1);assert(!identity_with_box(&m,&w,&proof,1));
    selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,0,0);
    assert(identity_with_box(&m,&w,&proof,1)==(variant==1?0:CS_IDENTITY_NONZERO));
    /* Equal numeric coordinates must not alias distinct source coordinates. */
    if(!variant){
      size_t active=(size_t)m.p.active_count;double zero[3]={0,0,0};
      assert(active==3&&!cs_precision_decision_cover_construct(&m.p,&m.control,zero,zero,zero,zero,&m.limits,&m.w,&m.counters,&m.result));
      assert(!identity_with_box(&m,&w,&proof,0));
      selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,0,0);
      assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_NONZERO);
    }
    /* Every independent replay rebuilds the coordinate cache, including after
       a deliberately false claim. No prior source/claim state grants success. */
    assert(!identity_with_box(&m,&w,&proof,0));assert(!identity_with_box(&m,&w,&proof,1));
    cs_precision_constitutive_close(&w);close_model(&m);
  }
}
static void explicit_coordinate_conditions(const char *dir){
  model m;three_row_model(&m,dir,"probe-distinct");
  cs_precision_constitutive_limits l=identity_limits(&m,4096);
  const uint32_t conditions[2][3]={{0,0,2},{CS_OSDI_PRECISION_NONE,1,2}};
  for(unsigned variant=0;variant<2;++variant){
    cs_precision_constitutive_workspace w;cs_precision_constitutive_identities proof={0};
    assert(!cs_precision_constitutive_open_conditioned(&m.p,&l,conditions[variant],3,&w));
    assert(!identity_with_box(&m,&w,&proof,0)&&!identity_with_box(&m,&w,&proof,1));
    uint32_t f=0,q=0;for(uint32_t i=0;i<proof.claim_count;++i){f+=proof.claims[i].kind==1;q+=proof.claims[i].kind==2;}
    assert(f==(variant?3u:2u)&&q==(variant?2u:3u));
    const uint32_t *binding=proof.coordinate_quotient;proof.coordinate_quotient=conditions[variant];
    assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_INVALID);proof.coordinate_quotient=binding;
    assert(!identity_with_box(&m,&w,&proof,1));
    cs_precision_constitutive_close(&w);
  }
  /* Full class intersection is required: every member overlaps the broad first
     interval, while the two narrow members do not overlap each other. */
  const uint32_t all[3]={0,0,0};cs_precision_constitutive_workspace w;
  assert(!cs_precision_constitutive_open_conditioned(&m.p,&l,all,3,&w));
  double lh[3]={0,1,8},ll[3]={0},uh[3]={10,2,9},ul[3]={0};
  assert(cs_precision_constitutive_domain(&w,lh,ll,uh,ul)==CS_IDENTITY_EMPTY_CONDITION);
  for(unsigned i=0;i<3;++i)lh[i]=uh[i]=1;
  ll[1]=ul[1]=0x1p-54;
  assert(cs_precision_constitutive_domain(&w,lh,ll,uh,ul)==CS_IDENTITY_EMPTY_CONDITION);
  ll[1]=ul[1]=0;assert(!cs_precision_constitutive_domain(&w,lh,ll,uh,ul));
  cs_precision_constitutive_close(&w);
  const uint32_t chain[3]={0,0,1};
  assert(cs_precision_constitutive_open_conditioned(&m.p,&l,chain,3,&w)==CS_IDENTITY_INVALID);
  close_model(&m);
  write_probe_fixture(dir,"probe-flow",3);three_row_model(&m,dir,"probe-flow");l=identity_limits(&m,4096);
  assert(cs_precision_constitutive_open_conditioned(&m.p,&l,conditions[0],3,&w)==CS_IDENTITY_INVALID);
  const uint32_t grounded_flow[3]={0,CS_OSDI_PRECISION_NONE,2};
  assert(cs_precision_constitutive_open_conditioned(&m.p,&l,grounded_flow,3,&w)==CS_IDENTITY_INVALID);
  assert(cs_precision_constitutive_open_conditioned(&m.p,&l,conditions[0],2,&w)==CS_IDENTITY_INVALID);
  assert(!cs_precision_constitutive_open(&m.p,&l,&w));cs_precision_constitutive_close(&w);close_model(&m);
}
static void setup_only_alias_is_not_source_authority(const char *dir){
  char pp[4096],sp[4096];assert(snprintf(pp,sizeof(pp),"%s/probe-distinct.program",dir)>0);
  assert(snprintf(sp,sizeof(sp),"%s/probe-distinct.setup",dir)>0);
  size_t bytes;unsigned char *setup=read_file(sp,&bytes);CSOsdiPrecisionSetupHeader *state=(void *)setup;
  uint32_t *map=(void *)(setup+state->raw_to_active.offset);
  state->active_unknown_count=1;map[0]=map[1]=0;map[2]=CS_OSDI_PRECISION_NONE;
  assert(snprintf(sp,sizeof(sp),"%s/probe-forged-setup.setup",dir)>0);
  FILE *out=fopen(sp,"wbx");assert(out&&fwrite(setup,1,bytes,out)==bytes&&!fclose(out));free(setup);
  /* The unchanged program has no COLLAPSE roots. Static ABI admission cannot
     grant this setup-only alias; the original source replay must reject it. */
  model m;open_model(&m,pp,sp,256,64,UINT64_C(1000000000),1000000,192);
  double lower=-1,upper=1,zero=0;cs_precision_decision_cover before=m.result;
  int status=cs_precision_decision_cover_construct(&m.p,&m.control,&lower,&zero,&upper,&zero,
    &m.limits,&m.w,&m.counters,&m.result);
  assert(status==CS_OSDI_PRECISION_INVALID_INPUT&&!memcmp(&before,&m.result,sizeof(before)));
  close_model(&m);
}
static void synthetic_identities(const char *dir){
  exact_probe_identities(dir);
  setup_only_alias_is_not_source_authority(dir);
  explicit_coordinate_conditions(dir);
  model m;three_row_model(&m,dir,"conservation");
  cs_precision_constitutive_workspace w;cs_precision_constitutive_limits l=identity_limits(&m,4096);
  assert(!cs_precision_constitutive_open(&m.p,&l,&w));cs_precision_constitutive_identities proof={0};
  /* Capacity metadata is preadmitted, but no rational denominator is allocated
     merely because a source-node ceiling is large. */
  for(uint32_t i=0;i<=w.limits.maximum_nodes;++i)assert(!w.constant_live[i]&&!w.weight_live[i]);
  for(size_t i=0;i<w.matrix_cells;++i)assert(!w.matrix_live[i]);
  assert(!identity_with_box(&m,&w,&proof,0));assert(proof.raw_row_count==3&&proof.claim_count>=3&&proof.terminal_count==1);
  assert(!identity_with_box(&m,&w,&proof,1));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,1,0);assert(!identity_with_box(&m,&w,&proof,1));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_F,1,1,1);assert(!identity_with_box(&m,&w,&proof,1));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,0,0,1);assert(!identity_with_box(&m,&w,&proof,1));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,0,0);assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_NONZERO);
  CSOsdiPrecisionSetupHeader other_setup=*m.p.setup;
  other_setup.temperature_binary64=UINT64_C(0x4073600000000000);
  proof.setup_bytes=&other_setup;assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_INVALID);
  proof.setup_bytes=m.p.setup;
  proof.raw_row_count=2;assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_INVALID);proof.raw_row_count=3;
  proof.terminal_count=0;assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_INVALID);proof.terminal_count=1;
  mpq_t *saved=w.claims[0].coefficients;w.claims[0].coefficients=w.matrix;
  assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_INVALID);w.claims[0].coefficients=saved;
  uint32_t cap=w.limits.maximum_nodes;w.limits.maximum_nodes=1;
  cs_precision_constitutive_identities sentinel=proof;
  assert(identity_with_box(&m,&w,&proof,0)==CS_IDENTITY_CAPACITY);
  assert(!memcmp(&sentinel,&proof,sizeof(proof)));w.limits.maximum_nodes=cap;
  assert(!identity_with_box(&m,&w,&proof,0));
  m.p.math.work=m.p.math.limit;
  assert(identity_with_box(&m,&w,&proof,1)==CS_OSDI_PRECISION_WORK_EXHAUSTED);assert(m.p.math.work==m.p.math.limit);
  cs_precision_constitutive_close(&w);cs_precision_constitutive_close(&w);close_model(&m);

  three_row_model(&m,dir,"conservation");l=identity_limits(&m,4096);l.maximum_nodes=10000;
  assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));uint32_t live_nodes=0;
  for(uint32_t i=0;i<=w.limits.maximum_nodes;++i)live_nodes+=w.constant_live[i]||w.weight_live[i];
  assert(live_nodes>0&&live_nodes<=w.node_count+1&&live_nodes<100);
  assert(!identity_with_box(&m,&w,&proof,0));
  cs_precision_constitutive_close(&w);cs_precision_constitutive_close(&w);close_model(&m);

  three_row_model(&m,dir,"branch");l=identity_limits(&m,4096);assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));assert(proof.terminal_count>1);
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,1,0);assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_NONZERO);
  uint32_t children=m.w.nodes[0].child_count;m.w.nodes[0].child_count--;
  assert(identity_with_box(&m,&w,&proof,1)==CS_PRECISION_COVER_INVALID);m.w.nodes[0].child_count=children;
  cs_precision_constitutive_close(&w);close_model(&m);

  three_row_model(&m,dir,"growth");l=identity_limits(&m,128);assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  memset(&proof,0x4a,sizeof(proof));sentinel=proof;
  assert(identity_with_box(&m,&w,&proof,0)==CS_IDENTITY_COEFFICIENT_LIMIT);
  assert(!memcmp(&sentinel,&proof,sizeof(proof)));cs_precision_constitutive_close(&w);
  l=identity_limits(&m,4096);assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,1,0);assert(!identity_with_box(&m,&w,&proof,1));
  cs_precision_constitutive_close(&w);close_model(&m);

  three_row_model(&m,dir,"independent_atoms");l=identity_limits(&m,4096);assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));
  selected_claim(&m,&w,&proof,CS_OSDI_PRECISION_ROOT_Q,1,1,0);assert(identity_with_box(&m,&w,&proof,1)==CS_IDENTITY_NONZERO);
  cs_precision_constitutive_close(&w);close_model(&m);

  three_row_model(&m,dir,"common_kernel");l=identity_limits(&m,4096);
  assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));assert(proof.claim_count==5);
  assert(!identity_with_box(&m,&w,&proof,1));
  uint32_t qclaims=0;
  for(uint32_t i=0;i<proof.claim_count;++i)if(proof.claims[i].kind==CS_OSDI_PRECISION_ROOT_Q){
    mpq_t *v=proof.claims[i].coefficients;mpq_t sum;mpq_init(sum);qclaims++;
    assert(mpq_sgn(v[0]) && !mpq_cmp(v[0],v[2]) && !mpq_cmp(v[1],v[3]));
    mpq_add(sum,v[0],v[1]);assert(!mpq_sgn(sum));mpq_clear(sum);
  }
  assert(qclaims==1);cs_precision_constitutive_close(&w);close_model(&m);

  fixture(&m,dir,"loop");assert(!construct(&m,0,0));l=identity_limits(&m,4096);
  assert(!cs_precision_constitutive_open(&m.p,&l,&w));
  assert(!identity_with_box(&m,&w,&proof,0));assert(!identity_with_box(&m,&w,&proof,1));
  assert(proof.raw_row_count==1 && proof.claim_count==1 && proof.claims[0].kind==CS_OSDI_PRECISION_ROOT_Q);
  cs_precision_constitutive_close(&w);close_model(&m);
}
static void identity_stage(const char *stage,const char *event,const model *m,int status){
  struct timespec now;assert(!clock_gettime(CLOCK_MONOTONIC,&now));
  fprintf(stderr,"identity-stage stage=%s event=%s seconds=%lld.%09ld status=%d work=%" PRIu64
    " steps=%" PRIu64 " guards=%" PRIu64 " copied=%" PRIu64 " nodes=%u leaves=%u\n",
    stage,event,(long long)now.tv_sec,now.tv_nsec,status,m->p.math.work,m->counters.steps,
    m->counters.guard_records,m->counters.copied_bytes,m->result.node_count,m->result.leaf_count);
  fflush(stderr);
}
static void actual_identity(char **argv,const identity_capture *capture){
  model m;open_model(&m,argv[2],argv[3],20000,64,number(argv[7]),number(argv[8]),capture->original.maximum_precision_bits);
  size_t bytes;unsigned char *request=read_file(argv[4],&bytes);size_t n=(size_t)m.p.active_count;
  assert(bytes==capture->request_bytes && bytes==sizeof(capture->original)+product(4*n,sizeof(double)));
  assert(!memcmp(request,&capture->original,sizeof(capture->original)));
  assert(!((uintptr_t)(request+sizeof(capture->original))%_Alignof(double)));
  const double *box=(const void *)(request+sizeof(capture->original));
  /* Preserve every original source/control byte. Experimental resource-owner
     ceilings are explicit separate overrides, not a rewritten source request. */
  m.control=capture->original;
  m.control.maximum_work_units=number(argv[7]);m.control.arena_bytes=number(argv[5]);
  identity_stage("cover","enter",&m,0);
  int status=cs_precision_decision_cover_construct(&m.p,&m.control,box,box+n,box+2*n,box+3*n,&m.limits,&m.w,&m.counters,&m.result);
  identity_stage("cover","leave",&m,status);
  cs_precision_constitutive_workspace w={0};cs_precision_constitutive_identities proof={0};
  cs_precision_constitutive_limits l=identity_limits(&m,number(argv[6]));
  l.maximum_atoms=(uint32_t)number(argv[10]);l.maximum_nodes=(uint32_t)number(argv[11]);
  l.maximum_workspace_bytes=number(argv[12]);
  if(!status){identity_stage("open","enter",&m,0);status=cs_precision_constitutive_open(&m.p,&l,&w);identity_stage("open","leave",&m,status);}
  if(!status){identity_stage("construct","enter",&m,0);status=identity_with_box(&m,&w,&proof,0);identity_stage("construct","leave",&m,status);}
  if(!status){identity_stage("validate","enter",&m,0);status=identity_with_box(&m,&w,&proof,1);identity_stage("validate","leave",&m,status);}
  printf("{\"status\":%d,\"claimCount\":%u,\"rawRows\":%u,\"terminalCount\":%u,\"work\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"identities\":[",
    status,status?0:proof.claim_count,m.p.header->raw_node_count,status?0:proof.terminal_count,m.p.math.work,m.counters.steps);
  if(!status)for(uint32_t i=0;i<proof.claim_count;++i){
    printf("%s{\"kind\":%u,\"rawRowCoefficients\":[",i?",":"",proof.claims[i].kind);
    for(uint32_t row=0;row<proof.raw_row_count;++row){if(row)putchar(',');gmp_printf("\"%Qd\"",proof.claims[i].coefficients[row]);}
    printf("]}");
  }
  printf("]}\n");
  cs_precision_constitutive_close(&w);free(request);close_model(&m);
}
int main(int argc,char **argv){
  assert(argc==2||argc==3||argc==14);identity_capture capture={0};
  if(argc==14){
    uint64_t nodes=number(argv[11]),atoms=number(argv[10]);assert(nodes<UINT32_MAX&&atoms<UINT32_MAX);
    int status=capture_preflight(argv[2],argv[3],argv[4],number(argv[5]),number(argv[13]),number(argv[12]),
        (uint32_t)nodes,(uint32_t)atoms,&capture);
    if(status){printf("{\"status\":%d,\"claimCount\":0,\"preAllocationRefusal\":true}\n",status);return 0;}
    assert(number(argv[9])==capture.original.requested_precision_bits);
  }else large_sparse_preflight(argc==2?argv[1]:argv[2]);
  cs_precision_arena arena;
  assert(cs_precision_arena_open(&arena,argc==14?number(argv[5]):UINT64_C(268435456)));
  if(argc==3){
    assert(!strcmp(argv[1],"--cancel"));model m;three_row_model(&m,argv[2],"conservation");
    cs_precision_constitutive_workspace w;cs_precision_constitutive_limits l=identity_limits(&m,4096);
    assert(!cs_precision_constitutive_open(&m.p,&l,&w));cs_precision_constitutive_identities proof={0};
    puts("source-identity-attempt-ready");fflush(stdout);
    for(;;){int status=identity_with_box(&m,&w,&proof,0);if(status){fprintf(stderr,"work stopped: %d\n",status);return 3;}}
  }
  if(argc==2){synthetic_identities(argv[1]);puts("source linear identities: conservation, exact scalars, missing roots, branch/setup/claim forgery, growth, work and rollback passed");}
  else actual_identity(argv,&capture);
  mpfr_free_cache();cs_precision_arena_close(&arena);return 0;
}
