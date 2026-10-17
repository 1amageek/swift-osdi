#include <COSDIPrecision.h>
#include <OSDIPrecisionProtocol.h>
int main(void) {
  return cs_osdi_precision_worker_run(CS_OSDI_PRECISION_SLAB_FD,
                                      CS_OSDI_PRECISION_REQUEST_FD,
                                      CS_OSDI_PRECISION_RESPONSE_FD);
}
