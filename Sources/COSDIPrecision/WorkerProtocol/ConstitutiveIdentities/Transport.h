#ifndef CS_PRECISION_IDENTITY_TRANSPORT_H
#define CS_PRECISION_IDENTITY_TRANSPORT_H
#include "../DecisionCover/Transport.h"
#include "../../ConstitutiveIdentities/ConstitutiveIdentities.h"
int cs_precision_identity_transport_open(cs_precision_program *,const cs_osdi_identity_control_v5 *,const uint32_t *,cs_precision_constitutive_workspace *);
int cs_precision_identity_transport_evaluate(cs_precision_cover_transport *,cs_precision_constitutive_workspace *,
    cs_precision_program *,cs_osdi_identity_control_v5 *,const cs_osdi_identity_control_v5 *,unsigned char *);
#endif
