#ifndef CS_OSDI_IDENTITY_PROJECTION_H
#define CS_OSDI_IDENTITY_PROJECTION_H
#include "../../include/OSDIPrecisionProcess.h"
int cs_osdi_identity_wire_check(const cs_osdi_identity_control_v5 *,const cs_osdi_identity_claim_v5 *,
    const cs_osdi_identity_coefficient_v5 *,const uint64_t *,uint64_t,uint64_t,cs_osdi_precision_cancel_probe,void *);
int cs_osdi_identity_condition_check(const uint32_t *,uint64_t,const double *const [4],
    uint32_t *,uint32_t *,uint64_t,cs_osdi_precision_cancel_probe,void *);
#endif
