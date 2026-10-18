#include "OSDIPrecisionProcess.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* One deterministic cleanup syscall fault; numeric results come from the real worker. */
static pid_t fail_waitpid_pid = -1;
pid_t waitpid(pid_t pid, int *status, int options) {
    if (pid == fail_waitpid_pid) { fail_waitpid_pid = -1; errno = EAGAIN; return -1; }
    pid_t (*actual)(pid_t, int *, int) = (pid_t (*)(pid_t, int *, int))dlsym(RTLD_NEXT, "waitpid");
    assert(actual); return actual(pid, status, options);
}
static int cancel_flag, cancel_calls;
static int32_t cancelled(void *context) { (void)context; ++cancel_calls; return cancel_flag; }
static unsigned char *read_bytes(const char *path, uint64_t *count) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); long length = ftell(file);
    assert(length > 0 && length <= 1048576 && fseek(file, 0, SEEK_SET) == 0);
    unsigned char *bytes = malloc((size_t)length); assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    *count = (uint64_t)length; return bytes;
}
static void empty(const cs_osdi_range_result_view *view) {
    assert(!view->raw_node_count && !view->f_lower && !view->f_upper && !view->q_lower && !view->q_upper);
    assert(!view->g_lower && !view->g_upper && !view->c_lower && !view->c_upper);
}
static void closed(cs_osdi_precision_session **session) {
    int32_t pid = cs_osdi_precision_process_identifier(*session);
    cs_osdi_precision_process_issue issue;
    assert(cs_osdi_precision_process_close(session, &issue) == 0 && !*session);
    assert(kill(pid, 0) == -1 && errno == ESRCH);
    assert(cs_osdi_precision_process_close(session, &issue) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 6); /* real worker, polynomial program/setup, floor program/setup */
    uint64_t program_count, setup_count, floor_count, floor_setup_count;
    unsigned char *program = read_bytes(argv[2], &program_count), *setup = read_bytes(argv[3], &setup_count);
    unsigned char *floor_program = read_bytes(argv[4], &floor_count), *floor_setup = read_bytes(argv[5], &floor_setup_count);
    cs_osdi_precision_process_options options = {0};
    options.maximum_mapping_bytes = 1048576; options.arena_bytes = 1048576;
    options.maximum_work_units = 1000000; options.minimum_precision_bits = 128; options.maximum_precision_bits = 256;
    options.startup_timeout_nanoseconds = options.evaluation_timeout_nanoseconds = options.cleanup_timeout_nanoseconds = 1000000000;
    options.poll_nanoseconds = 1000000;
    cs_osdi_precision_session *session = NULL; cs_osdi_precision_process_issue issue;
    cs_osdi_range_result_view view;
    cs_osdi_precision_context_v1 lower_time = {2,0,.25,0}, upper_time = {2,0,.5,0};
    double low = 1, high = 2, zero = 0;
    assert(cs_osdi_range_process_create(argv[1], program, program_count, setup, setup_count, 0,1,1,
        &options, cancelled,NULL,&session,&issue) == 0);
    assert(cs_osdi_range_process_evaluate(session,&low,&zero,&high,&zero,1,&lower_time,&upper_time,192,
        cancelled,NULL,&view,&issue) == 0);
    assert(view.raw_node_count == 1 && view.program_has_time_input == 1 && view.request_sequence == 1);
    assert(view.f_lower[0] <= 1.25 && view.f_upper[0] >= 4.5);
    assert(view.q_lower[0] <= 1.5 && view.q_upper[0] >= 9);
    assert(view.g_lower[0] <= 2 && view.g_upper[0] >= 4);
    assert(view.c_lower[0] <= 3 && view.c_upper[0] >= 12);
    const double *address = view.f_lower;
    /* Lexical input planes may alias each other. */
    assert(cs_osdi_range_process_evaluate(session,&low,&zero,&low,&zero,1,&lower_time,&lower_time,192,
        cancelled,NULL,&view,&issue) == 0 && view.f_lower == address && view.request_sequence == 2);
    closed(&session);
    unsigned refusals = 0;
    for (unsigned mode = 0; mode < 6; ++mode) {
        assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
            &options,cancelled,NULL,&session,&issue) == 0);
        double lh = 1, ll = 0, uh = 2, ul = 0; uint64_t count = 1;
        cs_osdi_precision_context_v1 lc = lower_time, uc = upper_time;
        if (mode == 0) { lh = 2; uh = 1; }
        if (mode == 1) { uc.mode = 3; uc.time_high = 0; }
        if (mode == 2) { lc.time_high = 1; uc.time_high = 0; }
        if (mode == 3) ll = .5;
        if (mode == 4) count = 0;
        if (mode == 5) cancel_flag = 1;
        memset(&view,255,sizeof view);
        int code = cs_osdi_range_process_evaluate(session,&lh,&ll,&uh,&ul,count,&lc,&uc,192,cancelled,NULL,&view,&issue);
        assert(code == (mode == 5 ? CS_OSDI_PRECISION_PROCESS_CANCELLED : CS_OSDI_PRECISION_PROCESS_INVALID));
        empty(&view);cancel_flag = 0;closed(&session);++refusals;
    }
    assert(cs_osdi_range_process_create(argv[1],floor_program,floor_count,floor_setup,floor_setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == 0);
    low = .5; high = 1.5;
    assert(cs_osdi_range_process_evaluate(session,&low,&zero,&high,&zero,1,&lower_time,&upper_time,192,
        cancelled,NULL,&view,&issue) == CS_OSDI_PRECISION_PROCESS_NONSMOOTH);
    assert(issue.worker_status == CS_OSDI_PRECISION_NONSMOOTH && CS_OSDI_PRECISION_PROCESS_IO == 10);
    empty(&view);closed(&session);
    assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == 0);
    cs_osdi_precision_result_view point_view;memset(&point_view,255,sizeof point_view);
    cancel_calls = 0;cancel_flag = 1;
    assert(cs_osdi_precision_process_evaluate(session,&low,&zero,&high,&zero,1,&lower_time,&upper_time,192,
        cancelled,NULL,&point_view,&issue) == CS_OSDI_PRECISION_PROCESS_INVALID);
    assert(cancel_calls == 0 && issue.request_sequence == 0 && !point_view.f_high && !point_view.raw_node_count);
    cancel_flag = 0;closed(&session);
    assert(cs_osdi_precision_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == 0);
    cancel_calls = 0;cancel_flag = 1;
    assert(cs_osdi_range_process_evaluate(session,&low,&zero,&high,&zero,1,&lower_time,&upper_time,192,
        cancelled,NULL,&view,&issue) == CS_OSDI_PRECISION_PROCESS_INVALID);
    assert(cancel_calls == 0 && issue.request_sequence == 0);empty(&view);
    cancel_flag = 0;closed(&session);
    options.maximum_mapping_bytes = 1;
    assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == CS_OSDI_PRECISION_PROCESS_RESOURCE && !session);
    options.maximum_mapping_bytes = 1048576;options.maximum_work_units = 1;
    int work_code = cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue);
    if (!work_code) {
        low = 1;high = 2;
        work_code = cs_osdi_range_process_evaluate(session,&low,&zero,&high,&zero,1,&lower_time,&upper_time,192,
            cancelled,NULL,&view,&issue);
        empty(&view);closed(&session);
    }
    assert(work_code == CS_OSDI_PRECISION_PROCESS_NUMERIC && issue.worker_status == CS_OSDI_PRECISION_WORK_EXHAUSTED);
    options.maximum_work_units = 1000000;
    assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,0,1,
        &options,cancelled,NULL,&session,&issue) == CS_OSDI_PRECISION_PROCESS_NUMERIC && !session);
    assert(issue.worker_status == CS_OSDI_PRECISION_INVALID_PROGRAM);
    cancel_flag = 1;
    assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == CS_OSDI_PRECISION_PROCESS_CANCELLED && !session);
    cancel_flag = 0;
    assert(cs_osdi_range_process_create(argv[1],program,program_count,setup,setup_count,0,1,1,
        &options,cancelled,NULL,&session,&issue) == 0);
    int32_t pid = cs_osdi_precision_process_identifier(session);fail_waitpid_pid = pid;
    assert(cs_osdi_precision_process_close(&session,&issue) == CS_OSDI_PRECISION_PROCESS_CLEANUP && session);
    assert(issue.system_errno == EAGAIN && cs_osdi_precision_process_identifier(session) == pid);
    closed(&session);
    free(program);free(setup);free(floor_program);free(floor_setup);
    printf("real-worker range containment, %u geometry/cancel refusals, nonsmooth mapping, cross-operation, capacity/work and retained cleanup passed\n",refusals);
    return 0;
}
