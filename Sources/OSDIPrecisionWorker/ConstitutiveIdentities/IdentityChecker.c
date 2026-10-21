#include "IdentityInternal.h"

static int capture(cs_precision_program *p,const cs_precision_cover_limits *l,
    cs_precision_cover_workspace *cw,cs_precision_cover_counters *c,
    const cs_precision_decision_cover *cover,uint32_t terminal,cs_precision_constitutive_workspace *w){
  int status=cs_cover_prefix(p,l,cw,c,cover,terminal);if(status)return status;
  if(p->source_observer)return CS_IDENTITY_INVALID;
  cs_precision_frontier_result replay={0};p->source_observer=&w->observer;
  status=cs_cover_replay(p,l,cw,c,cover,cover->nodes[terminal].depth,&replay);
  p->source_observer=NULL;
  if(status)return status;
  if(replay.state!=CS_PRECISION_TERMINAL||replay.event_count!=cover->nodes[terminal].depth)
    return CS_IDENTITY_INVALID;
  return cs_constitutive_prepare(w);
}
/* This checker neither uses nor trusts the search matrix/elimination. It starts
   at every claimed original raw root in a newly replayed source expression DAG. */
static int check_claim(cs_precision_constitutive_workspace *w,const cs_precision_constitutive_claim *claim){
  if(claim->kind!=CS_OSDI_PRECISION_ROOT_F&&claim->kind!=CS_OSDI_PRECISION_ROOT_Q)return CS_IDENTITY_INVALID;
  if(!claim->coefficients)return CS_IDENTITY_INVALID;
  uint32_t n=w->program->header->raw_node_count;int nonzero=0;
  uintptr_t base=(uintptr_t)w->claim_values, at=(uintptr_t)claim->coefficients;
  if(!n || at<base || (at-base)%sizeof(mpq_t) ||
      (at-base)/sizeof(mpq_t)>w->claim_cells ||
      (size_t)n>w->claim_cells-(at-base)/sizeof(mpq_t) ||
      ((at-base)/sizeof(mpq_t))%n)return CS_IDENTITY_INVALID;
  for(uint32_t row=0;row<n;++row){
    if(!w->claim_live[(at-base)/sizeof(mpq_t)+row])return CS_IDENTITY_INVALID;
    int status=cs_constitutive_scalar_valid(w,claim->coefficients[row]);if(status)return status;
    nonzero|=mpq_sgn(claim->coefficients[row])!=0;
  }
  if(!nonzero)return CS_IDENTITY_INVALID;
  int status=cs_constitutive_expand(w,claim->kind,(const mpq_t *)claim->coefficients,w->matrix);if(status)return status;
  for(uint32_t atom=0;atom<=w->atom_count;++atom){
    if(!cs_precision_charge(&w->program->math,1))return w->program->math.status;
    if(mpq_sgn(w->matrix[atom]))return CS_IDENTITY_NONZERO;
  }
  return 0;
}
static int admit(cs_precision_program *p,const cs_osdi_precision_control_v1 *control,
    const double *lh,const double *ll,const double *uh,const double *ul,
    const cs_precision_cover_limits *l,cs_precision_cover_workspace *cw,
    cs_precision_cover_counters *c,const cs_precision_decision_cover *cover,
    cs_precision_constitutive_workspace *w){
  if(!w||!w->initialized||w->program!=p||!p||p->source_observer||!cover ||
      w->observer.context!=w || w->observer.observe!=cs_constitutive_observe)
    return CS_IDENTITY_INVALID;
  int status=cs_precision_constitutive_domain(w,lh,ll,uh,ul);if(status)return status;
  return cs_precision_decision_cover_validate(p,control,lh,ll,uh,ul,l,cw,c,cover);
}
int cs_precision_constitutive_validate(cs_precision_program *p,const cs_osdi_precision_control_v1 *control,
    const double *lh,const double *ll,const double *uh,const double *ul,
    const cs_precision_cover_limits *l,cs_precision_cover_workspace *cw,cs_precision_cover_counters *c,
    const cs_precision_decision_cover *cover,cs_precision_constitutive_workspace *w,
    const cs_precision_constitutive_identities *proof){
  int status=admit(p,control,lh,ll,uh,ul,l,cw,c,cover,w);if(status)return status;
  if(!proof||proof->program!=p||proof->program_bytes!=p->header||proof->setup_bytes!=p->setup||
      proof->cover!=cover||proof->raw_row_count!=p->header->raw_node_count||
      proof->terminal_count!=cover->leaf_count||proof->claim_count>w->limits.maximum_claims||
      proof->claims!=w->claims||proof->coordinate_quotient!=w->quotient)return CS_IDENTITY_INVALID;
  uint32_t count=0;
  for(uint32_t node=0;node<cover->node_count;++node){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    if(cover->nodes[node].kind!=CS_PRECISION_COVER_TERMINAL)continue;
    count++;status=capture(p,l,cw,c,cover,node,w);if(status)return status;
    for(uint32_t claim=0;claim<proof->claim_count;++claim){
      status=check_claim(w,&proof->claims[claim]);if(status)return status;
    }
  }
  return count==proof->terminal_count?0:CS_IDENTITY_INVALID;
}
/* Construct the common source-linear kernel and independently check every
   original terminal under the immutable coordinate condition. These facts
   establish no circuit rank, root, forward flow or whole-history authority. */
