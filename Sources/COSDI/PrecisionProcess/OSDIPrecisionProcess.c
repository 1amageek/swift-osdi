#include "../include/OSDIPrecisionProcess.h"
#include "DecisionCover/Projection.h"
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* One exclusive Swift session owns this context. No process-global handlers or
 * allocator hooks are installed in the parent. Every mapping/fd/child is released
 * exactly once by close; an incomplete reap retains the entire context. */
struct cs_osdi_precision_session {
    void *mapping;
    size_t mapping_bytes;
    cs_osdi_precision_control_v1 *control;
    cs_osdi_precision_process_options options;
    cs_osdi_precision_control_v1 geometry;
    pid_t pid;
    int request_fd;
    int response_fd;
    int invalid;
    int reaped;
    int wait_status;
    uint64_t sequence;
    uint32_t program_has_time_input;
    cs_osdi_chart_control_v3 chart_geometry;
    cs_osdi_cover_control_v4 cover_geometry;
    uint32_t *cover_node_visits,*cover_leaf_visits;
    uint64_t completed_guard_records,completed_copied_bytes;
    uint64_t completed_work, completed_steps;
    const unsigned char *chart_constants;
    uint32_t chart_constant_count;
};

static void clear_issue(cs_osdi_precision_process_issue *out) {
    if (out) { memset(out, 0, sizeof(*out)); out->instruction = UINT32_MAX; out->exit_code = -1; }
}
static int32_t fail(cs_osdi_precision_session *s, cs_osdi_precision_process_issue *out,
                    int32_t code, int error) {
    if (s) s->invalid = 1;
    int32_t effective = code;
    if (code == CS_OSDI_PRECISION_PROCESS_WORKER && s && s->reaped
        && WIFEXITED(s->wait_status) && WEXITSTATUS(s->wait_status) == CS_OSDI_PRECISION_EXIT_ARENA)
        effective = CS_OSDI_PRECISION_PROCESS_RESOURCE;
    if (out) {
        out->code = effective; out->system_errno = error;
        if (s) {
            out->request_sequence = s->sequence;
            if (s->reaped) {
                if (WIFEXITED(s->wait_status)) out->exit_code = WEXITSTATUS(s->wait_status);
                if (WIFSIGNALED(s->wait_status)) out->signal_number = WTERMSIG(s->wait_status);
            }
        }
    }
    return effective;
}

static uint64_t now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return UINT64_MAX;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}
static uint64_t deadline(uint64_t duration) {
    uint64_t now = now_ns(); return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}
