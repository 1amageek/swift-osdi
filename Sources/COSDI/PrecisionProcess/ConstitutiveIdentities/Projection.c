#include "Projection.h"
#include <time.h>
#include <math.h>
static int poll_bound(uint64_t until,cs_osdi_precision_cancel_probe probe,void *context){
  if(probe && probe(context))return CS_OSDI_PRECISION_PROCESS_CANCELLED;
  struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return CS_OSDI_PRECISION_PROCESS_IO;
  uint64_t ns=(uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
  return ns>=until?CS_OSDI_PRECISION_PROCESS_TIMEOUT:0;
}
int cs_osdi_identity_wire_check(const cs_osdi_identity_control_v5 *h,const cs_osdi_identity_claim_v5 *claims,
    const cs_osdi_identity_coefficient_v5 *values,const uint64_t *words,uint64_t prior_copies,uint64_t until,
    cs_osdi_precision_cancel_probe probe,void *context){
  uint64_t n=h->cover.base.raw_node_count,cap=h->limits.maximum_coefficient_bits;
  if(h->claim_count>2*n)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  uint64_t stride=(cap-1)/64+1,coefficients=h->claim_count*n;
  uint64_t bytes=h->claim_count*sizeof(*claims)+coefficients*sizeof(*values)+coefficients*stride*sizeof(*words);
  uint64_t cover_bytes=4*h->cover.base.active_count*sizeof(double)+
    (uint64_t)h->cover.node_count*sizeof(cs_osdi_cover_node_v4)+
    (uint64_t)h->cover.leaf_count*(4*n+4*n*n)*sizeof(double)+
    ((uint64_t)h->cover.node_count+h->cover.leaf_count)*sizeof(uint32_t);
  /* The worker prepays the independent parent's two interval-index writes. */
  cover_bytes+=2*h->cover.base.active_count*sizeof(uint32_t);
  if(h->cover.completed_copied_bytes<prior_copies || cover_bytes>h->cover.completed_copied_bytes-prior_copies ||
      bytes>h->cover.completed_copied_bytes-prior_copies-cover_bytes)
    return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  uint32_t previous_kind=1;uint64_t kind_count=0;
  for(uint64_t claim=0;claim<h->claim_count;++claim){
    if(claims[claim].reserved || claims[claim].kind<previous_kind || claims[claim].kind>2)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    if(claims[claim].kind!=previous_kind){previous_kind=claims[claim].kind;kind_count=0;}
    if(++kind_count>n)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    int nonzero=0;
    for(uint64_t row=0;row<n;++row){
      uint64_t slot=claim*n+row;const cs_osdi_identity_coefficient_v5 *v=&values[slot];
      if(v->reserved || v->negative>1 || v->first_word!=slot*stride || v->word_count>stride ||
          v->denominator_power>=cap || (!v->word_count && (v->negative || v->denominator_power)))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      if(v->word_count){
        uint64_t high=words[v->first_word+v->word_count-1];
        if(!high || 64*(v->word_count-1)+64-(uint64_t)__builtin_clzll(high)>cap ||
            (v->denominator_power && !(words[v->first_word]&1)))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
        nonzero=1;
      }
      for(uint64_t j=0;j<stride;++j){
        if(!(j%256)){int status=poll_bound(until,probe,context);if(status)return status;}
        if(j>=v->word_count && words[v->first_word+j])return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
      }
    }
    if(!nonzero)return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  }
  return poll_bound(until,probe,context);
}

static int pair_le(double ah,double al,double bh,double bl){return ah<bh||(ah==bh&&al<=bl);}
int cs_osdi_identity_condition_check(const uint32_t *quotient,uint64_t count,const double *const planes[4],
    uint32_t *lower,uint32_t *upper,uint64_t until,cs_osdi_precision_cancel_probe probe,void *context){
  if(count>=UINT32_MAX||(count&&!quotient)||(planes&&count&&(!lower||!upper)))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
  for(uint32_t i=0;i<count;++i){
    if(!(i%256)){int status=poll_bound(until,probe,context);if(status)return status;}
    uint32_t r=quotient[i];if(r!=UINT32_MAX&&(r>i||quotient[r]!=r))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    if(!planes)continue;
    double lh=planes[0][i],ll=planes[1][i],uh=planes[2][i],ul=planes[3][i];
    if(!isfinite(lh)||!isfinite(ll)||!isfinite(uh)||!isfinite(ul)||lh+ll!=lh||uh+ul!=uh||!pair_le(lh,ll,uh,ul))
      return CS_OSDI_PRECISION_PROCESS_PROTOCOL;
    lower[i]=upper[i]=i;
    if(r==UINT32_MAX){if(!pair_le(lh,ll,0,0)||!pair_le(0,0,uh,ul))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;}
    else{
      uint32_t l=lower[r],u=upper[r];
      if(!pair_le(lh,ll,planes[0][l],planes[1][l]))lower[r]=i;
      if(!pair_le(planes[2][u],planes[3][u],uh,ul))upper[r]=i;
    }
  }
  for(uint32_t i=0;planes&&i<count;++i){
    if(!(i%256)){int status=poll_bound(until,probe,context);if(status)return status;}
    if(quotient[i]==i){uint32_t l=lower[i],u=upper[i];
      if(!pair_le(planes[0][l],planes[1][l],planes[2][u],planes[3][u]))return CS_OSDI_PRECISION_PROCESS_PROTOCOL;}
  }
  return poll_bound(until,probe,context);
}
