#ifndef CS_OSDI_COVER_PROJECTION_H
#define CS_OSDI_COVER_PROJECTION_H
#include "../../include/OSDIPrecisionProcess.h"
int cs_osdi_cover_wire_check(const cs_osdi_cover_control_v4 *,const unsigned char *,uint64_t,
    const cs_osdi_cover_node_v4 *,const double *,uint32_t *,uint32_t *,uint64_t,uint64_t,uint64_t,
    cs_osdi_precision_cancel_probe,void *);
#endif
