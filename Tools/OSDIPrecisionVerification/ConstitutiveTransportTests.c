#include "../../Sources/COSDI/PrecisionProcess/ConstitutiveIdentities/Projection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int32_t cancelled(void *context){(void)context;return 1;}
static void coordinate_conditions(void){
  uint32_t q[3]={0,0,0},lower[3],upper[3];
  double lh[3]={0,1,8},ll[3]={0},uh[3]={10,2,9},ul[3]={0};
  const double *planes[4]={lh,ll,uh,ul};
#define CONDITION(expected) assert(cs_osdi_identity_condition_check(q,3,planes,lower,upper,UINT64_MAX,NULL,NULL)==expected)
  CONDITION(CS_OSDI_PRECISION_PROCESS_PROTOCOL);
  lh[2]=1;uh[2]=2;CONDITION(0);
  for(unsigned i=0;i<3;++i)lh[i]=uh[i]=1;
  ll[1]=ul[1]=0x1p-54;CONDITION(CS_OSDI_PRECISION_PROCESS_PROTOCOL);
  ll[1]=ul[1]=0;CONDITION(0);
  q[0]=q[1]=q[2]=UINT32_MAX;CONDITION(CS_OSDI_PRECISION_PROCESS_PROTOCOL);
  for(unsigned i=0;i<3;++i)lh[i]=uh[i]=0;
  CONDITION(0);q[0]=0;q[1]=0;q[2]=1;CONDITION(CS_OSDI_PRECISION_PROCESS_PROTOCOL);
  q[1]=1;q[2]=2;CONDITION(0);
  assert(cs_osdi_identity_condition_check(q,3,NULL,NULL,NULL,UINT64_MAX,NULL,NULL)==0);
  assert(cs_osdi_identity_condition_check(q,3,planes,lower,upper,UINT64_MAX,cancelled,NULL)==CS_OSDI_PRECISION_PROCESS_CANCELLED);
  assert(cs_osdi_identity_condition_check(q,3,planes,lower,upper,0,NULL,NULL)==CS_OSDI_PRECISION_PROCESS_TIMEOUT);
  assert(cs_osdi_identity_condition_check(q,UINT32_MAX,NULL,NULL,NULL,UINT64_MAX,NULL,NULL)==CS_OSDI_PRECISION_PROCESS_PROTOCOL);
#undef CONDITION
}
int main(void){
  coordinate_conditions();
  cs_osdi_identity_control_v5 h={0};h.cover.base.raw_node_count=2;
  h.limits.maximum_coefficient_bits=65;h.claim_count=1;h.cover.completed_copied_bytes=100000;
  cs_osdi_identity_claim_v5 claim={2,0};
  cs_osdi_identity_coefficient_v5 values[2]={{0,1,1,0,0},{2,1,1,1,0}};
  uint64_t words[4]={3,0,3,0};
#define CHECK(expected) assert(cs_osdi_identity_wire_check(&h,&claim,values,words,0,UINT64_MAX,NULL,NULL)==expected)
  CHECK(0); /* (3/2,-3/2), exact and without a floating conversion. */
  claim.kind=0;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);claim.kind=2;
  claim.reserved=1;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);claim.reserved=0;
  values[1].first_word=0;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);values[1].first_word=2;
  values[0].word_count=3;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);values[0].word_count=1;
  values[0].denominator_power=65;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);values[0].denominator_power=1;
  values[0].negative=2;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);values[0].negative=0;
  words[0]=2;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);words[0]=3;
  words[1]=1;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);words[1]=0;
  values[0].word_count=0;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);values[0].word_count=1;
  h.claim_count=5;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);h.claim_count=1;
  h.cover.completed_copied_bytes=1;CHECK(CS_OSDI_PRECISION_PROCESS_PROTOCOL);h.cover.completed_copied_bytes=100000;
  assert(cs_osdi_identity_wire_check(&h,&claim,values,words,0,UINT64_MAX,cancelled,NULL)==CS_OSDI_PRECISION_PROCESS_CANCELLED);
  CHECK(0);puts("constitutive transport falsifiers passed");return 0;
}
