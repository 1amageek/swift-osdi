#include "CoverInternal.h"

static int equal_bytes(cs_precision_program *p,const void *a,const void *b,size_t bytes){
  if(!cs_precision_charge(&p->math,bytes))return p->math.status;
  return bytes && memcmp(a,b,bytes)?CS_PRECISION_COVER_INVALID:0;
}
static int event_valid(cs_precision_program *p,const cs_precision_cover_limits *l,
    cs_precision_cover_counters *c,const cs_precision_source_event *event,uint32_t depth){
  const cs_precision_source_decision *d=&event->decision;
  if(d->site.stage<1 || d->site.stage>3 || d->site.ordinal!=depth ||
      !cs_precision_cell_valid(&d->cell,l->maximum_integer_bits) ||
      !cs_precision_cell_valid(&event->first_cell,l->maximum_integer_bits) ||
      !cs_precision_cell_valid(&event->last_cell,l->maximum_integer_bits))return CS_PRECISION_COVER_INVALID;
  uint64_t count=cs_cover_outcomes(event);
  if(!count)return CS_PRECISION_COVER_INVALID;
  if(d->site.kind==CS_PRECISION_SOURCE_CELL){
    if(event->allowed_mask || count>l->maximum_cells || d->site.choice ||
        cs_precision_cell_compare(&event->first_cell,&event->last_cell)>0)return CS_PRECISION_COVER_INVALID;
    cs_precision_source_decision last;
    int status=cs_cover_outcome(p,l,c,event,count-1,&last);if(status)return status;
    if(cs_precision_cell_compare(&last.cell,&event->last_cell))return CS_PRECISION_COVER_INVALID;
  }else{
    uint32_t allowed=(d->site.kind==CS_PRECISION_SOURCE_RELATION || d->site.kind==CS_PRECISION_SOURCE_SIGN)?14:3;
    if(d->site.kind<1 || d->site.kind>CS_PRECISION_SOURCE_SIGN || event->allowed_mask&~allowed ||
        event->cell_count || event->first_cell.count || event->last_cell.count || d->cell.count)
      return CS_PRECISION_COVER_INVALID;
  }
  cs_precision_source_decision first;
  int status=cs_cover_outcome(p,l,c,event,0,&first);if(status)return status;
  if(count==1){if(!cs_cover_decision_equal(d,&first))return CS_PRECISION_COVER_INVALID;}
  else if(d->site.choice || d->cell.count)return CS_PRECISION_COVER_INVALID;
  /* The duplicated guard location is public evidence too; it must not carry a
   * different selection from the tree event template. */
  cs_precision_source_decision guard={0};guard.site=event->operands.decision;guard.opcode=d->opcode;guard.cell=d->cell;
  if(!cs_cover_decision_equal(d,&guard))return CS_PRECISION_COVER_INVALID;
  return 0;
}
int cs_precision_decision_cover_validate(cs_precision_program *p,
    const cs_osdi_precision_control_v1 *control,const double *lh,const double *ll,
    const double *uh,const double *ul,const cs_precision_cover_limits *l,
    cs_precision_cover_workspace *w,cs_precision_cover_counters *c,const cs_precision_decision_cover *cover){
  size_t stride;int status=cs_cover_admit(p,control,l,w,c,cover,&stride);if(status)return status;
  if(!cover || (p->active_count && (!lh || !ll || !uh || !ul)) || cover->program!=p ||
      cover->program_bytes!=p->header || cover->setup_bytes!=p->setup ||
      !cs_cover_control_equal(&cover->control,control) || cover->nodes!=w->nodes ||
      cover->leaf_planes!=w->leaf_planes || cover->domain!=w->domain || cover->plane_stride!=stride ||
      !cover->node_count || cover->node_count>l->maximum_nodes || cover->leaf_count>l->maximum_leaves)
    return CS_PRECISION_COVER_INVALID;
  const double *inputs[]={lh,ll,uh,ul};
  if(!cs_cover_inputs_disjoint(p,w,inputs,cover))return CS_OSDI_PRECISION_INVALID_INPUT;
  size_t m=(size_t)p->active_count;
  for(unsigned j=0;j<4;++j){status=equal_bytes(p,cover->domain+j*m,inputs[j],m*sizeof(double));if(status)return status;}
  status=cs_cover_copy(p,l,c,w->node_visits,NULL,(size_t)cover->node_count*sizeof(*w->node_visits));if(status)return status;
  status=cs_cover_copy(p,l,c,w->leaf_visits,NULL,(size_t)cover->leaf_count*sizeof(*w->leaf_visits));if(status)return status;
  if(cover->nodes[0].parent!=CS_COVER_NONE || cover->nodes[0].depth)return CS_PRECISION_COVER_INVALID;
  for(uint32_t i=0;i<cover->node_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    const cs_precision_cover_node *node=&cover->nodes[i];
    if(node->depth>l->maximum_depth || (i && (node->parent>=i || node->depth!=cover->nodes[node->parent].depth+1)))
      return CS_PRECISION_COVER_INVALID;
    if(node->kind==CS_PRECISION_COVER_DECISION){
      status=event_valid(p,l,c,&node->event,node->depth);if(status)return status;
      uint64_t count=cs_cover_outcomes(&node->event);
      if(count!=node->child_count || node->leaf!=CS_COVER_NONE || node->first_child<=i ||
          node->first_child>=cover->node_count || node->child_count>cover->node_count-node->first_child)
        return CS_PRECISION_COVER_INVALID;
      for(uint32_t j=0;j<node->child_count;++j){uint32_t index=node->first_child+j;
        const cs_precision_cover_node *child=&cover->nodes[index];cs_precision_source_decision expected;
        status=cs_cover_outcome(p,l,c,&node->event,j,&expected);if(status)return status;
        if(child->parent!=i || child->depth!=node->depth+1 || w->node_visits[index]++ ||
            !cs_cover_decision_equal(&child->incoming,&expected))return CS_PRECISION_COVER_INVALID;
      }
    }else if(node->kind==CS_PRECISION_COVER_TERMINAL || node->kind==CS_PRECISION_COVER_EXCLUSION){
      if(node->child_count || node->first_child!=CS_COVER_NONE)return CS_PRECISION_COVER_INVALID;
      if(node->kind==CS_PRECISION_COVER_TERMINAL){
        if(node->leaf>=cover->leaf_count || w->leaf_visits[node->leaf]++)return CS_PRECISION_COVER_INVALID;
      }else if(node->leaf!=CS_COVER_NONE)return CS_PRECISION_COVER_INVALID;
    }else return CS_PRECISION_COVER_INVALID;
  }
  for(uint32_t i=1;i<cover->node_count;++i)if(w->node_visits[i]!=1)return CS_PRECISION_COVER_INVALID;
  for(uint32_t i=0;i<cover->leaf_count;++i)if(w->leaf_visits[i]!=1)return CS_PRECISION_COVER_INVALID;
  status=cs_cover_copy(p,l,c,w->node_visits,NULL,(size_t)cover->node_count*sizeof(*w->node_visits));if(status)return status;
  for(uint32_t i=0;i<cover->node_count;++i){
    const cs_precision_cover_node *leaf=&cover->nodes[i];
    if(leaf->kind!=CS_PRECISION_COVER_TERMINAL && leaf->kind!=CS_PRECISION_COVER_EXCLUSION)continue;
    status=cs_cover_prefix(p,l,w,c,cover,i);if(status)return status;
    cs_precision_frontier_result replay={0};status=cs_cover_replay(p,l,w,c,cover,leaf->depth,&replay);if(status)return status;
    if(replay.event_count!=leaf->depth ||
        replay.state!=(leaf->kind==CS_PRECISION_COVER_TERMINAL?CS_PRECISION_TERMINAL:CS_PRECISION_EXCLUDED))
      return CS_PRECISION_COVER_INVALID;
    uint32_t current=i;w->node_visits[current]=1;
    while(current){const cs_precision_cover_node *child=&cover->nodes[current];
      const cs_precision_cover_node *parent=&cover->nodes[child->parent];
      if(!cs_cover_event_equal(&parent->event,&replay.events[child->depth-1]))return CS_PRECISION_COVER_INVALID;
      w->node_visits[child->parent]=1;current=child->parent;
    }
    if(leaf->kind==CS_PRECISION_COVER_TERMINAL){
      size_t n=p->header->raw_node_count,matrix=n*n,offset=(size_t)leaf->leaf*stride;
      for(unsigned j=0;j<8;++j){size_t count=j<4?n:matrix;
        status=equal_bytes(p,cover->leaf_planes+offset,replay.planes[j],count*sizeof(double));if(status)return status;offset+=count;
      }
    }
  }
  for(uint32_t i=0;i<cover->node_count;++i)if(w->node_visits[i]!=1)return CS_PRECISION_COVER_INVALID;
  return 0;
}
