/* Analytic stateless transport/source-export oracle, not a physical device model. */
#include "osdi_0_4.h"
#include "OSDICompilerPrecision.h"
#include "PrecisionProgram.h"
#include <stdlib.h>
uint32_t OSDI_VERSION_MAJOR=0,OSDI_VERSION_MINOR=4,OSDI_NUM_DESCRIPTORS=1;
uint32_t OSDI_DESCRIPTOR_SIZE=sizeof(OsdiDescriptor);
void (*osdi_log)(void *,const char *,uint32_t)=NULL;
uint32_t OSDI_PRECISION_V1_COUNT=1;
CSOsdiPrecisionExportV1 OSDI_PRECISION_V1[]={{1,sizeof(CSOsdiPrecisionExportV1),0,0,sizeof(precision_program),precision_program}};
static OsdiNode nodes[]={{"x","V","A",8,16,UINT32_MAX,UINT32_MAX,false}};
static OsdiNatureRef natures[]={{NATREF_NONE,0}};
static OsdiJacobianEntry jacobian[]={{{0,0},UINT32_MAX,JACOBIAN_ENTRY_RESIST}};
static void *access_parameter(void *i,void *m,uint32_t id,uint32_t flags){(void)i;(void)m;(void)id;(void)flags;return NULL;}
static void setup_model(void *h,void *m,OsdiSimParas *p,OsdiInitInfo *out){(void)h;(void)m;(void)p;*out=(OsdiInitInfo){0};}
static void setup_instance(void *h,void *i,void *m,double t,uint32_t n,OsdiSimParas *p,OsdiInitInfo *out){
    (void)h;(void)i;(void)m;(void)t;(void)n;(void)p;*out=(OsdiInitInfo){0};
}
static uint32_t evaluate(void *h,void *i,void *m,OsdiSimInfo *s){
    (void)h;(void)m;*(double *)((char *)i+8)=s->prev_solve[*(uint32_t *)i];*(double *)((char *)i+16)=0;return 0;
}
static void resist(void *i,void *m,double *out){(void)m;out[*(uint32_t *)i]+=*(double *)((char *)i+8);}
static void react(void *i,void *m,double *out){(void)i;(void)m;(void)out;}
static void load_jacobian(void *i,void *m){(void)m;**(double **)((char *)i+24)+=1;}
static void reactive_jacobian(void *i,void *m,double a){(void)i;(void)m;(void)a;}
static void rhs(void *i,void *m,double *d,double *p){(void)i;(void)m;(void)d;(void)p;abort();}
static void rhs_tran(void *i,void *m,double *d,double *p,double a){(void)i;(void)m;(void)d;(void)p;(void)a;abort();}
static void noise(void *i,void *m,double f,double *d){(void)i;(void)m;(void)f;(void)d;abort();}
static uint32_t given(void *s,uint32_t id){(void)s;(void)id;return 0;}
static void jacobian_array(void *i,void *m,double *d){(void)i;(void)m;d[0]=1;}
static void reactive_array(void *i,void *m,double *d){(void)i;(void)m;(void)d;}
static void jacobian_offset(void *i,void *m,size_t o){(void)i;(void)m;(void)o;abort();}
static void noise_parameters(void *i,void *m,double *p,double *e){(void)i;(void)m;(void)p;(void)e;abort();}
OsdiDescriptor OSDI_DESCRIPTORS[]={{.name="public_precision_source",.num_nodes=1,.num_terminals=1,.nodes=nodes,
 .num_jacobian_entries=1,.jacobian_entries=jacobian,.collapsed_offset=0,.node_mapping_offset=0,.jacobian_ptr_resist_offset=24,
 .num_states=0,.state_idx_off=0,.bound_step_offset=UINT32_MAX,.instance_size=32,.model_size=8,
 .access=access_parameter,.setup_model=setup_model,.setup_instance=setup_instance,.eval=evaluate,.load_noise=noise,
 .load_residual_resist=resist,.load_residual_react=react,.load_limit_rhs_resist=resist,.load_limit_rhs_react=react,
 .load_spice_rhs_dc=rhs,.load_spice_rhs_tran=rhs_tran,.load_jacobian_resist=load_jacobian,
 .load_jacobian_react=reactive_jacobian,.load_jacobian_tran=reactive_jacobian,
 .given_flag_model=given,.given_flag_instance=given,.num_resistive_jacobian_entries=1,.num_reactive_jacobian_entries=0,
 .write_jacobian_array_resist=jacobian_array,.write_jacobian_array_react=reactive_array,
 .load_jacobian_with_offset_resist=jacobian_offset,.load_jacobian_with_offset_react=jacobian_offset,
 .unknown_nature=natures,.residual_nature=natures,.load_noise_params=noise_parameters}};
