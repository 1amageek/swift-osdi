#include "../../Sources/COSDI/include/OSDIPrecisionProcess.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Complete cover process verification requires assertions"
#endif
typedef struct {void *program,*setup;size_t pn,sn;CSOsdiPrecisionProgramHeader h;CSOsdiPrecisionSetupHeader s;} input;
static size_t product(size_t a,size_t b){assert(!b || a<=SIZE_MAX/b);return a*b;}
static void *read_file(const char *path,size_t *length){FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<134217728 && !fseek(f,0,SEEK_SET));*length=(size_t)n;void *p=malloc(*length);assert(p && fread(p,1,*length,f)==*length && !fclose(f));return p;}
static input load(const char *pp,const char *sp){input in={0};in.program=read_file(pp,&in.pn);in.setup=read_file(sp,&in.sn);assert(in.pn>=sizeof(in.h) && in.sn>=sizeof(in.s));memcpy(&in.h,in.program,sizeof(in.h));memcpy(&in.s,in.setup,sizeof(in.s));return in;}
static void release(input *in){free(in->program);free(in->setup);}
static cs_osdi_precision_process_options options(void){cs_osdi_precision_process_options p={0};p.maximum_mapping_bytes=134217728;p.arena_bytes=33554432;p.maximum_work_units=100000000;p.minimum_precision_bits=128;p.maximum_precision_bits=256;p.startup_timeout_nanoseconds=p.evaluation_timeout_nanoseconds=10000000000;p.cleanup_timeout_nanoseconds=2000000000;p.poll_nanoseconds=1000000;return p;}
static cs_osdi_cover_limits_v4 limits(void){return (cs_osdi_cover_limits_v4){100000,100000,268435456,100,256,64,256,256,1025,0};}
static cs_osdi_precision_session *start(const char *worker,const input *in,cs_osdi_precision_process_options p,cs_osdi_cover_limits_v4 l){
  cs_osdi_precision_session *s=NULL;cs_osdi_precision_process_issue issue;
  int status=cs_osdi_cover_process_create(worker,in->program,in->pn,in->setup,in->sn,in->h.descriptor_index,in->s.active_unknown_count,in->h.raw_node_count,&p,&l,NULL,NULL,&s,&issue);
  if(status)fprintf(stderr,"cover start: code=%d worker=%u exit=%d errno=%d\n",status,issue.worker_status,issue.exit_code,issue.system_errno);assert(!status && s);return s;
}
static void close_session(cs_osdi_precision_session **s){cs_osdi_precision_process_issue issue;assert(!cs_osdi_precision_process_close(s,&issue) && !*s);assert(!cs_osdi_precision_process_close(s,&issue));}
static int scalar(cs_osdi_precision_session *s,double lo,double hi,cs_osdi_cover_result_view *v,cs_osdi_precision_process_issue *issue){double zero=0;cs_osdi_precision_context_v1 context={2,0,0,0};return cs_osdi_cover_process_evaluate(s,&lo,&zero,&hi,&zero,1,&context,&context,192,NULL,NULL,v,issue);}
static int cancel(void *context){unsigned *calls=context;++*calls;return 1;}
static input fixture(const char *dir,const char *name){char pp[4096],sp[4096];int n=snprintf(pp,sizeof(pp),"%s/%s.program",dir,name);assert(n>0 && (size_t)n<sizeof(pp));n=snprintf(sp,sizeof(sp),"%s/%s.setup",dir,name);assert(n>0 && (size_t)n<sizeof(sp));return load(pp,sp);}
static void synthetic(const char *worker,const char *dir){
  input in=fixture(dir,"equality");cs_osdi_precision_process_options p=options();cs_osdi_cover_limits_v4 l=limits();
  cs_osdi_precision_session *s=start(worker,&in,p,l);cs_osdi_cover_result_view v={0};cs_osdi_precision_process_issue issue;
  assert(!scalar(s,1,2,&v,&issue) && v.node_count==7 && v.leaf_count==3 && v.raw_node_count==1 && v.plane_stride==8);
  assert(v.lower_high[0]==1 && v.upper_high[0]==2 && v.nodes[0].event.allowed_mask==14);
  const cs_osdi_cover_node_v4 *address=v.nodes;const double *leaf_address=v.leaf_planes;
  uint64_t work=v.completed_work_units,steps=v.completed_steps,guards=v.completed_guard_records,copies=v.completed_copied_bytes;
  double snapshot[24];memcpy(snapshot,v.leaf_planes,sizeof(snapshot));
  assert(!scalar(s,1,2,&v,&issue) && v.nodes==address && v.leaf_planes==leaf_address && !memcmp(snapshot,v.leaf_planes,sizeof(snapshot)));
  assert(v.completed_steps==2*steps && v.completed_work_units>work && v.completed_guard_records==2*guards && v.completed_copied_bytes==2*copies);
  close_session(&s);
  for(unsigned failure=0;failure<7;++failure){cs_osdi_cover_limits_v4 bounded=l;cs_osdi_precision_process_options budget=p;
    if(failure==0)bounded.maximum_nodes=2;if(failure==1)bounded.maximum_steps=2*steps-1;
    if(failure==2)bounded.maximum_guard_records=2*guards-1;if(failure==3)bounded.maximum_copied_bytes=2*copies-1;
    if(failure==4)budget.maximum_work_units=2*work-1;
    s=start(worker,&in,budget,bounded);if(failure>=1 && failure<=4)assert(!scalar(s,1,2,&v,&issue));
    int status;
    if(failure==5){double lo=1,hi=2,zero=0;cs_osdi_precision_context_v1 context={2,0,0,0};unsigned calls=0;
      status=cs_osdi_cover_process_evaluate(s,&lo,&zero,&hi,&zero,1,&context,&context,192,cancel,&calls,&v,&issue);
      assert(status==CS_OSDI_PRECISION_PROCESS_CANCELLED && calls);
    }else if(failure==6){double zero=0,one=1;cs_osdi_precision_context_v1 context={2,0,0,0};cs_osdi_cover_result_view alias={0};
      status=cs_osdi_cover_process_evaluate(s,(const void *)&alias,&zero,&one,&zero,1,&context,&context,192,NULL,NULL,&alias,&issue);
      assert(status==CS_OSDI_PRECISION_PROCESS_INVALID && !alias.nodes);close_session(&s);continue;
    }else{status=scalar(s,1,2,&v,&issue);assert(status==CS_OSDI_PRECISION_PROCESS_NUMERIC);
      assert(issue.worker_status==(failure==1?CS_OSDI_CHART_STEP_LIMIT:failure==2?CS_OSDI_COVER_CAPACITY:failure==4?CS_OSDI_PRECISION_WORK_EXHAUSTED:CS_OSDI_COVER_CAPACITY));}
    assert(!v.nodes && !v.leaf_planes);close_session(&s);
  }
  s=start(worker,&in,p,l);cs_osdi_range_result_view range;double x=1,z=0;cs_osdi_precision_context_v1 context={2,0,0,0};
  assert(cs_osdi_range_process_evaluate(s,&x,&z,&x,&z,1,&context,&context,192,NULL,NULL,&range,&issue)==CS_OSDI_PRECISION_PROCESS_INVALID);close_session(&s);release(&in);
  in=fixture(dir,"correlated_invalid");s=start(worker,&in,p,l);assert(scalar(s,-1,1,&v,&issue)==CS_OSDI_PRECISION_PROCESS_NUMERIC && issue.worker_status==CS_OSDI_PRECISION_DOMAIN && !v.nodes);close_session(&s);release(&in);
  puts("complete-cover actual-worker equality/all-leaf/reuse/cumulative/cancel/alias/refusal tests passed");
}
static void forgeries(const char *worker,const char *pp,const char *sp){
  input in=load(pp,sp);
  for(unsigned mode=1;mode<=19;++mode){cs_osdi_precision_process_options p=options();p.program_sha256[0]=(unsigned char)mode;
    cs_osdi_precision_session *s=start(worker,&in,p,limits());cs_osdi_cover_result_view view;cs_osdi_precision_process_issue issue;
    assert(scalar(s,1,2,&view,&issue)==CS_OSDI_PRECISION_PROCESS_PROTOCOL && !view.nodes && !view.leaf_planes);close_session(&s);
  }
  release(&in);puts("complete-cover parent rejected nineteen tree/cell/source/binding/leaf/counter/exclusion forgeries");
}
static void actual(char **argv){
  input in=load(argv[2],argv[3]);size_t size;unsigned char *data=read_file(argv[4],&size);assert(size>=sizeof(cs_osdi_precision_control_v1));cs_osdi_precision_control_v1 control;memcpy(&control,data,sizeof(control));
  size_t n=in.s.active_unknown_count;assert(size==sizeof(control)+product(product(n,4),sizeof(double)));
  cs_osdi_precision_process_options p=options();p.arena_bytes=strtoull(argv[5],NULL,10);p.maximum_work_units=strtoull(argv[6],NULL,10);p.minimum_precision_bits=control.minimum_precision_bits;p.maximum_precision_bits=control.maximum_precision_bits;
  memcpy(p.program_sha256,control.program_sha256,32);memcpy(p.worker_profile_sha256,control.worker_profile_sha256,32);
  cs_osdi_cover_limits_v4 l=limits();l.maximum_steps=l.maximum_guard_records=strtoull(argv[7],NULL,10);uint64_t capacity=strtoull(argv[8],NULL,10);assert(capacity && capacity<UINT32_MAX);l.maximum_nodes=l.maximum_pending=l.maximum_depth=(uint32_t)capacity;l.maximum_leaves=16;
  cs_osdi_precision_session *s=start(argv[1],&in,p,l);const double *v=(const void *)(data+sizeof(control));cs_osdi_cover_result_view out;cs_osdi_precision_process_issue issue;
  int status=cs_osdi_cover_process_evaluate(s,v,v+n,v+2*n,v+3*n,n,&control.candidate,&control.reference,control.requested_precision_bits,NULL,NULL,&out,&issue);
  if(status)fprintf(stderr,"actual complete cover: code=%d status=%u instruction=%u exit=%d errno=%d\n",status,issue.worker_status,issue.instruction,issue.exit_code,issue.system_errno);
  assert(!status && out.node_count && out.leaf_count);
  printf("complete-cover process actual: %u nodes %u leaves %" PRIu64 " steps %" PRIu64 " guards %" PRIu64 " copies %" PRIu64 " work\n",out.node_count,out.leaf_count,out.completed_steps,out.completed_guard_records,out.completed_copied_bytes,out.completed_work_units);
  close_session(&s);free(data);release(&in);
}
int main(int argc,char **argv){assert(argc==3 || argc==4 || argc==9);if(argc==3)synthetic(argv[1],argv[2]);else if(argc==4)forgeries(argv[1],argv[2],argv[3]);else actual(argv);return 0;}
