#include "CoverInternal.h"

static int children(cs_precision_program *p,const cs_precision_cover_limits *l,
    cs_precision_cover_workspace *w,cs_precision_cover_counters *c,
    cs_precision_decision_cover *cover,uint32_t parent,const cs_precision_source_event *event,
    uint32_t *first){
  uint64_t count=cs_cover_outcomes(event);
  if(!count || count>l->maximum_nodes-cover->node_count || cover->nodes[parent].depth>=l->maximum_depth)
    return CS_PRECISION_COVER_CAPACITY;
  *first=cover->node_count;
  for(uint32_t j=0;j<(uint32_t)count;++j){
    cs_precision_cover_node *node=&w->nodes[cover->node_count];
    int status=cs_cover_copy(p,l,c,node,NULL,sizeof(*node));if(status)return status;
    node->parent=parent;node->depth=w->nodes[parent].depth+1;
    node->first_child=node->leaf=CS_COVER_NONE;
    status=cs_cover_outcome(p,l,c,event,j,&node->incoming);if(status)return status;
    cover->node_count++;
  }
  cs_precision_cover_node *node=&w->nodes[parent];
  int status=cs_cover_copy(p,l,c,&node->event,event,sizeof(*event));if(status)return status;
  node->kind=CS_PRECISION_COVER_DECISION;node->first_child=*first;node->child_count=(uint32_t)count;
  return 0;
}
static int terminal(cs_precision_program *p,const cs_precision_cover_limits *l,
    cs_precision_cover_workspace *w,cs_precision_cover_counters *c,
    cs_precision_decision_cover *cover,uint32_t index,const cs_precision_frontier_result *replay){
  cs_precision_cover_node *node=&w->nodes[index];
  if(replay->state==CS_PRECISION_EXCLUDED){node->kind=CS_PRECISION_COVER_EXCLUSION;return 0;}
  if(cover->leaf_count>=l->maximum_leaves)return CS_PRECISION_COVER_CAPACITY;
  size_t n=p->header->raw_node_count,matrix=n*n,offset=(size_t)cover->leaf_count*cover->plane_stride;
  for(unsigned plane=0;plane<8;++plane){size_t count=plane<4?n:matrix;
    int status=cs_cover_copy(p,l,c,w->leaf_planes+offset,replay->planes[plane],count*sizeof(double));
    if(status)return status;offset+=count;
  }
  node->kind=CS_PRECISION_COVER_TERMINAL;node->leaf=cover->leaf_count++;return 0;
}
int cs_precision_decision_cover_construct(cs_precision_program *p,
    const cs_osdi_precision_control_v1 *control,const double *lh,const double *ll,
    const double *uh,const double *ul,const cs_precision_cover_limits *l,
    cs_precision_cover_workspace *w,cs_precision_cover_counters *c,cs_precision_decision_cover *result){
  size_t stride;int status=cs_cover_admit(p,control,l,w,c,result,&stride);
  if(status)return status;
  if(!result || (p->active_count && (!lh || !ll || !uh || !ul)))return CS_OSDI_PRECISION_INVALID_INPUT;
  const double *inputs[]={lh,ll,uh,ul};
  if(!cs_cover_inputs_disjoint(p,w,inputs,result))return CS_OSDI_PRECISION_INVALID_INPUT;
  cs_precision_decision_cover cover={0};cover.program=p;cover.program_bytes=p->header;cover.setup_bytes=p->setup;
  status=cs_cover_copy(p,l,c,&cover.control,control,sizeof(*control));if(status)return status;
  cover.nodes=w->nodes;cover.domain=w->domain;cover.leaf_planes=w->leaf_planes;cover.plane_stride=stride;
  size_t m=(size_t)p->active_count;
  for(unsigned j=0;j<4;++j){status=cs_cover_copy(p,l,c,w->domain+j*m,inputs[j],m*sizeof(double));if(status)return status;}
  status=cs_cover_copy(p,l,c,w->nodes,NULL,sizeof(*w->nodes));if(status)return status;
  w->nodes[0].parent=w->nodes[0].first_child=w->nodes[0].leaf=CS_COVER_NONE;
  cover.node_count=1;size_t pending=1;w->pending[0]=0;
  while(pending){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    uint32_t current=w->pending[--pending];
    status=cs_cover_prefix(p,l,w,c,&cover,current);if(status)return status;
    uint32_t depth=w->nodes[current].depth;
    cs_precision_frontier_result replay={0};
    status=cs_cover_replay(p,l,w,c,&cover,depth,&replay);if(status)return status;
    if(replay.event_count<depth)return CS_PRECISION_COVER_INVALID;
    for(size_t j=depth;j<replay.event_count;++j){
      const cs_precision_source_event *event=&replay.events[j];uint64_t count=cs_cover_outcomes(event);
      int frontier=replay.state==CS_PRECISION_FRONTIER && j+1==replay.event_count;
      if((frontier && count<2) || (!frontier && count!=1))return CS_PRECISION_COVER_INVALID;
      if(frontier && count>l->maximum_pending-pending)return CS_PRECISION_COVER_CAPACITY;
      uint32_t first;status=children(p,l,w,c,&cover,current,event,&first);if(status)return status;
      if(frontier){
        for(uint32_t k=(uint32_t)count;k;--k)w->pending[pending++]=first+k-1;
      }else current=first;
    }
    if(replay.state==CS_PRECISION_FRONTIER){if(replay.event_count==depth)return CS_PRECISION_COVER_INVALID;}
    else if(replay.state==CS_PRECISION_TERMINAL || replay.state==CS_PRECISION_EXCLUDED){
      status=terminal(p,l,w,c,&cover,current,&replay);if(status)return status;
    }else return CS_PRECISION_COVER_INVALID;
  }
  status=cs_precision_decision_cover_validate(p,control,lh,ll,uh,ul,l,w,c,&cover);
  if(status)return status;
  *result=cover;return 0;
}