static int child_status(cs_osdi_precision_session *s) {
    if (s->reaped || s->pid <= 0) return s->reaped;
    pid_t result;
    do { result = waitpid(s->pid, &s->wait_status, WNOHANG); } while (result < 0 && errno == EINTR);
    if (result == s->pid) { s->reaped = 1; return 1; }
    return result < 0 ? -1 : 0;
}
/* Pipe EOF can become visible before waitpid publishes the child's exit status. */
static void collect_exit(cs_osdi_precision_session *s, uint64_t until) {
    while (child_status(s) == 0) {
        uint64_t now = now_ns();
        if (now >= until) return;
        uint64_t slice = until - now;
        if (slice > s->options.poll_nanoseconds) slice = s->options.poll_nanoseconds;
        struct timespec pause = {(time_t)(slice / UINT64_C(1000000000)), (long)(slice % UINT64_C(1000000000))};
        nanosleep(&pause, NULL);
    }
}
static int wait_response(cs_osdi_precision_session *s, uint64_t expected, uint64_t timeout,
                         cs_osdi_precision_cancel_probe cancelled, void *context,
                         cs_osdi_precision_process_issue *issue) {
    uint64_t until = deadline(timeout);
    for (;;) {
        if (cancelled && cancelled(context)) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CANCELLED, 0);
        if (child_status(s) < 0) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_WORKER, errno);
        uint64_t now = now_ns();
        if (now >= until) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_TIMEOUT, 0);
        uint64_t slice = until - now;
        if (slice > s->options.poll_nanoseconds) slice = s->options.poll_nanoseconds;
        uint64_t milliseconds = (slice + UINT64_C(999999)) / UINT64_C(1000000);
        int poll_ms = milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
        struct pollfd fd = {s->response_fd, POLLIN, 0};
        int ready = poll(&fd, 1, poll_ms);
        if (ready < 0) { if (errno == EINTR) continue; return fail(s, issue, CS_OSDI_PRECISION_PROCESS_IO, errno); }
        if (!ready) continue;
        if (fd.revents & POLLIN) {
            unsigned char wake;
            ssize_t count;
            do { count = read(s->response_fd, &wake, 1); } while (count < 0 && errno == EINTR);
            if (count != 1) { int error = count < 0 ? errno : 0; collect_exit(s, until); return fail(s, issue, CS_OSDI_PRECISION_PROCESS_WORKER, error); }
            uint64_t actual = __atomic_load_n(&s->control->response_sequence, __ATOMIC_ACQUIRE);
            if (actual != expected) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
            uint32_t maximum_status = s->geometry.protocol_version != CS_OSDI_PRECISION_PROTOCOL_VERSION
                ? CS_OSDI_PRECISION_NONSMOOTH : CS_OSDI_PRECISION_NUMERIC_INVARIANT;
            uint32_t status = s->control->response_status;
            int chart_status = s->geometry.protocol_version == CS_OSDI_CHART_PROTOCOL_VERSION &&
                status >= CS_OSDI_CHART_TAPE_MISMATCH && status <= CS_OSDI_CHART_STEP_LIMIT;
            int cover_status = s->geometry.protocol_version == CS_OSDI_COVER_PROTOCOL_VERSION &&
                (status == CS_OSDI_CHART_STEP_LIMIT || (status >= CS_OSDI_COVER_PREFIX_MALFORMED && status <= CS_OSDI_COVER_CAPACITY));
            if (status > maximum_status && !chart_status && !cover_status)
                return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
            if (s->control->response_status != CS_OSDI_PRECISION_SUCCESS) {
                if (issue) { issue->worker_status = s->control->response_status; issue->instruction = s->control->response_detail; }
                int32_t code = s->geometry.protocol_version != CS_OSDI_PRECISION_PROTOCOL_VERSION
                    && s->control->response_status == CS_OSDI_PRECISION_NONSMOOTH
                    ? CS_OSDI_PRECISION_PROCESS_NONSMOOTH : CS_OSDI_PRECISION_PROCESS_NUMERIC;
                return fail(s, issue, code, 0);
            }
            return CS_OSDI_PRECISION_PROCESS_SUCCESS;
        }
        if (fd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            collect_exit(s, until); return fail(s, issue, CS_OSDI_PRECISION_PROCESS_WORKER, 0);
        }
    }
}
static int add_descriptor(cs_osdi_precision_region_v1 *region,
                       uint64_t count, uint64_t stride, uint64_t *bytes) {
    if (*bytes > UINT64_MAX - 7) return 0;
    uint64_t offset = (*bytes + 7) & ~UINT64_C(7);
    if (stride && count > (UINT64_MAX - offset) / stride) return 0;
    *region = (cs_osdi_precision_region_v1){offset, count};
    *bytes = offset + count * stride; return 1;
}
static int add_region(cs_osdi_precision_control_v1 *header, unsigned region,
                       uint64_t count, uint64_t stride, uint64_t *bytes) {
    return add_descriptor(&header->regions[region], count, stride, bytes);
}
static int chart_geometry_matches(const cs_osdi_precision_session *s) {
    if (s->geometry.protocol_version == CS_OSDI_COVER_PROTOCOL_VERSION) {
        const unsigned char *actual=s->mapping,*fixed=(const void *)&s->cover_geometry;
        return !memcmp(actual+sizeof(s->geometry),fixed+sizeof(s->geometry),offsetof(cs_osdi_cover_control_v4,node_count)-sizeof(s->geometry));
    }
    if (s->geometry.protocol_version != CS_OSDI_CHART_PROTOCOL_VERSION) return 1;
    const unsigned char *actual = s->mapping, *fixed = (const void *)&s->chart_geometry;
    return !memcmp(actual + sizeof(s->geometry), fixed + sizeof(s->geometry),
        offsetof(cs_osdi_chart_control_v3, chart_mode) - sizeof(s->geometry));
}
static void *region(cs_osdi_precision_session *s, unsigned index) {
    return (unsigned char *)s->mapping + s->geometry.regions[index].byte_offset;
}
/* Called only after the worker has validated the complete immutable program. */
static int retain_time_input_fact(cs_osdi_precision_session *s) {
    uint64_t bytes = s->geometry.regions[0].element_count;
    if (bytes < sizeof(CSOsdiPrecisionProgramHeader)) return 0;
    CSOsdiPrecisionProgramHeader header;
    memcpy(&header, region(s, 0), sizeof(header));
    CSOsdiPrecisionSection inputs = header.inputs;
    if (inputs.stride != sizeof(CSOsdiPrecisionInput) || inputs.offset > bytes
        || inputs.count > (bytes - inputs.offset) / inputs.stride) return 0;
    if (s->geometry.protocol_version == CS_OSDI_CHART_PROTOCOL_VERSION || s->geometry.protocol_version == CS_OSDI_COVER_PROTOCOL_VERSION) {
        CSOsdiPrecisionSection constants = header.constants;
        if (constants.stride != sizeof(CSOsdiPrecisionConstant) || constants.offset > bytes ||
            constants.count > (bytes - constants.offset) / constants.stride) return 0;
        s->chart_constants = (unsigned char *)region(s, 0) + constants.offset;
        s->chart_constant_count = constants.count;
    }
    for (uint32_t i = 0; i < inputs.count; ++i) {
        CSOsdiPrecisionInput input;
        memcpy(&input, (unsigned char *)region(s, 0) + inputs.offset + (uint64_t)i * inputs.stride, sizeof(input));
        if (input.kind == CS_OSDI_PRECISION_TIME) s->program_has_time_input = 1;
    }
    return 1;
}
static int valid_chart_operand(const cs_osdi_precision_session *s, uint32_t kind,
                               uint32_t payload, double lower, double upper) {
    if (kind <= 4) return isfinite(lower) && isfinite(upper) && lower <= upper;
    if (kind != CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY &&
        kind != CS_OSDI_CHART_POSITIVE_PRIMITIVE_INFINITY) return 0;
    if (payload >= s->chart_constant_count) return 0;
    CSOsdiPrecisionConstant primitive;
    memcpy(&primitive, s->chart_constants + (size_t)payload * sizeof(primitive), sizeof(primitive));
    uint64_t expected = kind == CS_OSDI_CHART_NEGATIVE_PRIMITIVE_INFINITY ?
        UINT64_C(0xfff0000000000000) : UINT64_C(0x7ff0000000000000);
    double value; memcpy(&value, &expected, sizeof(value));
    return primitive.type == 1 && primitive.bits == expected && lower == value && upper == value;
}
static int safe_fd(int fd) { return fcntl(fd, F_DUPFD_CLOEXEC, 6); }
static int make_pipe(int fds[2]) {
    if (pipe(fds)) return -1;
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) || fcntl(fds[1], F_SETFD, FD_CLOEXEC)) {
        int error = errno; close(fds[0]); close(fds[1]); fds[0] = -1; fds[1] = -1; errno = error; return -1;
    }
    return 0;
}
static int create_mapping_fd(void) {
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        char name[96];
        uint64_t random; arc4random_buf(&random, sizeof(random));
        snprintf(name, sizeof(name), "/op.%x.%llx", (unsigned)getpid(), (unsigned long long)random);
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC)) { int error = errno; shm_unlink(name); close(fd); errno = error; return -1; }
            if (shm_unlink(name)) { int error = errno; close(fd); errno = error; return -1; }
            return fd;
        }
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST; return -1;
}

