#ifndef CS_PRECISION_COVER_TRANSPORT_H
#define CS_PRECISION_COVER_TRANSPORT_H
#include "../../DecisionCover/DecisionCover.h"
typedef struct {
  cs_precision_cover_limits limits;
  cs_precision_cover_workspace workspace;
  cs_precision_cover_counters counters;
} cs_precision_cover_transport;
int cs_precision_cover_transport_open(cs_precision_cover_transport *,const cs_osdi_cover_control_v4 *,double *[8]);
int cs_precision_cover_transport_evaluate(cs_precision_cover_transport *,cs_precision_program *,
    cs_osdi_cover_control_v4 *,const cs_osdi_cover_control_v4 *,unsigned char *);
#endif
