#include "IdentityInternal.h"
#include <math.h>

static int size_product(size_t a,size_t b,size_t *out){if(b&&a>SIZE_MAX/b)return 0;*out=a*b;return 1;}
static void *storage(cs_precision_program *p,size_t count,size_t stride,uint64_t cap,uint64_t *bytes){
  size_t size;if(!size_product(count?count:1,stride,&size)||size>cap-*bytes)return NULL;
  if(!cs_precision_charge(&p->math,(uint64_t)count+1))return NULL;
  void *result=cs_precision_arena_allocate(size);memset(result,0,size);*bytes+=size;return result;
}
int cs_precision_constitutive_open(cs_precision_program *p,const cs_precision_constitutive_limits *l,
    cs_precision_constitutive_workspace *w){
  return cs_precision_constitutive_open_conditioned(p,l,NULL,0,w);
}
int cs_precision_constitutive_open_conditioned(cs_precision_program *p,const cs_precision_constitutive_limits *l,
    const uint32_t *quotient,size_t quotient_count,cs_precision_constitutive_workspace *w){
  if(!p||!p->header||!p->setup||!l||!w||p->source_observer||p->chart||p->frontier||!p->range_mode||
      !l->maximum_nodes||l->maximum_nodes==UINT32_MAX||!l->maximum_atoms||l->maximum_atoms==UINT32_MAX||
      !l->maximum_claims||!l->maximum_coefficient_bits||!l->maximum_workspace_bytes)
    return CS_IDENTITY_INVALID;
  if((quotient && quotient_count!=p->active_count)||(!quotient && quotient_count))return CS_IDENTITY_INVALID;
  for(size_t i=0;quotient&&i<quotient_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    uint32_t r=quotient[i];if(r!=CS_ID_NONE&&(r>i||quotient[r]!=r))return CS_IDENTITY_INVALID;
  }
  size_t rows=p->header->raw_node_count,nodes=(size_t)l->maximum_nodes+1;
  size_t columns=(size_t)l->maximum_atoms+1,matrix,relations,claims;
  if(!size_product(rows,columns,&matrix)||!size_product(rows,rows,&relations)||
      !size_product(l->maximum_claims,rows,&claims))return CS_IDENTITY_CAPACITY;
  /* Preflight every fixed C pool before any arena mutation. GMP limb/scratch
     peaks are separately bounded by the already-owned arena, never OS heap. */
  const size_t counts[]={nodes,p->header->registers.count,p->stage_capacity,p->header->roots.count,
    l->maximum_atoms,nodes,nodes,nodes,matrix,relations,relations,claims,l->maximum_claims,
    nodes,nodes,matrix,relations,relations,claims,p->active_count,p->active_count,p->active_count,p->active_count};
  const size_t strides[]={sizeof(cs_precision_constitutive_node),sizeof(uint32_t),sizeof(uint32_t),sizeof(uint32_t),
    sizeof(uint32_t),sizeof(uint32_t),sizeof(mpq_t),sizeof(mpq_t),sizeof(mpq_t),sizeof(mpq_t),sizeof(mpq_t),sizeof(mpq_t),sizeof(cs_precision_constitutive_claim),1,1,1,1,1,1,sizeof(uint32_t),sizeof(uint32_t),sizeof(uint32_t),sizeof(uint32_t)};
  uint64_t payload=0;for(unsigned i=0;i<23;++i){size_t bytes;
    if(!size_product(counts[i]?counts[i]:1,strides[i],&bytes)||bytes>l->maximum_workspace_bytes-payload)return CS_IDENTITY_CAPACITY;
    payload+=bytes;
  }
  if(!cs_precision_charge(&p->math,payload))return p->math.status;
  memset(w,0,sizeof(*w));w->program=p;w->limits=*l;uint64_t allocated=0;
#define POOL(member,count,type) do{w->member=storage(p,count,sizeof(type),l->maximum_workspace_bytes,&allocated);if(!w->member)return p->math.status?p->math.status:CS_IDENTITY_CAPACITY;}while(0)
  POOL(nodes,nodes,cs_precision_constitutive_node);POOL(registers,p->header->registers.count,uint32_t);
  POOL(staged,p->stage_capacity,uint32_t);POOL(roots,p->header->roots.count,uint32_t);
  POOL(atoms,l->maximum_atoms,uint32_t);POOL(atom_index,nodes,uint32_t);
  POOL(constant_values,nodes,mpq_t);POOL(weights,nodes,mpq_t);
  POOL(matrix,matrix,mpq_t);POOL(relations,relations,mpq_t);POOL(composed,relations,mpq_t);POOL(claim_values,claims,mpq_t);
  POOL(claims,l->maximum_claims,cs_precision_constitutive_claim);
  POOL(coordinates,p->active_count,uint32_t);POOL(quotient,p->active_count,uint32_t);
  POOL(lower_index,p->active_count,uint32_t);POOL(upper_index,p->active_count,uint32_t);
  POOL(constant_live,nodes,uint8_t);POOL(weight_live,nodes,uint8_t);POOL(matrix_live,matrix,uint8_t);
  POOL(relation_live,relations,uint8_t);POOL(composed_live,relations,uint8_t);POOL(claim_live,claims,uint8_t);
#undef POOL
  w->matrix_cells=matrix;w->relation_cells=relations;w->claim_cells=claims;
  for(uint32_t i=0;i<l->maximum_claims;++i)w->claims[i].coefficients=w->claim_values+(size_t)i*rows;
  for(uint32_t i=0;i<p->active_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;w->quotient[i]=quotient?quotient[i]:i;
  }
  /* Reuse the still-empty coordinate cache for source ABI nature admission. */
  for(uint32_t raw=0;raw<p->header->raw_node_count;++raw){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    uint32_t active=p->map[raw];if(active==CS_ID_NONE)continue;
    uint32_t kind=p->nodes[raw].is_flow?2:1;
    if(w->coordinates[active]&&w->coordinates[active]!=kind)return CS_IDENTITY_INVALID;
    w->coordinates[active]=kind;
  }
  for(uint32_t i=0;i<p->active_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    uint32_t r=w->quotient[i];
    if((w->coordinates[i]==2&&r!=i)||(r!=CS_ID_NONE&&w->coordinates[r]!=w->coordinates[i]))return CS_IDENTITY_INVALID;
  }
  memset(w->coordinates,0,(size_t)p->active_count*sizeof(uint32_t));
  w->observer=(cs_precision_source_observer){w,cs_constitutive_observe};w->initialized=1;return 0;
}
void cs_precision_constitutive_close(cs_precision_constitutive_workspace *w){
  if(!w||!w->initialized)return;
  if(w->program->source_observer==&w->observer)w->program->source_observer=NULL;
  for(size_t i=0;i<=(size_t)w->limits.maximum_nodes;++i){
    if(w->constant_live[i])mpq_clear(w->constant_values[i]);
    if(w->weight_live[i])mpq_clear(w->weights[i]);
  }
  for(size_t i=0;i<w->matrix_cells;++i)if(w->matrix_live[i])mpq_clear(w->matrix[i]);
  for(size_t i=0;i<w->relation_cells;++i){
    if(w->relation_live[i])mpq_clear(w->relations[i]);
    if(w->composed_live[i])mpq_clear(w->composed[i]);
  }
  for(size_t i=0;i<w->claim_cells;++i)if(w->claim_live[i])mpq_clear(w->claim_values[i]);
  w->initialized=0;w->claim_count=0;
}

