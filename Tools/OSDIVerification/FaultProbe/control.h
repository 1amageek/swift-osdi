#ifndef OSDI_FAULT_PROBE_CONTROL_H
#define OSDI_FAULT_PROBE_CONTROL_H
void *fake_handle(void);
int fake_close_count(void);
void fake_allow_close(void);
void fake_reset(void);
#endif