int32_t cs_osdi_precision_process_close(cs_osdi_precision_session **owner,
                                        cs_osdi_precision_process_issue *issue) {
    clear_issue(issue);
    if (!owner || !*owner) return CS_OSDI_PRECISION_PROCESS_SUCCESS;
    cs_osdi_precision_session *s = *owner; s->invalid = 1;
    if (s->pid > 0 && !s->reaped) {
        int status = child_status(s);
        if (status < 0) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CLEANUP, errno);
        int kill_error = 0;
        if (!s->reaped && kill(-s->pid, SIGKILL) && errno != ESRCH) kill_error = errno;
        uint64_t until = deadline(s->options.cleanup_timeout_nanoseconds);
        while (!s->reaped) {
            status = child_status(s);
            if (status < 0) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CLEANUP, errno);
            if (s->reaped) break;
            uint64_t now = now_ns();
            if (now >= until) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CLEANUP, kill_error ? kill_error : ETIMEDOUT);
            uint64_t slice = until - now;
            if (slice > s->options.poll_nanoseconds) slice = s->options.poll_nanoseconds;
            struct timespec pause = {(time_t)(slice / UINT64_C(1000000000)), (long)(slice % UINT64_C(1000000000))};
            nanosleep(&pause, NULL);
        }
    }
    if (s->request_fd >= 0) close(s->request_fd);
    if (s->response_fd >= 0) close(s->response_fd);
    if (s->mapping && s->mapping != MAP_FAILED) munmap(s->mapping, s->mapping_bytes);
    free(s->cover_node_visits); free(s->cover_leaf_visits);
    free(s); *owner = NULL;
    return CS_OSDI_PRECISION_PROCESS_SUCCESS;
}

static int32_t process_create(uint32_t operation, const cs_osdi_chart_limits_v3 *chart_limits,
    const cs_osdi_cover_limits_v4 *cover_limits, const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue) {
    clear_issue(issue); if (out) *out = NULL;
    if (operation == CS_OSDI_CHART_PROTOCOL_VERSION && (!chart_limits ||
        !chart_limits->maximum_steps || !chart_limits->maximum_tape_entries ||
        !chart_limits->maximum_guard_records))
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    if (operation == CS_OSDI_COVER_PROTOCOL_VERSION && (!cover_limits || cover_limits->reserved ||
        !cover_limits->maximum_steps || !cover_limits->maximum_guard_records || !cover_limits->maximum_copied_bytes ||
        !cover_limits->maximum_cells || !cover_limits->maximum_nodes || cover_limits->maximum_nodes==UINT32_MAX ||
        !cover_limits->maximum_leaves || !cover_limits->maximum_pending || !cover_limits->maximum_depth ||
        !cover_limits->maximum_integer_bits || cover_limits->maximum_integer_bits>CS_OSDI_COVER_CELL_BITS))
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    if (!out || !options || !worker_path || !*worker_path || !program || !program_bytes || !setup || !setup_bytes
        || !raw_node_count || active_count > raw_node_count || !options->maximum_mapping_bytes
        || !options->arena_bytes || !options->maximum_work_units || options->minimum_precision_bits < 53
        || options->minimum_precision_bits > options->maximum_precision_bits
        || !options->startup_timeout_nanoseconds || !options->evaluation_timeout_nanoseconds
        || !options->cleanup_timeout_nanoseconds || !options->poll_nanoseconds)
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
#if !defined(__APPLE__) || !defined(__aarch64__)
    return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_UNSUPPORTED, 0);
