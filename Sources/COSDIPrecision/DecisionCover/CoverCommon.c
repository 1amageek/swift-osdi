#include "CoverInternal.h"
#include <math.h>
static int product(size_t a,size_t b,size_t *out){if(b && a>SIZE_MAX/b)return 0;*out=a*b;return 1;}
typedef struct { uintptr_t begin,end; } cover_span;
static int span(cover_span *s,const void *pointer,size_t count,size_t size,size_t alignment){
  size_t bytes;if(!pointer || !product(count,size,&bytes) || (uintptr_t)pointer%alignment || bytes>UINTPTR_MAX-(uintptr_t)pointer)return 0;
  s->begin=(uintptr_t)pointer;s->end=s->begin+bytes;return 1;
}
static int overlaps(cover_span a,cover_span b){return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
static int geometry(cs_precision_program *p,const cs_osdi_precision_control_v1 *control,
    const cs_precision_cover_limits *l,cs_precision_cover_workspace *w,const cs_precision_cover_counters *c,const cs_precision_decision_cover *metadata){
  cover_span buffers[16],immutable[8];
#define BUFFER(index,member,capacity,Type) if(!span(&buffers[index],w->member,w->capacity,sizeof(Type),_Alignof(Type)))return 0
  BUFFER(0,nodes,node_capacity,cs_precision_cover_node);BUFFER(1,pending,pending_capacity,uint32_t);BUFFER(2,prefix,prefix_capacity,cs_precision_source_decision);
  if(!span(&buffers[3],w->replay.events,w->replay.event_capacity,sizeof(cs_precision_source_event),_Alignof(cs_precision_source_event)))return 0;
  BUFFER(4,leaf_planes,leaf_plane_capacity,double);BUFFER(5,domain,domain_capacity,double);BUFFER(6,node_visits,node_visit_capacity,uint32_t);BUFFER(7,leaf_visits,leaf_visit_capacity,uint32_t);
#undef BUFFER
  for(unsigned i=0;i<8;++i)if(!span(&buffers[i+8],w->replay.formulas.planes[i],w->replay.formulas.plane_capacities[i],sizeof(double),_Alignof(double)))return 0;
  if(!span(&immutable[0],p->header,(size_t)p->header->program_bytes,1,1) ||
      !span(&immutable[1],p->setup,(size_t)p->setup->setup_bytes,1,1) ||
      !span(&immutable[2],p,1,sizeof(*p),_Alignof(cs_precision_program)) ||
      !span(&immutable[3],control,1,sizeof(*control),_Alignof(cs_osdi_precision_control_v1)) ||
      !span(&immutable[4],l,1,sizeof(*l),_Alignof(cs_precision_cover_limits)) ||
      !span(&immutable[5],w,1,sizeof(*w),_Alignof(cs_precision_cover_workspace)) ||
      !span(&immutable[6],c,1,sizeof(*c),_Alignof(cs_precision_cover_counters)) ||
      !span(&immutable[7],metadata,1,sizeof(*metadata),_Alignof(cs_precision_decision_cover)))return 0;
  for(unsigned i=0;i<7;++i)if(overlaps(immutable[i],immutable[7]))return 0;
  for(unsigned i=0;i<16;++i){
    for(unsigned j=i+1;j<16;++j)if(overlaps(buffers[i],buffers[j]))return 0;
    for(unsigned j=0;j<8;++j)if(overlaps(buffers[i],immutable[j]))return 0;
  }
  return 1;
}
int cs_cover_inputs_disjoint(cs_precision_program *p,const cs_precision_cover_workspace *w,
    const double *const inputs[4],const cs_precision_decision_cover *metadata){
  /* Input planes may share each other's backing, but no mutable cover pool. */
  cover_span buffers[16],input,result;
  if(!span(&result,metadata,1,sizeof(*metadata),_Alignof(cs_precision_decision_cover)))return 0;
  const void *pointers[]={w->nodes,w->pending,w->prefix,w->replay.events,w->leaf_planes,w->domain,w->node_visits,w->leaf_visits};
  const size_t counts[]={w->node_capacity,w->pending_capacity,w->prefix_capacity,w->replay.event_capacity,w->leaf_plane_capacity,w->domain_capacity,w->node_visit_capacity,w->leaf_visit_capacity};
  const size_t sizes[]={sizeof(*w->nodes),sizeof(*w->pending),sizeof(*w->prefix),sizeof(*w->replay.events),sizeof(double),sizeof(double),sizeof(uint32_t),sizeof(uint32_t)};
  for(unsigned i=0;i<8;++i)if(!span(&buffers[i],pointers[i],counts[i],sizes[i],1))return 0;
  for(unsigned i=0;i<8;++i)if(!span(&buffers[i+8],w->replay.formulas.planes[i],w->replay.formulas.plane_capacities[i],sizeof(double),1))return 0;
  for(unsigned i=0;i<4;++i){
    if(!p->active_count)continue;
    if(!span(&input,inputs[i],(size_t)p->active_count,sizeof(double),_Alignof(double)))return 0;
    if(overlaps(input,result))return 0;
    for(unsigned j=0;j<16;++j)if(overlaps(input,buffers[j]))return 0;
  }
  return 1;
}
int cs_cover_admit(cs_precision_program *p,const cs_osdi_precision_control_v1 *control,
    const cs_precision_cover_limits *l,cs_precision_cover_workspace *w,const cs_precision_cover_counters *c,const cs_precision_decision_cover *metadata,size_t *stride){
  if(!p || !p->header || !p->setup || !control || !l || !w || !c || !stride || !p->range_mode || p->chart || p->frontier ||
      control->protocol_version!=CS_OSDI_RANGE_PROTOCOL_VERSION ||
      control->raw_node_count!=p->header->raw_node_count || control->active_count!=p->active_count ||
      control->descriptor_index!=p->header->descriptor_index || control->maximum_precision_bits!=p->maximum_precision ||
      control->maximum_work_units!=p->math.limit || !l->maximum_steps || !l->maximum_guard_records ||
      !l->maximum_copied_bytes || !l->maximum_cells || l->maximum_cells>ULONG_MAX ||
      !l->maximum_nodes || l->maximum_nodes==UINT32_MAX || !l->maximum_leaves || !l->maximum_pending ||
      !l->maximum_depth || !l->maximum_integer_bits || l->maximum_integer_bits>CS_PRECISION_CELL_BITS ||
      c->steps>l->maximum_steps || c->guard_records>l->maximum_guard_records || c->copied_bytes>l->maximum_copied_bytes)
    return CS_OSDI_PRECISION_INVALID_INPUT;
  size_t n=p->header->raw_node_count,matrix,vector,all;
  if(!product(n,n,&matrix) || !product(n,4,&vector) || !product(matrix,4,&all) || vector>SIZE_MAX-all)return CS_PRECISION_COVER_CAPACITY;
  *stride=vector+all;
  size_t domain,planes;
  if(!product((size_t)p->active_count,4,&domain) || !product(*stride,l->maximum_leaves,&planes))return CS_PRECISION_COVER_CAPACITY;
  if(!w->nodes || w->node_capacity<l->maximum_nodes || !w->pending || w->pending_capacity<l->maximum_pending ||
      !w->prefix || w->prefix_capacity<l->maximum_depth || !w->replay.events || w->replay.event_capacity<l->maximum_depth ||
      !w->leaf_planes || w->leaf_plane_capacity<planes || !w->domain || w->domain_capacity<(domain?domain:1) ||
      !w->node_visits || w->node_visit_capacity<l->maximum_nodes || !w->leaf_visits || w->leaf_visit_capacity<l->maximum_leaves)
    return CS_PRECISION_COVER_CAPACITY;
  for(unsigned i=0;i<8;++i)if(!w->replay.formulas.planes[i] || w->replay.formulas.plane_capacities[i]<(i<4?n:matrix))return CS_PRECISION_COVER_CAPACITY;
  const size_t counts[]={w->node_capacity,w->pending_capacity,w->prefix_capacity,w->replay.event_capacity,w->leaf_plane_capacity,
    w->domain_capacity,w->node_visit_capacity,w->leaf_visit_capacity};
  const size_t sizes[]={sizeof(*w->nodes),sizeof(*w->pending),sizeof(*w->prefix),sizeof(*w->replay.events),sizeof(double),sizeof(double),sizeof(uint32_t),sizeof(uint32_t)};
  for(unsigned i=0;i<8;++i)if(!product(counts[i],sizes[i],&all))return CS_PRECISION_COVER_CAPACITY;
  return geometry(p,control,l,w,c,metadata)?0:CS_OSDI_PRECISION_INVALID_INPUT;
}
int cs_cover_copy(cs_precision_program *p,const cs_precision_cover_limits *l,cs_precision_cover_counters *c,void *to,const void *from,size_t bytes){
  if(bytes>l->maximum_copied_bytes-c->copied_bytes)return CS_PRECISION_COVER_CAPACITY;
  if(!cs_precision_charge(&p->math,bytes))return p->math.status;
  c->copied_bytes+=bytes;if(bytes){if(from)memcpy(to,from,bytes);else memset(to,0,bytes);}return 0;
}
int cs_cover_control_equal(const cs_osdi_precision_control_v1 *a,const cs_osdi_precision_control_v1 *b){
  return a->protocol_version==b->protocol_version && a->descriptor_index==b->descriptor_index &&
    a->active_count==b->active_count && a->raw_node_count==b->raw_node_count &&
    a->minimum_precision_bits==b->minimum_precision_bits && a->maximum_precision_bits==b->maximum_precision_bits &&
    a->requested_precision_bits==b->requested_precision_bits && a->maximum_work_units==b->maximum_work_units &&
    !memcmp(a->program_sha256,b->program_sha256,sizeof(a->program_sha256)) &&
    !memcmp(a->worker_profile_sha256,b->worker_profile_sha256,sizeof(a->worker_profile_sha256)) &&
    !memcmp(&a->candidate,&b->candidate,sizeof(a->candidate)) && !memcmp(&a->reference,&b->reference,sizeof(a->reference));
}
int cs_cover_decision_equal(const cs_precision_source_decision *a,const cs_precision_source_decision *b){
  return a->site.stage==b->site.stage && a->site.block==b->site.block && a->site.instruction==b->site.instruction &&
    a->site.kind==b->site.kind && a->site.choice==b->site.choice && a->site.ordinal==b->site.ordinal &&
    a->opcode==b->opcode && !memcmp(&a->cell,&b->cell,sizeof(a->cell));
}
int cs_cover_event_equal(const cs_precision_source_event *a,const cs_precision_source_event *b){
  const cs_precision_chart_guard *x=&a->operands,*y=&b->operands;
  int decision = a->decision.site.stage==b->decision.site.stage && a->decision.site.block==b->decision.site.block &&
    a->decision.site.instruction==b->decision.site.instruction && a->decision.site.kind==b->decision.site.kind &&
    a->decision.site.ordinal==b->decision.site.ordinal && a->decision.opcode==b->decision.opcode;
  if(cs_cover_outcomes(a)>1) decision = decision && !a->decision.site.choice && !a->decision.cell.count && !a->decision.cell.negative;
  else decision = decision && cs_cover_decision_equal(&a->decision,&b->decision);
  return decision && a->allowed_mask==b->allowed_mask && a->cell_count==b->cell_count &&
    !memcmp(&a->first_cell,&b->first_cell,sizeof(a->first_cell)) && !memcmp(&a->last_cell,&b->last_cell,sizeof(a->last_cell)) &&
    x->opcode==y->opcode && x->a==y->a && x->b==y->b && x->a_type==y->a_type && x->b_type==y->b_type &&
    x->a_integer==y->a_integer && x->b_integer==y->b_integer && x->formula_truth_known==y->formula_truth_known &&
    x->boundary_extension==y->boundary_extension && !memcmp(&x->a_lower,&y->a_lower,sizeof(double)) &&
    !memcmp(&x->a_upper,&y->a_upper,sizeof(double)) && !memcmp(&x->b_lower,&y->b_lower,sizeof(double)) && !memcmp(&x->b_upper,&y->b_upper,sizeof(double));
}
uint64_t cs_cover_outcomes(const cs_precision_source_event *event){
  return event->decision.site.kind==CS_PRECISION_SOURCE_CELL?event->cell_count:(uint64_t)__builtin_popcount(event->allowed_mask);
}
static int add_index(cs_precision_cell_integer *v,uint64_t add){
  if(v->negative && v->count<=1 && v->limbs[0]<=add){
    uint64_t value=add-v->limbs[0];memset(v,0,sizeof(*v));if(value){v->count=1;v->limbs[0]=value;}
  }else if(v->negative){
    uint64_t prior=v->limbs[0];v->limbs[0]-=add;uint64_t borrow=prior<add;
    for(uint32_t i=1;borrow && i<v->count;++i){prior=v->limbs[i];v->limbs[i]--;borrow=prior==0;}
    if(borrow)return 0;while(v->count && !v->limbs[v->count-1])v->count--;if(!v->count)v->negative=0;
  }else if(add){
    if(!v->count){v->count=1;v->limbs[0]=add;}
    else{uint64_t prior=v->limbs[0];v->limbs[0]+=add;uint64_t carry=v->limbs[0]<prior;
      uint32_t i=1;for(;carry && i<v->count;++i){v->limbs[i]++;carry=v->limbs[i]==0;}
      if(carry){if(v->count==CS_PRECISION_CELL_LIMBS)return 0;v->limbs[v->count++]=1;}}
  }
  return cs_precision_cell_valid(v,CS_PRECISION_CELL_BITS);
}
int cs_cover_outcome(cs_precision_program *p,const cs_precision_cover_limits *l,cs_precision_cover_counters *c,
    const cs_precision_source_event *event,uint64_t index,cs_precision_source_decision *out){
  if(index>=cs_cover_outcomes(event))return CS_PRECISION_COVER_INVALID;
  int status=cs_cover_copy(p,l,c,out,&event->decision,sizeof(*out));if(status)return status;
  if(event->decision.site.kind==CS_PRECISION_SOURCE_CELL){
    status=cs_cover_copy(p,l,c,&out->cell,&event->first_cell,sizeof(out->cell));if(status)return status;
    out->site.choice=0;if(!add_index(&out->cell,index))return CS_PRECISION_COVER_INVALID;
  }else{out->site.choice=0;for(uint32_t choice=0;choice<=3;++choice)if(event->allowed_mask&(1u<<choice)){
      if(!index){out->site.choice=choice;return 0;}index--;}}
  return 0;
}
int cs_cover_prefix(cs_precision_program *p,const cs_precision_cover_limits *l,cs_precision_cover_workspace *w,cs_precision_cover_counters *c,
    const cs_precision_decision_cover *cover,uint32_t index){
  if(index>=cover->node_count)return CS_PRECISION_COVER_INVALID;
  uint32_t depth=cover->nodes[index].depth;
  if(depth>l->maximum_depth || depth>w->prefix_capacity)return CS_PRECISION_COVER_CAPACITY;
  uint32_t at=depth,current=index;
  while(current){const cs_precision_cover_node *node=&cover->nodes[current];
    if(!at || node->parent>=current || node->depth!=at)return CS_PRECISION_COVER_INVALID;
    int status=cs_cover_copy(p,l,c,&w->prefix[--at],&node->incoming,sizeof(node->incoming));if(status)return status;current=node->parent;
  }
  return at?CS_PRECISION_COVER_INVALID:0;
}
int cs_cover_replay(cs_precision_program *p,const cs_precision_cover_limits *l,cs_precision_cover_workspace *w,cs_precision_cover_counters *c,
    const cs_precision_decision_cover *cover,size_t prefix_count,cs_precision_frontier_result *out){
  if(c->steps>=l->maximum_steps)return CS_PRECISION_CHART_STEP_LIMIT;
  cs_precision_frontier_scratch scratch=w->replay;
  uint64_t remaining_guards=l->maximum_guard_records-c->guard_records;
  uint64_t remaining_bytes=(l->maximum_copied_bytes-c->copied_bytes)/sizeof(*scratch.events);
  if(scratch.event_capacity>remaining_guards)scratch.event_capacity=(size_t)remaining_guards;
  if(scratch.event_capacity>remaining_bytes)scratch.event_capacity=(size_t)remaining_bytes;
  if(prefix_count>scratch.event_capacity)return CS_PRECISION_COVER_CAPACITY;
  cs_precision_frontier_request request={w->prefix,prefix_count,l->maximum_steps-c->steps,l->maximum_cells,l->maximum_integer_bits};
  cs_osdi_precision_control_v1 control=cover->control;size_t n=(size_t)p->active_count;
  cs_precision_frontier_usage usage={0};scratch.usage=&usage;
  int status=cs_precision_program_explore_frontier(p,&control,cover->domain,cover->domain+n,cover->domain+2*n,cover->domain+3*n,&request,&scratch,out);
  c->steps+=usage.steps;c->guard_records+=usage.events;c->copied_bytes+=usage.events*sizeof(*scratch.events);
  return status;
}
