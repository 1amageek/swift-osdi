#ifndef CS_OSDI_LIBRARY_INTERNAL_H
#define CS_OSDI_LIBRARY_INTERNAL_H
#include "COSDI.h"
#include "../ABI/osdi_0_4.h"
int32_t cs_osdi_borrow_descriptor(const cs_osdi_library *library, uint32_t index,
                                 const OsdiDescriptor **out, cs_osdi_error *error);
int32_t cs_osdi_bind_logger(cs_osdi_library *library,
                            void (*logger)(void *, const char *, uint32_t), cs_osdi_error *error);
#endif
