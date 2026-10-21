#ifndef CS_OSDI_PRECISION_WORKER_H
#define CS_OSDI_PRECISION_WORKER_H
/* Owns one isolated C worker; callers supply inherited shared-memory/pipe fds.
 */
int cs_osdi_precision_worker_run(int slab_fd, int request_read_fd,
                                 int response_write_fd);
#endif
