#include "Transport.h"
#include "../../Arena/Arena.h"
#include <string.h>
static int bytes(size_t count,size_t stride,size_t *out){if(stride && count>SIZE_MAX/stride)return 0;*out=count*stride;return 1;}
static void *pool(size_t count,size_t stride){size_t size;if(!bytes(count?count:1,stride,&size))return NULL;return cs_precision_arena_allocate(size);}
static cs_osdi_chart_decision_v3 site(const cs_precision_chart_decision *s){
  return (cs_osdi_chart_decision_v3){s->stage,s->block,s->instruction,s->kind,s->choice,0,s->ordinal};
}
static cs_osdi_cover_integer_v4 integer(const cs_precision_cell_integer *v){
  cs_osdi_cover_integer_v4 out={0};out.negative=v->negative;out.count=v->count;
  for(unsigned j=0;j<CS_OSDI_COVER_CELL_LIMBS;++j)out.limbs[j]=v->limbs[j];return out;
}
static cs_osdi_cover_decision_v4 decision(const cs_precision_source_decision *v){
  cs_osdi_cover_decision_v4 out={0};out.site=site(&v->site);out.opcode=v->opcode;out.cell=integer(&v->cell);return out;
}
static cs_osdi_chart_guard_v3 guard(const cs_precision_chart_guard *g){
  return (cs_osdi_chart_guard_v3){site(&g->decision),g->opcode,g->a,g->b,g->a_type,g->b_type,
    g->a_integer,g->b_integer,g->formula_truth_known,g->boundary_extension,0,g->a_lower,g->a_upper,g->b_lower,g->b_upper};
}
int cs_precision_cover_transport_open(cs_precision_cover_transport *t,const cs_osdi_cover_control_v4 *h,double *planes[8]){
  memset(t,0,sizeof(*t));const cs_osdi_cover_limits_v4 *v=&h->limits;
  t->limits=(cs_precision_cover_limits){v->maximum_steps,v->maximum_guard_records,v->maximum_copied_bytes,v->maximum_cells,
    v->maximum_nodes,v->maximum_leaves,v->maximum_pending,v->maximum_depth,v->maximum_integer_bits};
  cs_precision_cover_workspace *w=&t->workspace;
  w->node_capacity=v->maximum_nodes;w->pending_capacity=v->maximum_pending;w->prefix_capacity=v->maximum_depth;
  w->replay.event_capacity=v->maximum_depth;w->leaf_plane_capacity=(size_t)h->leaf_region.element_count;
  w->domain_capacity=(size_t)h->base.active_count*4;if(!w->domain_capacity)w->domain_capacity=1;
  w->node_visit_capacity=v->maximum_nodes;w->leaf_visit_capacity=v->maximum_leaves;
  w->nodes=pool(w->node_capacity,sizeof(*w->nodes));w->pending=pool(w->pending_capacity,sizeof(*w->pending));
  w->prefix=pool(w->prefix_capacity,sizeof(*w->prefix));w->replay.events=pool(w->replay.event_capacity,sizeof(*w->replay.events));
  w->leaf_planes=pool(w->leaf_plane_capacity,sizeof(double));w->domain=pool(w->domain_capacity,sizeof(double));
  w->node_visits=pool(w->node_visit_capacity,sizeof(*w->node_visits));w->leaf_visits=pool(w->leaf_visit_capacity,sizeof(*w->leaf_visits));
  for(unsigned j=0;j<8;++j){w->replay.formulas.planes[j]=planes[j];w->replay.formulas.plane_capacities[j]=(size_t)h->base.regions[j+6].element_count;}
  return w->nodes && w->pending && w->prefix && w->replay.events && w->leaf_planes && w->domain && w->node_visits && w->leaf_visits?0:CS_PRECISION_COVER_CAPACITY;
}
int cs_precision_cover_transport_evaluate(cs_precision_cover_transport *t,cs_precision_program *p,
    cs_osdi_cover_control_v4 *wire,const cs_osdi_cover_control_v4 *fixed,unsigned char *mapping){
  wire->node_count=wire->leaf_count=0;
  uint64_t input_bytes=4*fixed->base.active_count*sizeof(double);
  if(input_bytes>t->limits.maximum_copied_bytes-t->counters.copied_bytes)return CS_PRECISION_COVER_CAPACITY;
  if(!cs_precision_charge(&p->math,input_bytes))return p->math.status;
  t->counters.copied_bytes+=input_bytes;
  cs_osdi_precision_control_v1 local=wire->base;local.protocol_version=CS_OSDI_RANGE_PROTOCOL_VERSION;
  cs_precision_decision_cover cover={0};
  int status=cs_precision_decision_cover_construct(p,&local,
    (const void *)(mapping+fixed->base.regions[2].byte_offset),(const void *)(mapping+fixed->base.regions[3].byte_offset),
    (const void *)(mapping+fixed->base.regions[4].byte_offset),(const void *)(mapping+fixed->base.regions[5].byte_offset),
    &t->limits,&t->workspace,&t->counters,&cover);
  if(!status){
    uint64_t tree_bytes=(uint64_t)cover.node_count*sizeof(cs_osdi_cover_node_v4);
    uint64_t leaf_bytes=(uint64_t)cover.leaf_count*cover.plane_stride*sizeof(double);
    uint64_t parent_check_bytes=((uint64_t)cover.node_count+cover.leaf_count)*sizeof(uint32_t);
    if(tree_bytes>t->limits.maximum_copied_bytes-t->counters.copied_bytes ||
        leaf_bytes>t->limits.maximum_copied_bytes-t->counters.copied_bytes-tree_bytes ||
        parent_check_bytes>t->limits.maximum_copied_bytes-t->counters.copied_bytes-tree_bytes-leaf_bytes)status=CS_PRECISION_COVER_CAPACITY;
    else if(!cs_precision_charge(&p->math,tree_bytes+leaf_bytes+parent_check_bytes))status=p->math.status;
    else{
      t->counters.copied_bytes+=tree_bytes+leaf_bytes+parent_check_bytes;
      cs_osdi_cover_node_v4 *nodes=(void *)(mapping+fixed->tree_region.byte_offset);
      for(uint32_t i=0;i<cover.node_count;++i){const cs_precision_cover_node *n=&cover.nodes[i];
        cs_osdi_cover_node_v4 out={0};out.parent=n->parent;out.depth=n->depth;out.kind=n->kind;
        out.first_child=n->first_child;out.child_count=n->child_count;out.leaf=n->leaf;
        out.incoming=decision(&n->incoming);out.event.decision=decision(&n->event.decision);out.event.operands=guard(&n->event.operands);
        out.event.allowed_mask=n->event.allowed_mask;out.event.cell_count=n->event.cell_count;
        out.event.first_cell=integer(&n->event.first_cell);out.event.last_cell=integer(&n->event.last_cell);nodes[i]=out;
      }
      memcpy(mapping+fixed->leaf_region.byte_offset,cover.leaf_planes,(size_t)leaf_bytes);
      wire->node_count=cover.node_count;wire->leaf_count=cover.leaf_count;
    }
  }
  wire->completed_steps=t->counters.steps;wire->completed_guard_records=t->counters.guard_records;
  wire->completed_copied_bytes=t->counters.copied_bytes;
  /* A last replay's model bound is not an aggregate over every source leaf. */
  p->has_bound=0;return status;
}