#else
    cs_osdi_precision_control_v1 header = {0};
    cs_osdi_chart_control_v3 chart_header = {0};
    cs_osdi_cover_control_v4 cover_header = {0};
    header.magic = CS_OSDI_PRECISION_MAGIC; header.protocol_version = operation;
    header.header_bytes = operation == CS_OSDI_COVER_PROTOCOL_VERSION ? sizeof(cover_header) : operation == CS_OSDI_CHART_PROTOCOL_VERSION ? sizeof(chart_header) : sizeof(header); header.byte_order = CS_OSDI_PRECISION_BYTE_ORDER;
    header.descriptor_index = descriptor_index; header.active_count = active_count; header.raw_node_count = raw_node_count;
    header.arena_bytes = options->arena_bytes; header.minimum_precision_bits = options->minimum_precision_bits;
    header.maximum_precision_bits = options->maximum_precision_bits; header.maximum_work_units = options->maximum_work_units;
    header.requested_precision_bits = options->maximum_precision_bits;
    header.response_sequence = CS_OSDI_PRECISION_INITIAL_RESPONSE; header.response_detail = UINT32_MAX;
    memcpy(header.program_sha256, options->program_sha256, 32); memcpy(header.worker_profile_sha256, options->worker_profile_sha256, 32);
    uint64_t bytes = header.header_bytes, matrix = (uint64_t)raw_node_count * raw_node_count;
    if (!add_region(&header, 0, program_bytes, 1, &bytes) || !add_region(&header, 1, setup_bytes, 1, &bytes))
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, 0);
    for (unsigned i = 2; i < 14; ++i) {
        unsigned matrix_start = operation != CS_OSDI_PRECISION_PROTOCOL_VERSION ? 10 : 12;
        uint64_t count = i < 6 ? active_count : i < matrix_start ? raw_node_count : matrix;
        if (!add_region(&header, i, count, sizeof(double), &bytes)) return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, 0);
    }
    if (operation == CS_OSDI_CHART_PROTOCOL_VERSION) {
        chart_header.limits = *chart_limits;
        if (!add_descriptor(&chart_header.tape_region, chart_limits->maximum_tape_entries,
                sizeof(cs_osdi_chart_decision_v3), &bytes) ||
            !add_descriptor(&chart_header.guard_region, chart_limits->maximum_guard_records,
                sizeof(cs_osdi_chart_guard_v3), &bytes))
            return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, 0);
    }
    if(operation==CS_OSDI_COVER_PROTOCOL_VERSION){
        uint64_t vector=4*(uint64_t)raw_node_count;
        if(matrix>(UINT64_MAX-vector)/4)return fail(NULL,issue,CS_OSDI_PRECISION_PROCESS_RESOURCE,0);
        uint64_t stride=vector+4*matrix;
        if(cover_limits->maximum_leaves>UINT64_MAX/stride)return fail(NULL,issue,CS_OSDI_PRECISION_PROCESS_RESOURCE,0);
        cover_header.limits=*cover_limits;
        if(!add_descriptor(&cover_header.tree_region,cover_limits->maximum_nodes,sizeof(cs_osdi_cover_node_v4),&bytes) ||
            !add_descriptor(&cover_header.leaf_region,stride*cover_limits->maximum_leaves,sizeof(double),&bytes))
            return fail(NULL,issue,CS_OSDI_PRECISION_PROCESS_RESOURCE,0);
    }
    /* Darwin POSIX shared memory rounds its object size to the host page size. */
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || bytes > UINT64_MAX - ((uint64_t)page - 1))
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, 0);
    bytes = ((bytes + (uint64_t)page - 1) / (uint64_t)page) * (uint64_t)page;
    if (bytes > options->maximum_mapping_bytes || bytes > SIZE_MAX || bytes > INT64_MAX)
        return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, 0);
    header.mapping_bytes = bytes; chart_header.base = header; cover_header.base = header;
    cs_osdi_precision_session *s = calloc(1, sizeof(*s));
    if (!s) return fail(NULL, issue, CS_OSDI_PRECISION_PROCESS_RESOURCE, errno);
    s->pid = -1; s->request_fd = -1; s->response_fd = -1; s->options = *options; s->geometry = header; s->mapping_bytes = (size_t)bytes; s->chart_geometry = chart_header; s->cover_geometry=cover_header;
    int shared = -1, request[2] = {-1,-1}, response[2] = {-1,-1}, spawnfds[3] = {-1,-1,-1};
    int error = 0, result = CS_OSDI_PRECISION_PROCESS_IO;
    posix_spawn_file_actions_t actions; posix_spawnattr_t attributes;
    int have_actions = 0, have_attributes = 0;
    if(operation==CS_OSDI_COVER_PROTOCOL_VERSION){
        /* The admitted tree/leaf mapping products dominate these smaller visit
         * arrays; calloc still receives explicit checked byte counts. */
        uint64_t nb=(uint64_t)cover_limits->maximum_nodes*sizeof(uint32_t),lb=(uint64_t)cover_limits->maximum_leaves*sizeof(uint32_t);
        if(nb>SIZE_MAX || lb>SIZE_MAX){result=CS_OSDI_PRECISION_PROCESS_RESOURCE;goto failed;}
        s->cover_node_visits=calloc(1,(size_t)nb);s->cover_leaf_visits=calloc(1,(size_t)lb);
        if(!s->cover_node_visits || !s->cover_leaf_visits){result=CS_OSDI_PRECISION_PROCESS_RESOURCE;goto failed;}
    }
    shared = create_mapping_fd();
    if (shared < 0 || ftruncate(shared, (off_t)bytes)) goto failed;
    s->mapping = mmap(NULL, (size_t)bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shared, 0);
    if (s->mapping == MAP_FAILED) goto failed;
    s->control = s->mapping;
    if (!__atomic_is_lock_free(sizeof(uint64_t), &s->control->response_sequence)) { result = CS_OSDI_PRECISION_PROCESS_UNSUPPORTED; goto failed; }
    memset(s->mapping, 0, (size_t)bytes); memcpy(s->control, operation == CS_OSDI_COVER_PROTOCOL_VERSION ? (const void *)&cover_header : operation == CS_OSDI_CHART_PROTOCOL_VERSION ? (const void *)&chart_header : (const void *)&header, header.header_bytes);
    memcpy(region(s, 0), program, (size_t)program_bytes); memcpy(region(s, 1), setup, (size_t)setup_bytes);
    if (make_pipe(request) || make_pipe(response)) goto failed;
    if (fcntl(request[1], F_SETNOSIGPIPE, 1)) goto failed;
    spawnfds[0] = safe_fd(shared); spawnfds[1] = safe_fd(request[0]); spawnfds[2] = safe_fd(response[1]);
    if (spawnfds[0] < 0 || spawnfds[1] < 0 || spawnfds[2] < 0) goto failed;
    error = posix_spawn_file_actions_init(&actions); if (error) goto failed; have_actions = 1;
    error = posix_spawnattr_init(&attributes); if (error) goto failed; have_attributes = 1;
    for (unsigned i = 0; i < 3; ++i) { error = posix_spawn_file_actions_adddup2(&actions, spawnfds[i], (int)i + 3); if (error) goto failed; }
    error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0); if (error) goto failed;
    error = posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0); if (error) goto failed;
    error = posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0); if (error) goto failed;
    error = posix_spawnattr_setpgroup(&attributes, 0); if (error) goto failed;
    error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT); if (error) goto failed;
    char *argv[] = {(char *)worker_path, NULL}; char *environment[] = {"LANG=C", "LC_ALL=C", "TZ=UTC", NULL};
    error = posix_spawn(&s->pid, worker_path, &actions, &attributes, argv, environment); if (error) goto failed;
    for (unsigned i = 0; i < 3; ++i) { close(spawnfds[i]); spawnfds[i] = -1; }
    close(shared); shared = -1; close(request[0]); request[0] = -1; close(response[1]); response[1] = -1;
    s->request_fd = request[1]; request[1] = -1; s->response_fd = response[0]; response[0] = -1;
    posix_spawn_file_actions_destroy(&actions); have_actions = 0;
    posix_spawnattr_destroy(&attributes); have_attributes = 0;
    result = wait_response(s, 0, options->startup_timeout_nanoseconds, cancelled, cancel_context, issue);
    if (result) goto failed;
    if (memcmp(s->control, &s->geometry, offsetof(cs_osdi_precision_control_v1, request_sequence))
        || !chart_geometry_matches(s)
        || __atomic_load_n(&s->control->request_sequence, __ATOMIC_ACQUIRE) != 0) {
        result = fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0); goto failed;
    }
    if (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION && !retain_time_input_fact(s)) {
        result = fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0); goto failed;
    }
    s->completed_work = s->control->completed_work_units;
    *out = s; return 0;
