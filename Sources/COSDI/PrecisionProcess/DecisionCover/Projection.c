#include "Projection.h"
#include <math.h>
#include <string.h>
#include <time.h>
#define NONE UINT32_MAX
static int poll_bound(uint64_t until,cs_osdi_precision_cancel_probe probe,void *context){
  if(probe && probe(context))return CS_OSDI_PRECISION_PROCESS_CANCELLED;
  struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return CS_OSDI_PRECISION_PROCESS_IO;
  uint64_t ns=(uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
  return ns>=until?CS_OSDI_PRECISION_PROCESS_TIMEOUT:0;
}
static int integer_valid(const cs_osdi_cover_integer_v4 *v,uint32_t bits){
  if(v->negative>1 || v->count>CS_OSDI_COVER_CELL_LIMBS || (!v->count && v->negative) ||
      (v->count && !v->limbs[v->count-1]))return 0;
  for(uint32_t j=v->count;j<CS_OSDI_COVER_CELL_LIMBS;++j)if(v->limbs[j])return 0;
  return !v->count || 64u*(v->count-1)+64u-(uint32_t)__builtin_clzll(v->limbs[v->count-1])<=bits;
}
static int plus(cs_osdi_cover_integer_v4 *v,uint64_t add){
  if(v->negative && v->count<=1 && v->limbs[0]<=add){uint64_t n=add-v->limbs[0];memset(v,0,sizeof(*v));if(n){v->count=1;v->limbs[0]=n;}}
  else if(v->negative){uint64_t old=v->limbs[0];v->limbs[0]-=add;uint64_t borrow=old<add;
    for(uint32_t j=1;borrow && j<v->count;++j){old=v->limbs[j];v->limbs[j]--;borrow=old==0;}
    if(borrow)return 0;while(v->count && !v->limbs[v->count-1])v->count--;if(!v->count)v->negative=0;
  }else if(add){if(!v->count){v->count=1;v->limbs[0]=add;}
    else{uint64_t old=v->limbs[0];v->limbs[0]+=add;uint64_t carry=v->limbs[0]<old;
      for(uint32_t j=1;carry && j<v->count;++j){v->limbs[j]++;carry=v->limbs[j]==0;}
      if(carry){if(v->count==CS_OSDI_COVER_CELL_LIMBS)return 0;v->limbs[v->count++]=1;}}}
  return 1;
}
static int section(CSOsdiPrecisionSection v,uint64_t bytes,size_t stride){return v.stride==stride && v.offset<=bytes && v.count<=(bytes-v.offset)/stride;}
static int operand(const unsigned char *program,const CSOsdiPrecisionProgramHeader *h,uint32_t kind,uint32_t reg,uint32_t payload,double lo,double hi){
  if(kind>6 || (kind?reg>=h->registers.count:reg!=NONE))return 0;
  if(kind<=4)return isfinite(lo) && isfinite(hi) && lo<=hi && (kind!=3 || payload<=1);
  if(payload>=h->constants.count)return 0;
  CSOsdiPrecisionConstant c;memcpy(&c,program+h->constants.offset+(size_t)payload*sizeof(c),sizeof(c));
  uint64_t bits=kind==5?UINT64_C(0xfff0000000000000):UINT64_C(0x7ff0000000000000);double value;memcpy(&value,&bits,sizeof(value));
  return c.type==1 && c.bits==bits && lo==value && hi==value;
}
static int source_event(const unsigned char *program,const CSOsdiPrecisionProgramHeader *h,const cs_osdi_cover_event_v4 *e,uint32_t depth,uint32_t bits,uint64_t max_cells){
  const cs_osdi_cover_decision_v4 *d=&e->decision;const cs_osdi_chart_guard_v3 *g=&e->operands;
  if(e->reserved || d->reserved || d->site.reserved || g->reserved || g->decision.reserved ||
      d->site.stage<1 || d->site.stage>3 || d->site.block>=h->blocks.count || d->site.ordinal!=depth ||
      d->site.kind<1 || d->site.kind>6 || g->formula_truth_known || g->boundary_extension ||
      memcmp(&d->site,&g->decision,sizeof(d->site)) || g->opcode!=d->opcode ||
      !integer_valid(&d->cell,bits) || !integer_valid(&e->first_cell,bits) || !integer_valid(&e->last_cell,bits) ||
      !operand(program,h,g->a_type,g->a,g->a_integer,g->a_lower,g->a_upper) || !g->a_type ||
      !operand(program,h,g->b_type,g->b,g->b_integer,g->b_lower,g->b_upper))return 0;
  CSOsdiPrecisionBlock b;memcpy(&b,program+h->blocks.offset+(size_t)d->site.block*sizeof(b),sizeof(b));
  if(b.function_index!=d->site.stage-1)return 0;
  if(d->site.kind==2){if(d->site.instruction!=NONE || b.terminator!=2 || d->opcode!=2 || g->a!=b.condition || g->b!=NONE)return 0;}
  else{
    if(d->site.instruction>=h->instructions.count || d->site.instruction<b.first_instruction ||
        d->site.instruction-b.first_instruction>=b.instruction_count)return 0;
    CSOsdiPrecisionInstruction op;memcpy(&op,program+h->instructions.offset+(size_t)d->site.instruction*sizeof(op),sizeof(op));
    if(op.opcode!=d->opcode || op.a!=g->a || (d->site.kind!=3 && op.b!=g->b) || (d->site.kind==3 && g->b!=NONE))return 0;
    if(d->site.kind==3 && op.opcode!=74)return 0;
    if(d->site.kind==4 && !(op.opcode>=53 && op.opcode<=56) && op.opcode!=58 && op.opcode!=62)return 0;
    if(d->site.kind==5 && op.opcode!=10)return 0;
    if(d->site.kind==6 && op.opcode!=5 && op.opcode!=6 && op.opcode!=20 && op.opcode!=21)return 0;
    if(d->site.kind==1 && !(op.opcode>=49 && op.opcode<=64))return 0;
  }
  uint64_t count;
  if(d->site.kind==6){
    if(e->allowed_mask || !e->cell_count || e->cell_count>max_cells || d->site.choice)return 0;
    cs_osdi_cover_integer_v4 last=e->first_cell;if(!plus(&last,e->cell_count-1) || memcmp(&last,&e->last_cell,sizeof(last)))return 0;
    count=e->cell_count;
    if(count==1){if(memcmp(&d->cell,&e->first_cell,sizeof(d->cell)))return 0;}
    else if(d->cell.count)return 0;
  }else{
    uint32_t allowed=d->site.kind>=4?14:3;
    if(!e->allowed_mask || e->allowed_mask&~allowed || e->cell_count || d->cell.count || e->first_cell.count || e->last_cell.count)return 0;
    count=(uint64_t)__builtin_popcount(e->allowed_mask);
    if(count==1){if(d->site.choice!=(uint32_t)__builtin_ctz(e->allowed_mask))return 0;}
    else if(d->site.choice)return 0;
  }
  return count>0;
}
static int incoming(const cs_osdi_cover_event_v4 *e,uint32_t index,const cs_osdi_cover_decision_v4 *actual){
  cs_osdi_cover_decision_v4 expected=e->decision;
  if(expected.site.kind==6){expected.cell=e->first_cell;if(!plus(&expected.cell,index))return 0;}
  else{for(uint32_t choice=0;choice<=3;++choice)if(e->allowed_mask&(1u<<choice)){if(!index){expected.site.choice=choice;break;}index--;}}
  return !memcmp(&expected,actual,sizeof(expected));
}
int cs_osdi_cover_wire_check(const cs_osdi_cover_control_v4 *wire,const unsigned char *program,uint64_t program_bytes,
    const cs_osdi_cover_node_v4 *nodes,const double *planes,uint32_t *node_visits,uint32_t *leaf_visits,uint64_t previous_guards,uint64_t previous_copies,uint64_t until,
    cs_osdi_precision_cancel_probe probe,void *context){
  const cs_osdi_cover_limits_v4 *l=&wire->limits;
  if(!wire->node_count || wire->node_count>l->maximum_nodes || wire->leaf_count>l->maximum_leaves ||
      program_bytes<sizeof(CSOsdiPrecisionProgramHeader))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  uint64_t n=wire->base.raw_node_count,matrix=n*n,stride=4*n+4*matrix;
  if(wire->completed_copied_bytes<previous_copies || wire->completed_guard_records<previous_guards)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  uint64_t remaining=wire->completed_copied_bytes-previous_copies;
  const uint64_t minimum[]={4*wire->base.active_count*sizeof(double),(uint64_t)wire->node_count*sizeof(*nodes),
    (uint64_t)wire->leaf_count*stride*sizeof(double),((uint64_t)wire->node_count+wire->leaf_count)*sizeof(uint32_t)};
  for(unsigned i=0;i<4;++i){if(minimum[i]>remaining)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;remaining-=minimum[i];}
  uint64_t decisions=0;
  CSOsdiPrecisionProgramHeader h;memcpy(&h,program,sizeof(h));
  if(!section(h.constants,program_bytes,sizeof(CSOsdiPrecisionConstant)) || !section(h.blocks,program_bytes,sizeof(CSOsdiPrecisionBlock)) ||
      !section(h.instructions,program_bytes,sizeof(CSOsdiPrecisionInstruction)))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  memset(node_visits,0,(size_t)wire->node_count*sizeof(*node_visits));memset(leaf_visits,0,(size_t)wire->leaf_count*sizeof(*leaf_visits));
  if(nodes[0].parent!=NONE || nodes[0].depth)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  for(uint32_t i=0;i<wire->node_count;++i){
    if(!(i%32)){int status=poll_bound(until,probe,context);if(status)return status;}
    const cs_osdi_cover_node_v4 *n=&nodes[i];
    if(n->depth>l->maximum_depth || (i && (n->parent>=i || n->depth!=nodes[n->parent].depth+1)))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    if(n->kind==CS_OSDI_COVER_DECISION){
      decisions++;
      if(!source_event(program,&h,&n->event,n->depth,l->maximum_integer_bits,l->maximum_cells))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      uint64_t count=n->event.decision.site.kind==6?n->event.cell_count:(uint64_t)__builtin_popcount(n->event.allowed_mask);
      if(count!=n->child_count || n->leaf!=NONE || n->first_child<=i || n->first_child>=wire->node_count ||
          n->child_count>wire->node_count-n->first_child)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      for(uint32_t j=0;j<n->child_count;++j){uint32_t child=n->first_child+j;
        if(!(j%32)){int status=poll_bound(until,probe,context);if(status)return status;}
        if(node_visits[child]++ || nodes[child].parent!=i || nodes[child].depth!=n->depth+1 || !incoming(&n->event,j,&nodes[child].incoming))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      }
    }else if(n->kind==CS_OSDI_COVER_TERMINAL){
      if(n->first_child!=NONE || n->child_count)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      if(n->leaf>=wire->leaf_count || leaf_visits[n->leaf]++)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    }else return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  }
  for(uint32_t i=1;i<wire->node_count;++i)if(node_visits[i]!=1)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  if(wire->completed_guard_records-previous_guards<2*decisions)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  for(uint32_t leaf=0;leaf<wire->leaf_count;++leaf){
    if(leaf_visits[leaf]!=1)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;uint64_t offset=(uint64_t)leaf*stride;
    for(unsigned plane=0;plane<8;plane+=2){uint64_t count=plane<4?n:matrix;
      for(uint64_t j=0;j<count;++j){if(!(j%256)){int status=poll_bound(until,probe,context);if(status)return status;}
        double lo=planes[offset+j],hi=planes[offset+count+j];if(!isfinite(lo) || !isfinite(hi) || lo>hi)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      }offset+=2*count;
    }
  }
  return poll_bound(until,probe,context);
}