int cs_precision_constitutive_construct(cs_precision_program *p,const cs_osdi_precision_control_v1 *control,
    const double *lh,const double *ll,const double *uh,const double *ul,
    const cs_precision_cover_limits *l,cs_precision_cover_workspace *cw,cs_precision_cover_counters *c,
    const cs_precision_decision_cover *cover,cs_precision_constitutive_workspace *w,
    cs_precision_constitutive_identities *result){
  if(!result)return CS_IDENTITY_INVALID;
  int status=admit(p,control,lh,ll,uh,ul,l,cw,c,cover,w);if(status)return status;
  uint32_t n=p->header->raw_node_count;
  if(n>UINT32_MAX/2 || w->limits.maximum_claims<2*n)return CS_IDENTITY_CAPACITY;
  uint32_t counts[2]={n,n};
  for(uint32_t group=0;group<2;++group)for(uint32_t row=0;row<n;++row){
    size_t slot=(size_t)group*n+row;
    w->claims[slot].kind=group+1;
    w->claims[slot].coefficients=w->claim_values+slot*n;
    for(uint32_t col=0;col<n;++col){
      if(!cs_precision_charge(&p->math,1))return p->math.status;
      status=cs_constitutive_touch(w,w->claims[slot].coefficients[col]);if(status)return status;
      mpq_set_ui(w->claims[slot].coefficients[col],row==col,1);
    }
  }
  uint32_t terminals=0;
  for(uint32_t node=0;node<cover->node_count;++node){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    if(cover->nodes[node].kind!=CS_PRECISION_COVER_TERMINAL)continue;
    terminals++;status=capture(p,l,cw,c,cover,node,w);if(status)return status;
    for(uint32_t group=0;group<2;++group){
      uint32_t dimension;
      status=cs_constitutive_intersect(w,group+1,w->claim_values+(size_t)group*n*n,counts[group],&dimension);
      if(status)return status;counts[group]=dimension;
    }
  }
  if(!terminals||terminals!=cover->leaf_count)return CS_IDENTITY_INVALID;
  w->claim_count=0;
  for(uint32_t group=0;group<2;++group)for(uint32_t row=0;row<counts[group];++row){
    cs_precision_constitutive_claim claim={group+1,w->claim_values+((size_t)group*n+row)*n};
    w->claims[w->claim_count++]=claim;
  }
  /* An independent pass starts from the original SSA again. Neither its weighted
     root check nor terminal enumeration trusts the intersection/elimination. */
  for(uint32_t node=0;node<cover->node_count;++node){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    if(cover->nodes[node].kind!=CS_PRECISION_COVER_TERMINAL)continue;
    status=capture(p,l,cw,c,cover,node,w);if(status)return status;
    for(uint32_t claim=0;claim<w->claim_count;++claim){status=check_claim(w,&w->claims[claim]);if(status)return status;}
  }
  cs_precision_constitutive_identities complete={p,p->header,p->setup,cover,w->claims,
    p->header->raw_node_count,w->claim_count,terminals,w->quotient};
  *result=complete;return 0;
}