failed:
    if (!error) error = errno;
    if (have_actions) posix_spawn_file_actions_destroy(&actions);
    if (have_attributes) posix_spawnattr_destroy(&attributes);
    if (shared >= 0) close(shared);
    for (unsigned i = 0; i < 2; ++i) { if (request[i] >= 0) close(request[i]); if (response[i] >= 0) close(response[i]); }
    for (unsigned i = 0; i < 3; ++i) if (spawnfds[i] >= 0) close(spawnfds[i]);
    if (!issue || !issue->code) fail(s, issue, result, error);
    cs_osdi_precision_process_issue cleanup;
    int32_t closed = cs_osdi_precision_process_close(&s, &cleanup);
    if (closed) *out = s;
    return issue ? issue->code : result;
#endif
}

int32_t cs_osdi_precision_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue) {
    return process_create(CS_OSDI_PRECISION_PROTOCOL_VERSION, NULL, NULL, worker_path, program, program_bytes,
        setup, setup_bytes, descriptor_index, active_count, raw_node_count, options,
        cancelled, cancel_context, out, issue);
}

int32_t cs_osdi_range_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue) {
    return process_create(CS_OSDI_RANGE_PROTOCOL_VERSION, NULL, NULL, worker_path, program, program_bytes,
        setup, setup_bytes, descriptor_index, active_count, raw_node_count, options,
        cancelled, cancel_context, out, issue);
}

int32_t cs_osdi_chart_process_create(const char *worker_path,
    const unsigned char *program, uint64_t program_bytes,
    const unsigned char *setup, uint64_t setup_bytes,
    uint32_t descriptor_index, uint32_t active_count, uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options, const cs_osdi_chart_limits_v3 *limits,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_session **out, cs_osdi_precision_process_issue *issue) {
    return process_create(CS_OSDI_CHART_PROTOCOL_VERSION, limits, NULL, worker_path, program, program_bytes,
        setup, setup_bytes, descriptor_index, active_count, raw_node_count, options,
        cancelled, cancel_context, out, issue);
}

static int cover_span_disjoint(const void *a,size_t an,const void *b,size_t bn){
    if((an && !a) || (bn && !b) || an>UINTPTR_MAX-(uintptr_t)a || bn>UINTPTR_MAX-(uintptr_t)b)return 0;
    return !an || !bn || (uintptr_t)a+an<=(uintptr_t)b || (uintptr_t)b+bn<=(uintptr_t)a;
}
int32_t cs_osdi_cover_process_create(const char *worker_path,
    const unsigned char *program,uint64_t program_bytes,const unsigned char *setup,uint64_t setup_bytes,
    uint32_t descriptor_index,uint32_t active_count,uint32_t raw_node_count,
    const cs_osdi_precision_process_options *options,const cs_osdi_cover_limits_v4 *limits,
    cs_osdi_precision_cancel_probe cancelled,void *cancel_context,cs_osdi_precision_session **out,cs_osdi_precision_process_issue *issue){
    if(program_bytes>SIZE_MAX || setup_bytes>SIZE_MAX)return fail(NULL,NULL,CS_OSDI_PRECISION_PROCESS_INVALID,0);
    const void *metadata[]={out,issue};size_t lengths[]={out?sizeof(*out):0,issue?sizeof(*issue):0};
    const void *inputs[]={program,setup,options,limits,worker_path};
    size_t input_bytes[]={(size_t)program_bytes,(size_t)setup_bytes,options?sizeof(*options):0,limits?sizeof(*limits):0,worker_path?strlen(worker_path)+1:0};
    if((out && (uintptr_t)out%_Alignof(cs_osdi_precision_session *)) ||
        (issue && (uintptr_t)issue%_Alignof(cs_osdi_precision_process_issue)) ||
        !cover_span_disjoint(out,lengths[0],issue,lengths[1]))return fail(NULL,NULL,CS_OSDI_PRECISION_PROCESS_INVALID,0);
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<5;++j)
        if(!cover_span_disjoint(metadata[i],lengths[i],inputs[j],input_bytes[j]))return fail(NULL,NULL,CS_OSDI_PRECISION_PROCESS_INVALID,0);
    return process_create(CS_OSDI_COVER_PROTOCOL_VERSION,NULL,limits,worker_path,program,program_bytes,setup,setup_bytes,
        descriptor_index,active_count,raw_node_count,options,cancelled,cancel_context,out,issue);
}

