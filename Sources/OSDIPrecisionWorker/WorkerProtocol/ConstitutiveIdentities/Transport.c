#include "Transport.h"
#include <string.h>
int cs_precision_identity_transport_open(cs_precision_program *p,const cs_osdi_identity_control_v5 *h,
    const uint32_t *quotient,cs_precision_constitutive_workspace *w){
  cs_precision_constitutive_limits l={h->limits.maximum_nodes,h->limits.maximum_atoms,
    (uint32_t)(2*h->cover.base.raw_node_count),h->limits.maximum_coefficient_bits,h->limits.maximum_workspace_bytes};
  return cs_precision_constitutive_open_conditioned(p,&l,quotient,(size_t)h->cover.base.active_count,w);
}
/* Project only independently checked, condition-bound source identities after
   constructing the complete original-box cover. */
int cs_precision_identity_transport_evaluate(cs_precision_cover_transport *t,cs_precision_constitutive_workspace *w,
    cs_precision_program *p,cs_osdi_identity_control_v5 *wire,const cs_osdi_identity_control_v5 *fixed,unsigned char *mapping){
  wire->claim_count=0;
  const uint32_t *quotient=(const void *)(mapping+fixed->quotient_region.byte_offset);
  for(size_t i=0;i<p->active_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    if(quotient[i]!=w->quotient[i])return CS_IDENTITY_INVALID;
  }
  const double *domain[4];for(unsigned j=0;j<4;++j)domain[j]=(const void *)(mapping+fixed->cover.base.regions[j+2].byte_offset);
  int status=cs_precision_constitutive_domain(w,domain[0],domain[1],domain[2],domain[3]);if(status)return status;
  status=cs_precision_cover_transport_evaluate(t,p,&wire->cover,&fixed->cover,mapping);
  if(status)return status;
  cs_osdi_precision_control_v1 local=wire->cover.base;local.protocol_version=CS_OSDI_RANGE_PROTOCOL_VERSION;
  uint64_t n=local.raw_node_count;
  cs_precision_decision_cover cover={.program=p,.program_bytes=p->header,.setup_bytes=p->setup,.control=local,
    .domain=t->workspace.domain,.nodes=t->workspace.nodes,.leaf_planes=t->workspace.leaf_planes,
    .node_count=wire->cover.node_count,.leaf_count=wire->cover.leaf_count,.plane_stride=4*n+4*n*n};
  const double *inputs[4];for(unsigned j=0;j<4;++j)inputs[j]=(const void *)(mapping+local.regions[j+2].byte_offset);
  cs_precision_constitutive_identities proof={0};
  status=cs_precision_constitutive_construct(p,&local,inputs[0],inputs[1],inputs[2],inputs[3],
    &t->limits,&t->workspace,&t->counters,&cover,w,&proof);
  if(status)goto done;
  uint64_t words=(fixed->limits.maximum_coefficient_bits-1)/64+1;
  uint64_t coefficients=(uint64_t)proof.claim_count*n;
  /* Geometry admitted all maximum products. Charge every initialized slot and
     the parent's bounded scan before any output write, without resetting owners. */
  uint64_t bytes=2*local.active_count*sizeof(uint32_t)+(uint64_t)proof.claim_count*sizeof(cs_osdi_identity_claim_v5)+
    coefficients*sizeof(cs_osdi_identity_coefficient_v5)+coefficients*words*sizeof(uint64_t);
  if(bytes>t->limits.maximum_copied_bytes-t->counters.copied_bytes){status=CS_IDENTITY_CAPACITY;goto done;}
  if(!cs_precision_charge(&p->math,bytes)){status=p->math.status;goto done;}
  t->counters.copied_bytes+=bytes;
  cs_osdi_identity_claim_v5 *claims=(void *)(mapping+fixed->claim_region.byte_offset);
  cs_osdi_identity_coefficient_v5 *values=(void *)(mapping+fixed->coefficient_region.byte_offset);
  uint64_t *limbs=(void *)(mapping+fixed->word_region.byte_offset);
  memset(limbs,0,(size_t)(coefficients*words*sizeof(uint64_t)));
  for(uint32_t claim=0;claim<proof.claim_count;++claim){
    claims[claim]=(cs_osdi_identity_claim_v5){proof.claims[claim].kind,0};
    for(uint32_t row=0;row<n;++row){
      mpq_srcptr q=proof.claims[claim].coefficients[row];
      mpz_srcptr numerator=mpq_numref(q),denominator=mpq_denref(q);
      uint64_t nb=mpz_sgn(numerator)?mpz_sizeinbase(numerator,2):0,db=mpz_sizeinbase(denominator,2);
      if(mpz_sgn(denominator)<=0 || mpz_popcount(denominator)!=1 ||
          nb>fixed->limits.maximum_coefficient_bits || db>fixed->limits.maximum_coefficient_bits ||
          (db>1 && mpz_even_p(numerator))){status=CS_IDENTITY_INVALID;goto done;}
      size_t count=0;uint64_t slot=(uint64_t)claim*n+row;
      mpz_export(limbs+slot*words,&count,-1,sizeof(uint64_t),0,0,numerator);
      values[slot]=(cs_osdi_identity_coefficient_v5){slot*words,count,db-1,mpz_sgn(numerator)<0,0};
    }
  }
  wire->claim_count=proof.claim_count;
done:
  wire->cover.completed_steps=t->counters.steps;
  wire->cover.completed_guard_records=t->counters.guard_records;
  wire->cover.completed_copied_bytes=t->counters.copied_bytes;
  if(status)wire->cover.node_count=wire->cover.leaf_count=0;
  p->has_bound=0;return status;
}
