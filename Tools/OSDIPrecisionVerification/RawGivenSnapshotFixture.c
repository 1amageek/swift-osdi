/* Analytic own-scope/merged-given oracle. No semiconductor model physics. */
#include "osdi_0_4.h"
#include "OSDICompilerPrecision.h"
#include "RawGivenProgram.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct { double value; uint32_t given; } FixtureModel;
typedef struct {
    uint32_t nodes[2];
    double f[2],q[2];
    double *jacobian[2],*reactive[2];
    double value;
    uint32_t given;
    double factor;
} FixtureInstance;
uint32_t OSDI_VERSION_MAJOR=0,OSDI_VERSION_MINOR=4,OSDI_NUM_DESCRIPTORS=1;
uint32_t OSDI_DESCRIPTOR_SIZE=sizeof(OsdiDescriptor);
void (*osdi_log)(void *,const char *,uint32_t)=NULL;
uint32_t OSDI_PRECISION_V1_COUNT=1;
CSOsdiPrecisionExportV1 OSDI_PRECISION_V1[]={{1,sizeof(CSOsdiPrecisionExportV1),0,0,sizeof(precision_program),precision_program}};
static OsdiNode nodes[]={{"x","V","A",offsetof(FixtureInstance,f[0]),offsetof(FixtureInstance,q[0]),UINT32_MAX,UINT32_MAX,false},
                        {"y","V","A",offsetof(FixtureInstance,f[1]),offsetof(FixtureInstance,q[1]),UINT32_MAX,UINT32_MAX,false}};
static OsdiNatureRef natures[]={{NATREF_NONE,0},{NATREF_NONE,0}};
static OsdiJacobianEntry jacobian[]={{{0,0},offsetof(FixtureInstance,reactive[0]),JACOBIAN_ENTRY_RESIST|JACOBIAN_ENTRY_REACT},
                                   {{1,0},offsetof(FixtureInstance,reactive[1]),JACOBIAN_ENTRY_RESIST|JACOBIAN_ENTRY_REACT}};
static char *names[]={"gain"};
static OsdiParamOpvar parameters[]={{names,0,"Scoped assignment presence","",PARA_TY_REAL|PARA_KIND_INST,0}};
static void *access_parameter(void *instance,void *model,uint32_t id,uint32_t flags){
    if(id)return NULL;
    if(flags&ACCESS_FLAG_INSTANCE){FixtureInstance *s=instance;if(!s)return NULL;if(flags&ACCESS_FLAG_SET)s->given=1;return &s->value;}
    FixtureModel *s=model;if(!s)return NULL;if(flags&ACCESS_FLAG_SET)s->given=1;return &s->value;
}
static uint32_t given_model(void *model,uint32_t id){return model&&!id?((FixtureModel*)model)->given:0;}
static uint32_t given_instance(void *instance,uint32_t id){return instance&&!id?((FixtureInstance*)instance)->given:0;}
static void setup_model(void *h,void *m,OsdiSimParas *p,OsdiInitInfo *out){(void)h;(void)m;(void)p;*out=(OsdiInitInfo){0};}
static void setup_instance(void *h,void *instance,void *model,double t,uint32_t n,OsdiSimParas *p,OsdiInitInfo *out){
    (void)h;(void)t;(void)n;(void)p;FixtureInstance *s=instance;FixtureModel *m=model;
    if(!s->given)s->value=m->value;
    s->factor=(double)m->given+(s->given?2.0:1.0)+(double)s->given+(double)(s->given||m->given);*out=(OsdiInitInfo){0};
}
static uint32_t evaluate(void *h,void *instance,void *model,OsdiSimInfo *info){
    (void)h;FixtureInstance *s=instance;FixtureModel *m=model;double x=info->prev_solve[s->nodes[0]];
    s->f[0]=s->factor*x;s->f[1]=x;s->q[0]=(s->given||m->given)?x:0;s->q[1]=x;return 0;
}
static void resist(void *instance,void *model,double *out){(void)model;FixtureInstance *s=instance;for(unsigned j=0;j<2;j++)out[s->nodes[j]]+=s->f[j];}
static void react(void *instance,void *model,double *out){(void)model;FixtureInstance *s=instance;for(unsigned j=0;j<2;j++)out[s->nodes[j]]+=s->q[j];}
static void load_jacobian(void *instance,void *model){(void)model;FixtureInstance *s=instance;*s->jacobian[0]+=s->factor;*s->jacobian[1]+=1;}
static void reactive_jacobian(void *instance,void *model,double alpha){FixtureInstance *s=instance;FixtureModel *m=model;*s->reactive[0]+=alpha*(s->given||m->given);*s->reactive[1]+=alpha;}
static void rhs(void *i,void *m,double *d,double *p){(void)i;(void)m;(void)d;(void)p;abort();}
static void rhs_tran(void *i,void *m,double *d,double *p,double a){(void)i;(void)m;(void)d;(void)p;(void)a;abort();}
static void noise(void *i,void *m,double f,double *d){(void)i;(void)m;(void)f;(void)d;abort();}
static void jacobian_array(void *instance,void *model,double *out){(void)model;out[0]=((FixtureInstance*)instance)->factor;out[1]=1;}
static void reactive_array(void *instance,void *model,double *out){out[0]=((FixtureInstance*)instance)->given||((FixtureModel*)model)->given;out[1]=1;}
static void jacobian_offset(void *i,void *m,size_t o){(void)i;(void)m;(void)o;abort();}
static void noise_parameters(void *i,void *m,double *p,double *e){(void)i;(void)m;(void)p;(void)e;abort();}
OsdiDescriptor OSDI_DESCRIPTORS[]={{.name="raw_given_source",.num_nodes=2,.num_terminals=2,.nodes=nodes,
 .num_jacobian_entries=2,.jacobian_entries=jacobian,.num_params=1,.num_instance_params=1,.param_opvar=parameters,
 .collapsed_offset=0,.node_mapping_offset=offsetof(FixtureInstance,nodes),.jacobian_ptr_resist_offset=offsetof(FixtureInstance,jacobian),
 .num_states=0,.state_idx_off=0,.bound_step_offset=UINT32_MAX,.instance_size=sizeof(FixtureInstance),.model_size=sizeof(FixtureModel),
 .access=access_parameter,.setup_model=setup_model,.setup_instance=setup_instance,.eval=evaluate,.load_noise=noise,
 .load_residual_resist=resist,.load_residual_react=react,.load_limit_rhs_resist=resist,.load_limit_rhs_react=react,
 .load_spice_rhs_dc=rhs,.load_spice_rhs_tran=rhs_tran,.load_jacobian_resist=load_jacobian,.load_jacobian_react=reactive_jacobian,
 .load_jacobian_tran=reactive_jacobian,.given_flag_model=given_model,.given_flag_instance=given_instance,
 .num_resistive_jacobian_entries=2,.num_reactive_jacobian_entries=2,.write_jacobian_array_resist=jacobian_array,
 .write_jacobian_array_react=reactive_array,.load_jacobian_with_offset_resist=jacobian_offset,
 .load_jacobian_with_offset_react=jacobian_offset,.unknown_nature=natures,.residual_nature=natures,.load_noise_params=noise_parameters}};