static int normalized(double high, double low) { return isfinite(high) && isfinite(low) && high + low == high; }
static int ordered(double lower_high, double lower_low, double upper_high, double upper_low) {
    return lower_high < upper_high || (lower_high == upper_high && lower_low <= upper_low);
}
static int valid_context(const cs_osdi_precision_context_v1 *context) {
    if (!context || context->reserved || !normalized(context->time_high, context->time_low)) return 0;
    if (context->mode == CS_OSDI_PRECISION_TRANSIENT_INITIALIZATION) return context->time_high == 0 && context->time_low == 0;
    return context->mode == CS_OSDI_PRECISION_TRANSIENT && context->time_high >= 0 && !(context->time_high == 0 && context->time_low < 0);
}
static int cover_metadata_valid(cs_osdi_precision_session *s,void *out,cs_osdi_precision_process_issue *issue,
    const double *const inputs[4],uint64_t count,const cs_osdi_precision_context_v1 *lower,const cs_osdi_precision_context_v1 *upper){
    if(!out || (uintptr_t)out%_Alignof(cs_osdi_cover_result_view) ||
        (issue && (uintptr_t)issue%_Alignof(cs_osdi_precision_process_issue)) || count>SIZE_MAX/sizeof(double))return 0;
    const void *metadata[]={out,issue};size_t lengths[]={sizeof(cs_osdi_cover_result_view),issue?sizeof(*issue):0};
    if(!cover_span_disjoint(out,lengths[0],issue,lengths[1]))return 0;
    for(unsigned j=0;j<2;++j){
        if(!cover_span_disjoint(metadata[j],lengths[j],s,s?sizeof(*s):0) ||
            !cover_span_disjoint(metadata[j],lengths[j],s?s->mapping:NULL,s?s->mapping_bytes:0) ||
            !cover_span_disjoint(metadata[j],lengths[j],lower,lower?sizeof(*lower):0) ||
            !cover_span_disjoint(metadata[j],lengths[j],upper,upper?sizeof(*upper):0))return 0;
        for(unsigned i=0;i<4;++i)if(!cover_span_disjoint(metadata[j],lengths[j],inputs[i],(size_t)count*sizeof(double)))return 0;
    }
    if(s && s->geometry.protocol_version==CS_OSDI_COVER_PROTOCOL_VERSION){
        const void *pools[]={s->cover_node_visits,s->cover_leaf_visits};
        size_t pool_bytes[]={(size_t)s->cover_geometry.limits.maximum_nodes*sizeof(uint32_t),(size_t)s->cover_geometry.limits.maximum_leaves*sizeof(uint32_t)};
        for(unsigned j=0;j<2;++j){
            for(unsigned k=0;k<2;++k)if(!cover_span_disjoint(metadata[k],lengths[k],pools[j],pool_bytes[j]))return 0;
            for(unsigned i=0;i<4;++i)if(!cover_span_disjoint(inputs[i],(size_t)count*sizeof(double),pools[j],pool_bytes[j]))return 0;
        }
    }
    for(unsigned i=0;i<4;++i)if(!cover_span_disjoint(inputs[i],(size_t)count*sizeof(double),s?s->mapping:NULL,s?s->mapping_bytes:0))return 0;
    return 1;
}
static int32_t process_evaluate(uint32_t operation, uint32_t chart_mode,
    const cs_osdi_chart_decision_v3 *tape, uint64_t tape_count, cs_osdi_precision_session *s,
    const double *candidate_high, const double *candidate_low,
    const double *reference_high, const double *reference_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *candidate, const cs_osdi_precision_context_v1 *reference,
    uint32_t precision_bits, cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    void *out, cs_osdi_precision_process_issue *issue) {
    const double *original_inputs[]={candidate_high,candidate_low,reference_high,reference_low};
    if(operation==CS_OSDI_COVER_PROTOCOL_VERSION && !cover_metadata_valid(s,out,issue,original_inputs,active_count,candidate,reference))
        return fail(s,NULL,CS_OSDI_PRECISION_PROCESS_INVALID,0);
    clear_issue(issue);
    if (out) memset(out, 0, operation == CS_OSDI_COVER_PROTOCOL_VERSION ? sizeof(cs_osdi_cover_result_view) : operation == CS_OSDI_CHART_PROTOCOL_VERSION ? sizeof(cs_osdi_chart_result_view) :
        operation == CS_OSDI_RANGE_PROTOCOL_VERSION ? sizeof(cs_osdi_range_result_view) : sizeof(cs_osdi_precision_result_view));
    if (!s || s->invalid) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CLOSED, 0);
    if (s->geometry.protocol_version != operation)
        return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    if (!out || active_count != s->geometry.active_count || (active_count && (!candidate_high || !candidate_low || !reference_high || !reference_low))
        || !valid_context(candidate) || !valid_context(reference) || precision_bits < s->options.minimum_precision_bits
        || precision_bits > s->options.maximum_precision_bits || s->sequence >= CS_OSDI_PRECISION_MAX_SEQUENCE)
        return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    if (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION && (candidate->mode != reference->mode
        || !ordered(candidate->time_high, candidate->time_low, reference->time_high, reference->time_low)))
        return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    for (uint64_t i = 0; i < active_count; ++i)
        if (!normalized(candidate_high[i], candidate_low[i]) || !normalized(reference_high[i], reference_low[i])
            || (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION
                && !ordered(candidate_high[i], candidate_low[i], reference_high[i], reference_low[i])))
            return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
    if (operation == CS_OSDI_CHART_PROTOCOL_VERSION) {
        if (chart_mode < CS_OSDI_CHART_RECORD || chart_mode > CS_OSDI_CHART_FORMULA_CLOSURE ||
            tape_count > s->chart_geometry.limits.maximum_tape_entries || (tape_count && !tape) ||
            (chart_mode == CS_OSDI_CHART_RECORD && tape_count))
            return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
        for (uint64_t i = 0; i < tape_count; ++i)
            if (tape[i].reserved || tape[i].stage < 1 || tape[i].stage > 3 ||
                tape[i].kind < 1 || tape[i].kind > 3 || tape[i].choice > 1 || tape[i].ordinal != i)
                return fail(s, issue, CS_OSDI_PRECISION_PROCESS_INVALID, 0);
        cs_osdi_chart_control_v3 *chart = s->mapping;
        if (tape_count) memmove((unsigned char *)s->mapping + s->chart_geometry.tape_region.byte_offset,
            tape, (size_t)tape_count * sizeof(*tape));
        chart->chart_mode = chart_mode; chart->reserved = 0; chart->tape_count = tape_count;
    }
    uint64_t cover_until=operation==CS_OSDI_COVER_PROTOCOL_VERSION?deadline(s->options.evaluation_timeout_nanoseconds):0;
    const double *inputs[] = {candidate_high,candidate_low,reference_high,reference_low};
    for (unsigned i = 0; i < 4; ++i) if (active_count) memcpy(region(s, i + 2), inputs[i], (size_t)active_count * sizeof(double));
    s->control->candidate = *candidate; s->control->reference = *reference;
    s->control->requested_precision_bits = precision_bits; s->sequence += 1;
    __atomic_store_n(&s->control->request_sequence, s->sequence, __ATOMIC_RELEASE);
    unsigned char wake = 1; ssize_t count;
    do { count = write(s->request_fd, &wake, 1); } while (count < 0 && errno == EINTR);
    if (count != 1) { child_status(s); return fail(s, issue, CS_OSDI_PRECISION_PROCESS_WORKER, errno); }
    int32_t result = wait_response(s, s->sequence, s->options.evaluation_timeout_nanoseconds, cancelled, cancel_context, issue);
    if (result) return result;
    cs_osdi_precision_control_v1 *h = s->control;
    const cs_osdi_precision_control_v1 *fixed = &s->geometry;
    if (h->magic != fixed->magic || h->protocol_version != fixed->protocol_version || h->header_bytes != fixed->header_bytes
        || h->mapping_bytes != fixed->mapping_bytes || h->byte_order != fixed->byte_order || h->descriptor_index != fixed->descriptor_index
        || h->active_count != fixed->active_count || h->raw_node_count != fixed->raw_node_count || h->arena_bytes != fixed->arena_bytes
        || h->minimum_precision_bits != fixed->minimum_precision_bits || h->maximum_precision_bits != fixed->maximum_precision_bits
        || h->maximum_work_units != fixed->maximum_work_units || memcmp(h->regions, fixed->regions, sizeof(h->regions))
        || memcmp(h->program_sha256, fixed->program_sha256, 32) || memcmp(h->worker_profile_sha256, fixed->worker_profile_sha256, 32)
        || (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION && h->requested_precision_bits != precision_bits)
        || !chart_geometry_matches(s)
        || __atomic_load_n(&h->request_sequence, __ATOMIC_ACQUIRE) != s->sequence
        || memcmp(&h->candidate, candidate, sizeof(*candidate)) || memcmp(&h->reference, reference, sizeof(*reference)))
        return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
    if (h->model_flags || h->used_precision_bits < precision_bits || h->used_precision_bits > s->options.maximum_precision_bits
        || h->completed_work_units > s->options.maximum_work_units || h->has_maximum_timestep > 1
        || (h->has_maximum_timestep && (!isfinite(h->maximum_timestep) || h->maximum_timestep <= 0)))
        return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
    uint64_t nodes = h->raw_node_count;
    for (unsigned i = 6; operation != CS_OSDI_COVER_PROTOCOL_VERSION && i < 14; ++i) {
        const double *values = region(s, i);
        unsigned matrix_start = operation != CS_OSDI_PRECISION_PROTOCOL_VERSION ? 10 : 12;
        uint64_t n = i < matrix_start ? nodes : nodes * nodes;
        for (uint64_t j = 0; j < n; ++j)
            if (!isfinite(values[j]) || (operation == CS_OSDI_PRECISION_PROTOCOL_VERSION
                && (i == 8 || i == 11) && values[j] < 0))
                return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
    }
    if (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION && operation != CS_OSDI_COVER_PROTOCOL_VERSION) {
        for (unsigned i = 6; i < 14; i += 2) {
            const double *lower = region(s, i), *upper = region(s, i + 1);
            uint64_t count = i < 10 ? nodes : nodes * nodes;
            for (uint64_t j = 0; j < count; ++j)
                if (lower[j] > upper[j]) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
        }
    } else if(operation == CS_OSDI_PRECISION_PROTOCOL_VERSION) {
        for (uint64_t j = 0; j < nodes; ++j)
            if (!normalized(((double *)region(s, 6))[j], ((double *)region(s, 7))[j])
                || !normalized(((double *)region(s, 9))[j], ((double *)region(s, 10))[j]))
                return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
    }
    if (operation == CS_OSDI_CHART_PROTOCOL_VERSION) {
        const cs_osdi_chart_control_v3 *chart = s->mapping;
        if (chart->chart_mode != chart_mode || chart->reserved || chart->tape_count != tape_count ||
            chart->guard_count > s->chart_geometry.limits.maximum_guard_records ||
            (chart_mode != CS_OSDI_CHART_RECORD && chart->guard_count != tape_count) ||
            chart->completed_steps <= s->completed_steps ||
            chart->completed_steps > s->chart_geometry.limits.maximum_steps ||
            h->completed_work_units < s->completed_work ||
            (tape_count && memcmp((unsigned char *)s->mapping + s->chart_geometry.tape_region.byte_offset,
                tape, (size_t)tape_count * sizeof(*tape))))
            return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
        const cs_osdi_chart_guard_v3 *guards = (const void *)((unsigned char *)s->mapping +
            s->chart_geometry.guard_region.byte_offset);
        for (uint64_t i = 0; i < chart->guard_count; ++i) {
            const cs_osdi_chart_guard_v3 *g = &guards[i];
            if (g->reserved || g->decision.reserved || g->decision.stage < 1 || g->decision.stage > 3 ||
                g->decision.kind < 1 || g->decision.kind > 3 || g->decision.choice > 1 || g->decision.ordinal != i ||
                g->formula_truth_known > 1 || g->boundary_extension > 1 ||
                (chart_mode == CS_OSDI_CHART_RECORD && !g->formula_truth_known) ||
                (chart_mode != CS_OSDI_CHART_FORMULA_CLOSURE && g->boundary_extension) ||
                g->a_type < 1 || !valid_chart_operand(s, g->a_type, g->a_integer, g->a_lower, g->a_upper) ||
                !valid_chart_operand(s, g->b_type, g->b_integer, g->b_lower, g->b_upper) ||
                (chart_mode != CS_OSDI_CHART_RECORD && memcmp(&g->decision, &tape[i], sizeof(*tape))))
                return fail(s, issue, CS_OSDI_PRECISION_PROCESS_PROTOCOL, 0);
        }
        s->completed_steps = chart->completed_steps; s->completed_work = h->completed_work_units;
    }
    if(operation==CS_OSDI_COVER_PROTOCOL_VERSION){
        const cs_osdi_cover_control_v4 *cover=s->mapping;const cs_osdi_cover_limits_v4 *limits=&s->cover_geometry.limits;
        if(h->has_maximum_timestep || h->completed_work_units<s->completed_work ||
            cover->completed_steps<=s->completed_steps || cover->completed_steps>limits->maximum_steps ||
            cover->completed_guard_records<s->completed_guard_records || cover->completed_guard_records>limits->maximum_guard_records ||
            cover->completed_copied_bytes<=s->completed_copied_bytes || cover->completed_copied_bytes>limits->maximum_copied_bytes ||
            h->completed_work_units-s->completed_work<cover->completed_copied_bytes-s->completed_copied_bytes)
            return fail(s,issue,CS_OSDI_PRECISION_PROCESS_PROTOCOL,0);
        for(unsigned i=0;i<4;++i)if(active_count && memcmp(region(s,i+2),inputs[i],(size_t)active_count*sizeof(double)))
            return fail(s,issue,CS_OSDI_PRECISION_PROCESS_PROTOCOL,0);
        const cs_osdi_cover_node_v4 *tree=(const void *)((unsigned char *)s->mapping+s->cover_geometry.tree_region.byte_offset);
        const double *leaves=(const void *)((unsigned char *)s->mapping+s->cover_geometry.leaf_region.byte_offset);
        int checked=cs_osdi_cover_wire_check(cover,region(s,0),s->geometry.regions[0].element_count,tree,leaves,
            s->cover_node_visits,s->cover_leaf_visits,s->completed_guard_records,s->completed_copied_bytes,cover_until,cancelled,cancel_context);
        if(checked)return fail(s,issue,checked,0);
        s->completed_steps=cover->completed_steps;s->completed_work=h->completed_work_units;
        s->completed_guard_records=cover->completed_guard_records;s->completed_copied_bytes=cover->completed_copied_bytes;
        cs_osdi_cover_result_view *view=out;
        *view=(cs_osdi_cover_result_view){.raw_node_count=nodes,.active_count=active_count,.plane_stride=4*nodes+4*nodes*nodes,
            .nodes=tree,.leaf_planes=leaves,.lower_high=region(s,2),.lower_low=region(s,3),.upper_high=region(s,4),.upper_low=region(s,5),
            .node_count=cover->node_count,.leaf_count=cover->leaf_count,.used_precision_bits=h->used_precision_bits,
            .program_has_time_input=s->program_has_time_input,.lower_context=*candidate,.upper_context=*reference,
            .completed_work_units=h->completed_work_units,.completed_steps=cover->completed_steps,
            .completed_guard_records=cover->completed_guard_records,.completed_copied_bytes=cover->completed_copied_bytes,.request_sequence=s->sequence};
        return 0;
    }
    if (cancelled && cancelled(cancel_context)) return fail(s, issue, CS_OSDI_PRECISION_PROCESS_CANCELLED, 0);
    if (operation != CS_OSDI_PRECISION_PROTOCOL_VERSION) {
        *(cs_osdi_range_result_view *)out = (cs_osdi_range_result_view){
            .raw_node_count = nodes, .f_lower = region(s,6), .f_upper = region(s,7),
            .q_lower = region(s,8), .q_upper = region(s,9), .g_lower = region(s,10),
            .g_upper = region(s,11), .c_lower = region(s,12), .c_upper = region(s,13),
            .used_precision_bits = h->used_precision_bits, .has_maximum_timestep = h->has_maximum_timestep,
            .maximum_timestep = h->maximum_timestep, .completed_work_units = h->completed_work_units,
            .request_sequence = s->sequence, .program_has_time_input = s->program_has_time_input};
    } else {
        *(cs_osdi_precision_result_view *)out = (cs_osdi_precision_result_view){nodes, region(s,6), region(s,7), region(s,8), region(s,9), region(s,10), region(s,11),
            region(s,12), region(s,13), h->used_precision_bits, h->has_maximum_timestep, h->maximum_timestep, h->completed_work_units, s->sequence};
    }
    if (operation == CS_OSDI_CHART_PROTOCOL_VERSION) {
        const cs_osdi_chart_control_v3 *chart = s->mapping;
        cs_osdi_chart_result_view *view = out;
        view->guards = (const void *)((unsigned char *)s->mapping + s->chart_geometry.guard_region.byte_offset);
        view->guard_count = chart->guard_count; view->completed_steps = chart->completed_steps;
        view->semantics = chart_mode;
    }
    return 0;
}

