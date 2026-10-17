#include "OSDIPrecisionProcess.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <dlfcn.h>

/* Deterministic syscall fault oracle; the production adapter has no test hook. */
static pid_t fail_waitpid_pid = -1;
pid_t waitpid(pid_t pid, int *status, int options) {
    if (fail_waitpid_pid > 0 && pid == fail_waitpid_pid) { fail_waitpid_pid = -1; errno = EAGAIN; return -1; }
    pid_t (*actual)(pid_t, int *, int) = (pid_t (*)(pid_t, int *, int))dlsym(RTLD_NEXT, "waitpid");
    assert(actual); return actual(pid, status, options);
}
static int cancelled_flag;
static int32_t cancelled(void *context) { (void)context; return cancelled_flag; }
int main(int argc, char **argv) {
    assert(argc == 2);
    cs_osdi_precision_process_options options = {0};
    options.maximum_mapping_bytes = 65536; options.arena_bytes = 65536;
    options.maximum_work_units = 1000; options.startup_timeout_nanoseconds = 200000000;
    options.evaluation_timeout_nanoseconds = 100000000; options.cleanup_timeout_nanoseconds = 1000000000;
    options.poll_nanoseconds = 1000000; options.minimum_precision_bits = 53; options.maximum_precision_bits = 128;
    unsigned char setup = 0;
    unsigned passed = 0;
    for (unsigned char mode = 0; mode <= 10; ++mode) {
        if (mode == 5) continue;
        cs_osdi_precision_session *s = NULL;
        cs_osdi_precision_process_issue issue;
        int32_t code = cs_osdi_precision_process_create(argv[1], &mode, 1, &setup, 1, 0, 1, 1,
            &options, cancelled, NULL, &s, &issue);
        if (mode == 1) { assert(code == CS_OSDI_PRECISION_PROCESS_RESOURCE && !s); ++passed; continue; }
        if (mode == 2) { assert(code == CS_OSDI_PRECISION_PROCESS_TIMEOUT && !s); ++passed; continue; }
        if (code || !s) fprintf(stderr,"mode%u create code%d errno%d exit%d signal%d\n",mode,code,issue.system_errno,issue.exit_code,issue.signal_number);
        assert(code == 0 && s);
        int32_t pid = cs_osdi_precision_process_identifier(s);
        if (mode == 3) { struct timespec wait = {0,100000000}; nanosleep(&wait,NULL); }
        double high = 1, low = 0, reference = 0;
        cs_osdi_precision_context_v1 context = {2,0,1,0};
        cs_osdi_precision_result_view result;
        memset(&result, 255, sizeof(result));
        code = cs_osdi_precision_process_evaluate(s, &high, &low, &reference, &low, 1,
            &context, &context, 128, cancelled, NULL, &result, &issue);
        if (mode == 0) { assert(code == 0 && result.f_high[0] == pid && result.delta_q_high[0] == 1); }
        else { assert(code != 0 && !result.f_high && result.raw_node_count == 0); }
        if (mode == 3) assert(code == CS_OSDI_PRECISION_PROCESS_WORKER);
        if (mode == 4 || mode == 8 || mode == 10) assert(code == CS_OSDI_PRECISION_PROCESS_PROTOCOL);
        if (mode == 6) assert(code == CS_OSDI_PRECISION_PROCESS_RESOURCE);
        if (mode == 7) assert(code == CS_OSDI_PRECISION_PROCESS_TIMEOUT);
        if (mode == 9) assert(code == CS_OSDI_PRECISION_PROCESS_NUMERIC && issue.worker_status == CS_OSDI_PRECISION_WORK_EXHAUSTED);
        int32_t close_code = cs_osdi_precision_process_close(&s, &issue);
        if (close_code) fprintf(stderr,"mode%u close code%d errno%d exit%d signal%d\n",mode,close_code,issue.system_errno,issue.exit_code,issue.signal_number);
        assert(close_code == 0 && !s);
        assert(kill(pid, 0) == -1 && errno == ESRCH);
        assert(cs_osdi_precision_process_close(&s, &issue) == 0);
        ++passed;
    }
    cs_osdi_precision_session *s = NULL; cs_osdi_precision_process_issue issue;
    unsigned char mode = 0; cancelled_flag = 1;
    assert(cs_osdi_precision_process_create(argv[1], &mode, 1, &setup, 1, 0, 1, 1,
        &options, cancelled, NULL, &s, &issue) == CS_OSDI_PRECISION_PROCESS_CANCELLED && !s);
    cancelled_flag = 0;
    assert(cs_osdi_precision_process_create(argv[1], &mode, 1, &setup, 1, 0, 1, 1,
        &options, cancelled, NULL, &s, &issue) == 0 && s);
    cancelled_flag = 1;
    double high = 1, low = 0;
    cs_osdi_precision_context_v1 context = {2,0,1,0};
    cs_osdi_precision_result_view result;
    assert(cs_osdi_precision_process_evaluate(s, &high, &low, &high, &low, 1,
        &context, &context, 128, cancelled, NULL, &result, &issue) == CS_OSDI_PRECISION_PROCESS_CANCELLED);
    assert(result.f_high == NULL && result.raw_node_count == 0);
    assert(cs_osdi_precision_process_close(&s, &issue) == 0 && !s);
    cancelled_flag = 0;
    assert(cs_osdi_precision_process_create(argv[1], &mode, 1, &setup, 1, 0, 1, 1,
        &options, cancelled, NULL, &s, &issue) == 0 && s);
    int32_t retained_pid = cs_osdi_precision_process_identifier(s);
    fail_waitpid_pid = retained_pid;
    assert(cs_osdi_precision_process_close(&s, &issue) == CS_OSDI_PRECISION_PROCESS_CLEANUP && s);
    assert(issue.system_errno == EAGAIN && cs_osdi_precision_process_identifier(s) == retained_pid);
    assert(cs_osdi_precision_process_close(&s, &issue) == 0 && !s);
    assert(kill(retained_pid, 0) == -1 && errno == ESRCH);
    printf("%u transport cases + cancelled startup/evaluation + retained cleanup retry passed\n", passed);
    return 0;
}