/* Unknown addresses are the algorithm's already-initialized lexical temporaries.
   Pooled objects acquire GMP ownership only at first actual write. */
int cs_constitutive_touch(cs_precision_constitutive_workspace *w,mpq_ptr value){
  mpq_t *pools[]={w->constant_values,w->weights,w->matrix,w->relations,w->composed,w->claim_values};
  uint8_t *live[]={w->constant_live,w->weight_live,w->matrix_live,w->relation_live,w->composed_live,w->claim_live};
  size_t counts[]={(size_t)w->limits.maximum_nodes+1,(size_t)w->limits.maximum_nodes+1,w->matrix_cells,
    w->relation_cells,w->relation_cells,w->claim_cells};
  uintptr_t at=(uintptr_t)value;
  for(unsigned pool=0;pool<6;++pool){
    uintptr_t base=(uintptr_t)pools[pool];
    if(at<base || (at-base)/sizeof(mpq_t)>=counts[pool] || (at-base)%sizeof(mpq_t))continue;
    size_t index=(at-base)/sizeof(mpq_t);if(live[pool][index])return 0;
    /* Prepay initialization plus exactly-once cleanup even after quota failure. */
    if(!cs_precision_charge(&w->program->math,2))return w->program->math.status;
    mpq_init(value);live[pool][index]=1;return 0;
  }
  return 0;
}

static int pair_le(double ah,double al,double bh,double bl){return ah<bh||(ah==bh&&al<=bl);}
int cs_precision_constitutive_domain(cs_precision_constitutive_workspace *w,const double *lh,const double *ll,
    const double *uh,const double *ul){
  size_t n=(size_t)w->program->active_count;
  if(n&&(!lh||!ll||!uh||!ul))return CS_IDENTITY_INVALID;
  for(uint32_t i=0;i<n;++i){
    if(!cs_precision_charge(&w->program->math,8))return w->program->math.status;
    if(!isfinite(lh[i])||!isfinite(ll[i])||!isfinite(uh[i])||!isfinite(ul[i])||
        lh[i]+ll[i]!=lh[i]||uh[i]+ul[i]!=uh[i]||!pair_le(lh[i],ll[i],uh[i],ul[i]))return CS_IDENTITY_INVALID;
    w->lower_index[i]=w->upper_index[i]=i;
    uint32_t r=w->quotient[i];if(r!=CS_ID_NONE&&(r>i||w->quotient[r]!=r))return CS_IDENTITY_INVALID;
    if(r==CS_ID_NONE){if(!pair_le(lh[i],ll[i],0,0)||!pair_le(0,0,uh[i],ul[i]))return CS_IDENTITY_EMPTY_CONDITION;}
    else{
      uint32_t lower=w->lower_index[r],upper=w->upper_index[r];
      if(!pair_le(lh[i],ll[i],lh[lower],ll[lower]))w->lower_index[r]=i;
      if(!pair_le(uh[upper],ul[upper],uh[i],ul[i]))w->upper_index[r]=i;
    }
  }
  for(uint32_t i=0;i<n;++i){
    if(!cs_precision_charge(&w->program->math,1))return w->program->math.status;
    if(w->quotient[i]==i){uint32_t lower=w->lower_index[i],upper=w->upper_index[i];
      if(!pair_le(lh[lower],ll[lower],uh[upper],ul[upper]))return CS_IDENTITY_EMPTY_CONDITION;}
  }
  return 0;
}