int32_t cs_osdi_precision_process_evaluate(cs_osdi_precision_session *s,
    const double *candidate_high, const double *candidate_low,
    const double *reference_high, const double *reference_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *candidate, const cs_osdi_precision_context_v1 *reference,
    uint32_t precision_bits, cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_precision_result_view *out, cs_osdi_precision_process_issue *issue) {
    return process_evaluate(CS_OSDI_PRECISION_PROTOCOL_VERSION, 0, NULL, 0, s, candidate_high, candidate_low,
        reference_high, reference_low, active_count, candidate, reference, precision_bits,
        cancelled, cancel_context, out, issue);
}

int32_t cs_osdi_range_process_evaluate(cs_osdi_precision_session *s,
    const double *lower_high, const double *lower_low,
    const double *upper_high, const double *upper_low, uint64_t active_count,
    const cs_osdi_precision_context_v1 *lower_context, const cs_osdi_precision_context_v1 *upper_context,
    uint32_t precision_bits, cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_range_result_view *out, cs_osdi_precision_process_issue *issue) {
    return process_evaluate(CS_OSDI_RANGE_PROTOCOL_VERSION, 0, NULL, 0, s, lower_high, lower_low,
        upper_high, upper_low, active_count, lower_context, upper_context, precision_bits,
        cancelled, cancel_context, out, issue);
}

