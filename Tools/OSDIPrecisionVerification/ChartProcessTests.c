#include "../../Sources/COSDI/include/OSDIPrecisionProcess.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Chart process verification requires enabled assertions"
#endif

/* Deterministic cleanup syscall fault; every numerical result uses the real worker. */
static pid_t fail_waitpid_pid = -1;
pid_t waitpid(pid_t pid, int *status, int flags) {
    if (pid == fail_waitpid_pid) { fail_waitpid_pid = -1; errno = EAGAIN; return -1; }
    pid_t (*actual)(pid_t, int *, int) = (pid_t (*)(pid_t, int *, int))dlsym(RTLD_NEXT, "waitpid");
    assert(actual); return actual(pid, status, flags);
}
static size_t product(size_t a, size_t b) {
    assert(!b || a <= SIZE_MAX / b); return a * b;
}
static void *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb"); assert(file && !fseek(file, 0, SEEK_END));
    long n = ftell(file); assert(n > 0 && (uint64_t)n <= UINT64_C(134217728) && !fseek(file, 0, SEEK_SET));
    *length = (size_t)n; void *bytes = malloc(*length); assert(bytes);
    assert(fread(bytes, 1, *length, file) == *length && !fclose(file)); return bytes;
}
typedef struct {
    void *program, *setup;
    size_t program_bytes, setup_bytes;
    CSOsdiPrecisionProgramHeader header;
    CSOsdiPrecisionSetupHeader setup_header;
} input;
static input load(const char *program, const char *setup) {
    input in = {0};
    in.program = read_file(program, &in.program_bytes); in.setup = read_file(setup, &in.setup_bytes);
    assert(in.program_bytes >= sizeof(in.header) && in.setup_bytes >= sizeof(in.setup_header));
    memcpy(&in.header, in.program, sizeof(in.header)); memcpy(&in.setup_header, in.setup, sizeof(in.setup_header));
    return in;
}
static void release(input *in) { free(in->program); free(in->setup); }
static cs_osdi_precision_process_options options(uint64_t arena, uint64_t work) {
    cs_osdi_precision_process_options result = {0};
    result.maximum_mapping_bytes = UINT64_C(134217728); result.arena_bytes = arena;
    result.maximum_work_units = work; result.minimum_precision_bits = 128; result.maximum_precision_bits = 256;
    result.startup_timeout_nanoseconds = result.evaluation_timeout_nanoseconds = UINT64_C(10000000000);
    result.cleanup_timeout_nanoseconds = UINT64_C(2000000000); result.poll_nanoseconds = UINT64_C(1000000);
    return result;
}
static cs_osdi_precision_session *start(const char *worker, const input *in,
    cs_osdi_precision_process_options policy, cs_osdi_chart_limits_v3 limits) {
    cs_osdi_precision_session *session = NULL; cs_osdi_precision_process_issue issue;
    int status = cs_osdi_chart_process_create(worker, in->program, in->program_bytes, in->setup, in->setup_bytes,
        in->header.descriptor_index, in->setup_header.active_unknown_count, in->header.raw_node_count,
        &policy, &limits, NULL, NULL, &session, &issue);
    if (status) fprintf(stderr, "chart start: code=%d status=%u exit=%d errno=%d\n", status, issue.worker_status, issue.exit_code, issue.system_errno);
    assert(!status && session); return session;
}
static void close_session(cs_osdi_precision_session **session) {
    cs_osdi_precision_process_issue issue; assert(!cs_osdi_precision_process_close(session, &issue) && !*session);
    assert(!cs_osdi_precision_process_close(session, &issue));
}
static int scalar(cs_osdi_precision_session *s, double x, uint32_t mode,
    const cs_osdi_chart_decision_v3 *tape, size_t count, cs_osdi_chart_result_view *out,
    cs_osdi_precision_process_issue *issue) {
    double low = 0; cs_osdi_precision_context_v1 context = {2,0,0,0};
    return cs_osdi_chart_process_evaluate(s, &x, &low, &x, &low, 1, &context, &context, 192,
        mode, tape, count, NULL, NULL, out, issue);
}
static cs_osdi_chart_decision_v3 *copy_tape(const cs_osdi_chart_result_view *view) {
    size_t bytes = product((size_t)view->guard_count + 1, sizeof(cs_osdi_chart_decision_v3));
    cs_osdi_chart_decision_v3 *tape = calloc(1, bytes); assert(tape);
    for (uint64_t i = 0; i < view->guard_count; ++i) tape[i] = view->guards[i].decision;
    return tape;
}
static int cancel(void *context) { unsigned *calls = context; ++*calls; return 1; }
static void synthetic(const char *worker, const char *directory) {
    char pp[4096], sp[4096];
    int n = snprintf(pp,sizeof(pp),"%s/jump.program",directory); assert(n>0 && (size_t)n<sizeof(pp));
    n = snprintf(sp,sizeof(sp),"%s/jump.setup",directory); assert(n>0 && (size_t)n<sizeof(sp));
    input in = load(pp,sp);
    cs_osdi_precision_process_options policy = options(UINT64_C(1048576), UINT64_C(10000000));
    cs_osdi_chart_limits_v3 limits = {10000,100,100};
    cs_osdi_precision_session *session = start(worker,&in,policy,limits);
    cs_osdi_chart_result_view view = {0}; cs_osdi_precision_process_issue issue;
    assert(!scalar(session,.75,CS_OSDI_CHART_RECORD,NULL,0,&view,&issue));
    assert(view.guard_count==3 && view.semantics==CS_OSDI_CHART_RECORD && view.formulas.request_sequence==1);
    uint64_t work = view.formulas.completed_work_units, steps = view.completed_steps, count = view.guard_count;
    const double *address = view.formulas.f_lower;
    cs_osdi_chart_decision_v3 *tape = copy_tape(&view);
    assert(!scalar(session,.75,CS_OSDI_CHART_EXACT_GRAPH,tape,(size_t)count,&view,&issue));
    assert(view.formulas.f_lower==address && view.formulas.completed_work_units>work && view.completed_steps==2*steps);
    assert(!scalar(session,.5,CS_OSDI_CHART_FORMULA_CLOSURE,tape,(size_t)count,&view,&issue));
    assert(view.semantics==CS_OSDI_CHART_FORMULA_CLOSURE && view.guards[0].boundary_extension==1);
    assert(view.formulas.q_lower[0]==.75 && view.formulas.q_upper[0]==.75 && view.formulas.f_lower[0]==2);
    fail_waitpid_pid = cs_osdi_precision_process_identifier(session);
    assert(cs_osdi_precision_process_close(&session,&issue)==CS_OSDI_PRECISION_PROCESS_CLEANUP && session);
    assert(issue.system_errno==EAGAIN); close_session(&session);
    session=start(worker,&in,policy,limits);
    assert(!scalar(session,.5,CS_OSDI_CHART_RECORD,NULL,0,&view,&issue));
    assert(view.formulas.q_lower[0]==.625 && view.formulas.f_lower[0]==1 && !view.guards[0].decision.choice);
    assert(scalar(session,.5,CS_OSDI_CHART_EXACT_GRAPH,tape,(size_t)count,&view,&issue)==CS_OSDI_PRECISION_PROCESS_NUMERIC);
    assert(issue.worker_status==CS_OSDI_CHART_TAPE_MISMATCH && !view.guards && !view.formulas.f_lower);
    close_session(&session);
    for (unsigned failure=0;failure<5;++failure) {
        cs_osdi_chart_limits_v3 bounded=limits; cs_osdi_precision_process_options budget=policy;
        if(failure==0) bounded.maximum_guard_records=1;
        if(failure==1) bounded.maximum_steps=2*steps-1;
        if(failure==2) budget.maximum_work_units=2*work-1;
        session=start(worker,&in,budget,bounded);
        if(failure==1 || failure==2) assert(!scalar(session,.75,CS_OSDI_CHART_RECORD,NULL,0,&view,&issue));
        if(failure==3) tape[0].block+=1;
        int status=scalar(session,.75,failure==3?CS_OSDI_CHART_EXACT_GRAPH:CS_OSDI_CHART_RECORD,
            failure==3?tape:NULL,failure==3?(size_t)count:0,&view,&issue);
        if(failure<4) {
            assert(status==CS_OSDI_PRECISION_PROCESS_NUMERIC);
            assert(issue.worker_status==(failure==0?CS_OSDI_CHART_CAPACITY:failure==1?CS_OSDI_CHART_STEP_LIMIT:
                failure==2?CS_OSDI_PRECISION_WORK_EXHAUSTED:CS_OSDI_CHART_TAPE_MISMATCH));
            assert(!view.guards && !view.formulas.f_lower);
        } else {
            assert(!status);
            unsigned calls=0; double x=.75,low=0; cs_osdi_precision_context_v1 ctx={2,0,0,0};
            assert(cs_osdi_chart_process_evaluate(session,&x,&low,&x,&low,1,&ctx,&ctx,192,
                CS_OSDI_CHART_RECORD,NULL,0,cancel,&calls,&view,&issue)==CS_OSDI_PRECISION_PROCESS_CANCELLED);
            assert(calls && !view.guards && !view.formulas.f_lower);
        }
        close_session(&session);
    }
    free(tape); release(&in); puts("chart-process synthetic passed");
}
static void sentinel_fixtures(const char *worker,const char *directory) {
    char pp[4096],sp[4096];cs_osdi_precision_process_options policy=options(1048576,10000000);
    cs_osdi_chart_limits_v3 limits={10000,100,100};cs_osdi_precision_process_issue issue;cs_osdi_chart_result_view view;
    for(unsigned overflow=0;overflow<2;++overflow) {
        const char *name=overflow?"overflow":"sentinels";
        int n=snprintf(pp,sizeof(pp),"%s/%s.program",directory,name);assert(n>0 && (size_t)n<sizeof(pp));
        n=snprintf(sp,sizeof(sp),"%s/%s.setup",directory,name);assert(n>0 && (size_t)n<sizeof(sp));
        input in=load(pp,sp);cs_osdi_precision_session *session=start(worker,&in,policy,limits);
        int status=scalar(session,.25,CS_OSDI_CHART_RECORD,NULL,0,&view,&issue);
        if(overflow) {assert(status==CS_OSDI_PRECISION_PROCESS_NUMERIC && issue.worker_status==CS_OSDI_PRECISION_NONFINITE_PROJECTION && !view.guards);}
        else {
            assert(!status && view.guard_count==4 && view.formulas.f_lower[0]==.25 && view.formulas.q_lower[0]==.25);
            assert(view.guards[0].a_type==CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY);
            assert(view.guards[1].b_type==CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY);
            cs_osdi_chart_decision_v3 *tape=copy_tape(&view);size_t count=(size_t)view.guard_count;
            assert(!scalar(session,.25,CS_OSDI_CHART_EXACT_GRAPH,tape,count,&view,&issue));
            tape[0].choice=0;
            assert(scalar(session,.25,CS_OSDI_CHART_FORMULA_CLOSURE,tape,count,&view,&issue)==CS_OSDI_PRECISION_PROCESS_NUMERIC);
            assert(issue.worker_status==CS_OSDI_CHART_TAPE_MISMATCH);free(tape);
        }
        close_session(&session);release(&in);
    }
    puts("chart-process primitive infinity and finite-overflow tests passed");
}
static void forgeries(const char *worker,const char *program,const char *setup) {
    input in=load(program,setup);cs_osdi_chart_limits_v3 limits={10000,100,100};
    for(unsigned mode=1;mode<=6;++mode) {
        cs_osdi_precision_process_options policy=options(1048576,10000000);policy.program_sha256[0]=(unsigned char)mode;
        cs_osdi_precision_session *session=start(worker,&in,policy,limits);
        cs_osdi_chart_result_view view;cs_osdi_precision_process_issue issue;
        assert(scalar(session,.25,CS_OSDI_CHART_RECORD,NULL,0,&view,&issue)==CS_OSDI_PRECISION_PROCESS_PROTOCOL);
        assert(!view.guards && !view.formulas.f_lower);close_session(&session);
    }
    release(&in);puts("chart-process infinity/NaN/provenance/output forgery refusals passed");
}
static void actual(int argc, char **argv) {
    assert(argc==10);
    input in=load(argv[2],argv[3]); size_t request_bytes;
    unsigned char *request=read_file(argv[4],&request_bytes); assert(request_bytes>=sizeof(cs_osdi_precision_control_v1));
    cs_osdi_precision_control_v1 control; memcpy(&control,request,sizeof(control));
    uint64_t active=in.setup_header.active_unknown_count;
    assert(request_bytes==sizeof(control)+product(product((size_t)active,4),sizeof(double)));
    cs_osdi_precision_process_options policy=options(strtoull(argv[5],NULL,10),strtoull(argv[6],NULL,10));
    policy.minimum_precision_bits=control.minimum_precision_bits;policy.maximum_precision_bits=control.maximum_precision_bits;
    memcpy(policy.program_sha256,control.program_sha256,32);memcpy(policy.worker_profile_sha256,control.worker_profile_sha256,32);
    cs_osdi_chart_limits_v3 limits={strtoull(argv[7],NULL,10),strtoull(argv[8],NULL,10),strtoull(argv[9],NULL,10)};
    cs_osdi_precision_session *session=start(argv[1],&in,policy,limits);
    const double *v=(const void *)(request+sizeof(control)); cs_osdi_chart_result_view view={0};cs_osdi_precision_process_issue issue;
    int record_status=cs_osdi_chart_process_evaluate(session,v,v+active,v+2*active,v+3*active,active,&control.candidate,&control.reference,
        control.requested_precision_bits,CS_OSDI_CHART_RECORD,NULL,0,NULL,NULL,&view,&issue);
    if(record_status)fprintf(stderr,"actual record: code=%d status=%u instruction=%u exit=%d signal=%d errno=%d sequence=%" PRIu64
        " precision=%u active=%" PRIu64 " raw=%u arena=%" PRIu64 " work=%" PRIu64 " steps=%" PRIu64 " tape=%" PRIu64 " guards=%" PRIu64 "\n",
        record_status,issue.worker_status,issue.instruction,issue.exit_code,issue.signal_number,issue.system_errno,issue.request_sequence,
        control.requested_precision_bits,active,in.header.raw_node_count,policy.arena_bytes,policy.maximum_work_units,
        limits.maximum_steps,limits.maximum_tape_entries,limits.maximum_guard_records);
    assert(!record_status);
    cs_osdi_chart_decision_v3 *tape=copy_tape(&view); uint64_t count=view.guard_count,work=view.formulas.completed_work_units,steps=view.completed_steps;
    uint64_t nodes=view.formulas.raw_node_count;
    size_t values=product(4,(size_t)nodes)+product(4,product((size_t)nodes,(size_t)nodes));
    double *snapshot=malloc(product(values,sizeof(double)));assert(snapshot);
    const double *planes[]={view.formulas.f_lower,view.formulas.f_upper,view.formulas.q_lower,view.formulas.q_upper,
        view.formulas.g_lower,view.formulas.g_upper,view.formulas.c_lower,view.formulas.c_upper};
    size_t at=0;
    for(unsigned i=0;i<8;++i) {size_t n=i<4?(size_t)nodes:product((size_t)nodes,(size_t)nodes);memcpy(snapshot+at,planes[i],product(n,sizeof(double)));at+=n;}
    int status=cs_osdi_chart_process_evaluate(session,v,v+active,v+2*active,v+3*active,active,&control.candidate,&control.reference,
        control.requested_precision_bits,CS_OSDI_CHART_EXACT_GRAPH,tape,count,NULL,NULL,&view,&issue);
    if(status)fprintf(stderr,"actual replay: code=%d status=%u instruction=%u\n",status,issue.worker_status,issue.instruction);
    assert(!status && view.guard_count==count && view.formulas.completed_work_units>work && view.completed_steps==2*steps);
    const double *replayed[]={view.formulas.f_lower,view.formulas.f_upper,view.formulas.q_lower,view.formulas.q_upper,
        view.formulas.g_lower,view.formulas.g_upper,view.formulas.c_lower,view.formulas.c_upper};at=0;
    for(unsigned i=0;i<8;++i) {size_t n=i<4?(size_t)nodes:product((size_t)nodes,(size_t)nodes);assert(!memcmp(snapshot+at,replayed[i],product(n,sizeof(double))));at+=n;}
    printf("chart-process actual passed guards=%" PRIu64 " steps=%" PRIu64 " work=%" PRIu64 "\n",count,view.completed_steps,view.formulas.completed_work_units);
    close_session(&session);free(snapshot);free(tape);free(request);release(&in);
}
int main(int argc,char **argv) { if(argc==3){synthetic(argv[1],argv[2]);sentinel_fixtures(argv[1],argv[2]);}else if(argc==4)forgeries(argv[1],argv[2],argv[3]);else actual(argc,argv);return 0; }
