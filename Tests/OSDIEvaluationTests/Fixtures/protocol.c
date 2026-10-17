/* ABI state/flags/logger transport oracle only; not a physical model reference. */
#include "osdi_0_4.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef PROTOCOL_MODE
#error PROTOCOL_MODE must select a protocol transport test
#endif
uint32_t OSDI_VERSION_MAJOR=0,OSDI_VERSION_MINOR=4;
uint32_t OSDI_NUM_DESCRIPTORS=1,OSDI_DESCRIPTOR_SIZE=sizeof(OsdiDescriptor);
void (*osdi_log)(void *,const char *,uint32_t)=NULL;
#if PROTOCOL_MODE == 13
#define REACTIVE_RESIDUAL_OFFSET UINT32_MAX
#else
#define REACTIVE_RESIDUAL_OFFSET 24
#endif
static OsdiNode nodes[]={{"p","V","A",16,REACTIVE_RESIDUAL_OFFSET,UINT32_MAX,UINT32_MAX,false}};
static OsdiNatureRef natures[]={{NATREF_NONE,0}};
static OsdiJacobianEntry jacobian[]={{{0,0},UINT32_MAX,JACOBIAN_ENTRY_RESIST}};
static void *access_parameter(void *instance,void *model,uint32_t id,uint32_t flags) {
    (void)instance;(void)model;(void)id;(void)flags;return NULL;
}
static void setup_model(void *handle,void *model,OsdiSimParas *paras,OsdiInitInfo *info) {
    (void)handle;(void)model;(void)paras;*info=(OsdiInitInfo){0};
}
static void setup_instance(void *handle,void *instance,void *model,double temperature,uint32_t terminals,OsdiSimParas *paras,OsdiInitInfo *info) {
    (void)handle;(void)model;(void)temperature;(void)terminals;(void)paras;
    uint32_t *states=(uint32_t *)((char *)instance+8);states[0]=0;states[1]=1;
    *info=(OsdiInitInfo){0};
}
static uint32_t evaluate(void *handle,void *instance,void *model,OsdiSimInfo *info) {
    (void)model;
    double input=info->prev_solve[*(uint32_t *)instance];
    uint32_t *indices=(uint32_t *)((char *)instance+8);
    info->next_state[indices[0]]=info->prev_state[indices[0]]+input;
    /* The second candidate slot is intentionally untouched: caller retains previous state. */
    *(double *)((char *)instance+16)=input+info->prev_state[indices[0]];
    *(double *)((char *)instance+24)=0;
    if(PROTOCOL_MODE==12) *(double *)((char *)instance+24)=info->abstime+info->prev_state[indices[0]];
    if(PROTOCOL_MODE==1)return EVAL_RET_FLAG_FATAL;
    if(PROTOCOL_MODE==2)return EVAL_RET_FLAG_FINISH;
    if(PROTOCOL_MODE==3)return EVAL_RET_FLAG_STOP;
    if(PROTOCOL_MODE==7)return EVAL_RET_FLAG_LIM;
    if(PROTOCOL_MODE==8)return UINT32_C(0x80000000);
    if(PROTOCOL_MODE==4||PROTOCOL_MODE==5) {
        if(!osdi_log)abort();
        unsigned int count=PROTOCOL_MODE==5?3:1;
        for(unsigned int index=0;index<count;++index) {
            char *message=strdup("owned protocol warning");if(!message)abort();
            osdi_log(handle,message,LOG_LVL_WARN);
        }
        osdi_log(handle,"literal protocol warning",LOG_LVL_WARN|LOG_FMT_ERR);
    }
    if(PROTOCOL_MODE==6) {
        if(!osdi_log)abort();
        osdi_log(handle,"literal protocol error",LOG_LVL_ERR|LOG_FMT_ERR);
    }
    if(PROTOCOL_MODE==9)*(double *)((char *)instance+16)=1.0/0.0;
    return 0;
}
static void load_resist(void *instance,void *model,double *destination) {
    (void)model;destination[*(uint32_t *)instance]+=*(double *)((char *)instance+16);
}
static void load_react(void *instance,void *model,double *destination) {
    (void)model;destination[*(uint32_t *)instance]+=*(double *)((char *)instance+24);
}
static void load_jacobian(void *instance,void *model) {
    (void)model;double **pointers=(double **)((char *)instance+32);*pointers[0]+=1;
}
static void load_reactive_jacobian(void *instance,void *model,double alpha) {
    (void)instance;(void)model;(void)alpha;
}
static void load_rhs(void *instance,void *model,double *destination,double *previous) {
    (void)instance;(void)model;(void)destination;(void)previous;abort();
}
static void load_rhs_tran(void *instance,void *model,double *destination,double *previous,double alpha) {
    (void)instance;(void)model;(void)destination;(void)previous;(void)alpha;abort();
}
static void load_noise(void *instance,void *model,double frequency,double *destination) {
    (void)instance;(void)model;(void)frequency;
    if (PROTOCOL_MODE==10) { destination[0]=-1; return; }
    if (PROTOCOL_MODE==11) { destination[0]=NAN; return; }
    abort();
}
static uint32_t given(void *storage,uint32_t id) { (void)storage;(void)id;return 0; }
static void jacobian_array(void *instance,void *model,double *destination) {
    (void)instance;(void)model;destination[0]=1;
}
static void reactive_array(void *instance,void *model,double *destination) {
    (void)instance;(void)model;(void)destination;
}
static void jacobian_offset(void *instance,void *model,size_t offset) {
    (void)instance;(void)model;(void)offset;abort();
}
static void noise_parameters(void *instance,void *model,double *power,double *exponent) {
    (void)instance;(void)model;(void)power;(void)exponent;abort();
}
#if PROTOCOL_MODE == 10 || PROTOCOL_MODE == 11
static OsdiNoiseSource noise_sources[]={{"invalid-spectrum",{0,UINT32_MAX}}};
static uint32_t noise_types[]={NOISE_TYPE_WHITE};
#endif
OsdiDescriptor OSDI_DESCRIPTORS[]={
    {
#if PROTOCOL_MODE == 10 || PROTOCOL_MODE == 11
     .num_noise_src=1,.noise_sources=noise_sources,.noise_source_type=noise_types,
#endif
     .name="protocol_transport",.num_nodes=1,.num_terminals=1,.nodes=nodes,
     .num_jacobian_entries=1,.jacobian_entries=jacobian,.collapsed_offset=0,
     .node_mapping_offset=0,.jacobian_ptr_resist_offset=32,.num_states=2,.state_idx_off=8,
     .bound_step_offset=UINT32_MAX,.instance_size=64,.model_size=8,
     .access=access_parameter,.setup_model=setup_model,.setup_instance=setup_instance,.eval=evaluate,
     .load_noise=load_noise,.load_residual_resist=load_resist,.load_residual_react=load_react,
     .load_limit_rhs_resist=load_resist,.load_limit_rhs_react=load_react,
     .load_spice_rhs_dc=load_rhs,.load_spice_rhs_tran=load_rhs_tran,
     .load_jacobian_resist=load_jacobian,.load_jacobian_react=load_reactive_jacobian,
     .load_jacobian_tran=load_reactive_jacobian,.given_flag_model=given,.given_flag_instance=given,
     .num_resistive_jacobian_entries=1,.num_reactive_jacobian_entries=0,
     .write_jacobian_array_resist=jacobian_array,.write_jacobian_array_react=reactive_array,
     .load_jacobian_with_offset_resist=jacobian_offset,.load_jacobian_with_offset_react=jacobian_offset,
     .unknown_nature=natures,.residual_nature=natures,.load_noise_params=noise_parameters}
};