int32_t cs_osdi_chart_process_evaluate(cs_osdi_precision_session *s,
    const double *lower_high, const double *lower_low, const double *upper_high, const double *upper_low,
    uint64_t active_count, const cs_osdi_precision_context_v1 *lower_context,
    const cs_osdi_precision_context_v1 *upper_context, uint32_t precision_bits,
    uint32_t chart_mode, const cs_osdi_chart_decision_v3 *tape, uint64_t tape_count,
    cs_osdi_precision_cancel_probe cancelled, void *cancel_context,
    cs_osdi_chart_result_view *out, cs_osdi_precision_process_issue *issue) {
    return process_evaluate(CS_OSDI_CHART_PROTOCOL_VERSION, chart_mode, tape, tape_count, s,
        lower_high, lower_low, upper_high, upper_low, active_count, lower_context, upper_context,
        precision_bits, cancelled, cancel_context, out, issue);
}

int32_t cs_osdi_cover_process_evaluate(cs_osdi_precision_session *s,
    const double *lh,const double *ll,const double *uh,const double *ul,uint64_t count,
    const cs_osdi_precision_context_v1 *lower,const cs_osdi_precision_context_v1 *upper,uint32_t bits,
    cs_osdi_precision_cancel_probe cancelled,void *context,cs_osdi_cover_result_view *out,cs_osdi_precision_process_issue *issue){
    return process_evaluate(CS_OSDI_COVER_PROTOCOL_VERSION,0,NULL,0,s,lh,ll,uh,ul,count,lower,upper,bits,cancelled,context,out,issue);
}

int32_t cs_osdi_precision_process_identifier(const cs_osdi_precision_session *session) {
    return session ? (int32_t)session->pid : -1;
}
