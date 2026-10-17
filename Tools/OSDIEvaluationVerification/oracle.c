/* Independent trusted-native OSDI 0.4 point observer. No COSDI/runtime linkage. */
#include "osdi_0_4.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) { fprintf(stderr, "%s\n", message); exit(2); }
static void *allocate(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) fail("allocation overflow");
    void *value = calloc(count ? count : 1, size);
    if (!value) fail("allocation failed");
    return value;
}
static void *checked(void *base, size_t size, uint32_t offset, size_t length) {
    if (offset > size || length > size-offset) fail("instance span out of bounds");
    return (char *)base+offset;
}
static double number(const char *text) {
    char *end; errno=0; double value=strtod(text,&end);
    if (errno || end==text || *end || !isfinite(value)) fail("invalid finite number");
    return value;
}
static void real(double value) {
    if (!isfinite(value)) fail("nonfinite observation");
    if (value==0 && signbit(value)) { fputs("-0.0",stdout); return; }
    printf("%.17g",value);
}
static void string(const char *text) {
    if (!text) fail("null observed string");
    putchar('"');
    for (;*text;++text) {
        unsigned char value=(unsigned char)*text;
        if (value=='"' || value=='\\') { putchar('\\'); putchar(value); }
        else if (value<32) printf("\\u%04x",value);
        else putchar(value);
    }
    putchar('"');
}
static void vector(const double *values,uint32_t count) {
    putchar('[');
    for(uint32_t index=0;index<count;++index) { if(index)putchar(','); real(values[index]); }
    putchar(']');
}
static uint32_t representative(uint32_t *parents,uint32_t index) {
    while(parents[index]!=index) index=parents[index];
    return index;
}
static uint32_t parameter(OsdiDescriptor *descriptor,const char *name) {
    uint32_t found=UINT32_MAX;
    for(uint32_t index=0;index<descriptor->num_params;++index) {
        OsdiParamOpvar *entry=&descriptor->param_opvar[index];
        for(uint32_t alias=0;alias<=entry->num_alias;++alias) {
            if(!strcmp(entry->name[alias],name)) {
                if(found!=UINT32_MAX && found!=index) fail("ambiguous parameter");
                found=index;
            }
        }
    }
    if(found==UINT32_MAX) fail("unknown parameter");
    return found;
}
static void log_message(void *handle,const char *message,uint32_t level) {
    (void)handle; fprintf(stderr,"model[%u] %s\n",level,message);
    /* Native formatting transfers allocated messages except LOG_FMT_ERR literals. */
    if(message && !(level&LOG_FMT_ERR))free((void *)message);
}
static void initialized(OsdiInitInfo *info,const char *stage) {
    if(info->flags || info->num_errors) {
        printf("{\"failure\":{\"stage\":");string(stage);
        printf(",\"flags\":%u,\"count\":%u,\"errors\":[",info->flags,info->num_errors);
        for(uint32_t index=0;index<info->num_errors;++index) {
            if(index)putchar(',');
            printf("{\"code\":%u,\"parameterID\":%u}",info->errors[index].code,info->errors[index].payload.parameter_id);
        }
        puts("]}}");exit(2);
    }
}
static void value(void *pointer,uint32_t type) {
    if(!pointer)fail("parameter read rejected");
    if(type==PARA_TY_REAL) { printf("{\"kind\":\"real\",\"real\":");real(*(double *)pointer);putchar('}'); }
    else if(type==PARA_TY_INT)printf("{\"kind\":\"integer\",\"integer\":%d}",*(int32_t *)pointer);
    else if(type==PARA_TY_STR) { printf("{\"kind\":\"string\",\"string\":");string(*(char **)pointer);putchar('}'); }
    else fail("unsupported parameter scalar type");
}
int main(int argc,char **argv) {
    if(argc<5)fail("usage: oracle library descriptorIndex kelvin ports [p:m|i:r|i|s:name=value] [v:node=value] [r:name]");
    double selected=number(argv[2]),temperature=number(argv[3]),ports=number(argv[4]);
    if(selected<0 || selected>UINT32_MAX || selected!=floor(selected) || temperature<=0 || ports<0 || ports>64 || ports!=floor(ports))fail("invalid preparation inputs");
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library)fail(dlerror());
    uint32_t *major=dlsym(library,"OSDI_VERSION_MAJOR"),*minor=dlsym(library,"OSDI_VERSION_MINOR");
    uint32_t *count=dlsym(library,"OSDI_NUM_DESCRIPTORS"),*stride=dlsym(library,"OSDI_DESCRIPTOR_SIZE");
    unsigned char *descriptors=dlsym(library,"OSDI_DESCRIPTORS");
    if(!major||!minor||!count||!stride||!descriptors||*major!=0||*minor!=4||*stride<sizeof(OsdiDescriptor)||selected>=*count)fail("unsupported oracle ABI");
    OsdiDescriptor *d=(OsdiDescriptor *)(descriptors+(uint32_t)selected*(size_t)*stride);
    if(d->num_nodes==0||d->num_nodes>64||d->num_states>64||ports>d->num_terminals||d->num_params>8192||d->absdelay_count)fail("oracle bounds exceeded");
    void (**logger)(void *,const char *,uint32_t)=dlsym(library,"osdi_log");
    if(logger)*logger=log_message;
    uint32_t n=d->num_nodes;
    void *model=allocate(d->model_size,1),*instance=allocate(d->instance_size,1);
    double *bias=allocate(n,sizeof(double));bool *bias_given=allocate(n,sizeof(bool));
    bool *given=allocate((size_t)d->num_params*2,sizeof(bool));
    bool *probed=allocate(d->num_params,sizeof(bool));
    char **owned=allocate((size_t)argc,sizeof(char *));uint32_t owned_count=0;
    for(int argument=5;argument<argc;++argument) {
        char *assignment=strdup(argv[argument]);if(!assignment)fail("allocation failed");
        owned[owned_count++]=assignment;
        if(!strncmp(assignment,"r:",2)) { probed[parameter(d,assignment+2)]=true;continue; }
        char *equal=strchr(assignment,'=');if(!equal)fail("missing assignment value");*equal=0;
        if(!strncmp(assignment,"v:",2)) {
            bool found=false;
            for(uint32_t index=0;index<n;++index)if(!strcmp(d->nodes[index].name,assignment+2)) {
                if(bias_given[index])fail("duplicate potential");
                bias[index]=number(equal+1);bias_given[index]=true;found=true;break;
            }
            if(!found)fail("unknown potential node");
            continue;
        }
        if(strlen(assignment)<7||assignment[0]!='p'||assignment[1]!=':'||assignment[3]!=':'||assignment[5]!=':')fail("invalid parameter assignment");
        uint32_t scope=assignment[2]=='m'?0:(assignment[2]=='i'?1:UINT32_MAX);
        uint32_t type=assignment[4]=='r'?PARA_TY_REAL:(assignment[4]=='i'?PARA_TY_INT:(assignment[4]=='s'?PARA_TY_STR:UINT32_MAX));
        uint32_t index=parameter(d,assignment+6);OsdiParamOpvar *entry=&d->param_opvar[index];
        uint32_t kind=entry->flags>>30;
        if(scope>1||type!=(entry->flags&PARA_TY_MASK)||entry->len||(scope==1&&kind!=1))fail("parameter type or scope mismatch");
        if(given[index*2+scope])fail("duplicate canonical or alias assignment");
        given[index*2+scope]=true;probed[index]=true;
        void *destination=d->access(instance,model,index,ACCESS_FLAG_SET|(scope?ACCESS_FLAG_INSTANCE:0));
        if(!destination)fail("parameter write rejected");
        if(type==PARA_TY_REAL)*(double *)destination=number(equal+1);
        else if(type==PARA_TY_INT) {
            double integer=number(equal+1);
            if(integer<INT32_MIN||integer>INT32_MAX||integer!=floor(integer))fail("invalid int32");
            *(int32_t *)destination=(int32_t)integer;
        } else *(char **)destination=equal+1;
    }
    char *names[]={"gmin",NULL},*strings[]={NULL};double numeric[]={0};
    OsdiSimParas paras={names,numeric,strings,strings};OsdiInitInfo init={0};
    d->setup_model(NULL,model,&paras,&init);initialized(&init,"setup_model");
    init=(OsdiInitInfo){0};
    d->setup_instance(NULL,instance,model,temperature,(uint32_t)ports,&paras,&init);initialized(&init,"setup_instance");
    uint32_t *parent=allocate(n+1,sizeof(uint32_t));for(uint32_t index=0;index<=n;++index)parent[index]=index;
    bool *collapsed=checked(instance,d->instance_size,d->collapsed_offset,d->num_collapsible*sizeof(bool));
    for(uint32_t index=0;index<d->num_collapsible;++index)if(collapsed[index]) {
        uint32_t a=d->collapsible[index].node_1,b=d->collapsible[index].node_2;
        if(a>=n||(b!=UINT32_MAX&&b>=n))fail("invalid collapse node");
        a=representative(parent,a);b=representative(parent,b==UINT32_MAX?n:b);
        if(a==n)parent[b]=a;else if(b==n)parent[a]=b;else if(a<b)parent[b]=a;else parent[a]=b;
    }
    uint32_t *mapping=checked(instance,d->instance_size,d->node_mapping_offset,n*sizeof(uint32_t));
    uint32_t *dense=allocate(n,sizeof(uint32_t)),*root_to_dense=allocate(n+1,sizeof(uint32_t));
    for(uint32_t index=0;index<=n;++index)root_to_dense[index]=UINT32_MAX;
    uint32_t active=0;
    for(uint32_t index=0;index<n;++index) {
        uint32_t root=representative(parent,index);mapping[index]=root==n?0:root+1;
        if(root==n)dense[index]=UINT32_MAX;
        else { if(root_to_dense[root]==UINT32_MAX)root_to_dense[root]=active++;dense[index]=root_to_dense[root]; }
    }
    double *solve=allocate(n+1,sizeof(double)),*active_values=allocate(active,sizeof(double));
    bool *solve_given=allocate(n+1,sizeof(bool));
    for(uint32_t index=0;index<n;++index)if(bias_given[index]) {
        uint32_t root=mapping[index];
        if((!root&&bias[index]!=0)||(solve_given[root]&&solve[root]!=bias[index]))fail("conflicting aliased potentials");
        solve[root]=bias[index];solve_given[root]=true;
    }
    for(uint32_t index=0;index<n;++index) {
        bias[index]=solve[mapping[index]];
        if(dense[index]!=UINT32_MAX)active_values[dense[index]]=bias[index];
    }
    double *f=allocate(n,sizeof(double)),*q=allocate(n,sizeof(double));
    double *assembled_f=allocate(n+1,sizeof(double)),*assembled_q=allocate(n+1,sizeof(double));
    double *g=allocate((size_t)n*n,sizeof(double)),*c=allocate((size_t)n*n,sizeof(double));
    double **gptr=checked(instance,d->instance_size,d->jacobian_ptr_resist_offset,d->num_jacobian_entries*sizeof(double *));
    for(uint32_t index=0;index<d->num_jacobian_entries;++index) {
        OsdiJacobianEntry *entry=&d->jacobian_entries[index];
        if(entry->nodes.node_1>=n||entry->nodes.node_2>=n)fail("invalid Jacobian node");
        size_t position=(size_t)entry->nodes.node_1*n+entry->nodes.node_2;
        gptr[index]=&g[position];
        if(entry->react_ptr_off!=UINT32_MAX)*(double **)checked(instance,d->instance_size,entry->react_ptr_off,sizeof(double *))=&c[position];
    }
    double *previous_state=allocate(d->num_states,sizeof(double)),*next_state=allocate(d->num_states,sizeof(double));
    const uint32_t flags=CALC_RESIST_RESIDUAL|CALC_REACT_RESIDUAL|CALC_RESIST_JACOBIAN|CALC_REACT_JACOBIAN|CALC_OP|ANALYSIS_DC|ANALYSIS_STATIC;
    OsdiSimInfo info={.paras=paras,.abstime=0,.prev_solve=solve,.prev_state=previous_state,.next_state=next_state,.flags=flags};
    uint32_t returned=d->eval(NULL,instance,model,&info);
    if(returned) { printf("{\"failure\":{\"stage\":\"eval\",\"flags\":%u}}\n",returned);return 2; }
    d->load_residual_resist(instance,model,assembled_f);d->load_residual_react(instance,model,assembled_q);
    d->load_jacobian_resist(instance,model);d->load_jacobian_react(instance,model,1);
    for(uint32_t index=0;index<n;++index) {
        if(d->nodes[index].resist_residual_off!=UINT32_MAX)f[index]=*(double *)checked(instance,d->instance_size,d->nodes[index].resist_residual_off,sizeof(double));
        if(d->nodes[index].react_residual_off!=UINT32_MAX)q[index]=*(double *)checked(instance,d->instance_size,d->nodes[index].react_residual_off,sizeof(double));
    }
    printf("{\"schema\":\"osdi.independent-point.v1\",\"descriptor\":");string(d->name);
    printf(",\"descriptorIndex\":%u,\"evaluationFlags\":%u,\"modelFlags\":%u,\"stateCount\":%u,\"nodes\":[",(uint32_t)selected,flags,returned,d->num_states);
    for(uint32_t index=0;index<n;++index) { if(index)putchar(',');string(d->nodes[index].name); }
    printf("],\"nodeMetadata\":[");
    for(uint32_t index=0;index<n;++index) {
        if(index)putchar(',');printf("{\"units\":");string(d->nodes[index].units);
        printf(",\"residualUnits\":");string(d->nodes[index].residual_units);
        printf(",\"isFlow\":%s}",d->nodes[index].is_flow?"true":"false");
    }
    printf("],\"collapseMap\":[");
    for(uint32_t index=0;index<n;++index) { if(index)putchar(',');if(dense[index]==UINT32_MAX)printf("null");else printf("%u",dense[index]); }
    printf("],\"localUnknowns\":");vector(active_values,active);
    printf(",\"rawPotentials\":");vector(bias,n);
    printf(",\"f\":");vector(f,n);printf(",\"q\":");vector(q,n);
    printf(",\"g\":");vector(g,n*n);printf(",\"c\":");vector(c,n*n);
    printf(",\"nextState\":");vector(next_state,d->num_states);
    printf(",\"parameters\":[");bool first=true;
    for(uint32_t index=0;index<d->num_params;++index)if(probed[index]) {
        OsdiParamOpvar *entry=&d->param_opvar[index];uint32_t kind=entry->flags>>30,type=entry->flags&PARA_TY_MASK;
        if(!first)putchar(',');first=false;
        printf("{\"index\":%u,\"kind\":%u,\"type\":%u,\"names\":[",index,kind,type);
        for(uint32_t alias=0;alias<=entry->num_alias;++alias) { if(alias)putchar(',');string(entry->name[alias]); }
        printf("],\"modelValue\":");value(d->access(instance,model,index,ACCESS_FLAG_READ),type);
        printf(",\"modelGiven\":%s",d->given_flag_model(model,index)?"true":"false");
        if(kind==1) {
            printf(",\"instanceValue\":");value(d->access(instance,model,index,ACCESS_FLAG_READ|ACCESS_FLAG_INSTANCE),type);
            printf(",\"instanceGiven\":%s",d->given_flag_instance(instance,index)?"true":"false");
        }
        putchar('}');
    }
    puts("]}");
    free(model);free(instance);free(bias);free(bias_given);free(given);free(probed);
    free(parent);free(dense);free(root_to_dense);free(solve);free(active_values);free(solve_given);
    free(f);free(q);free(assembled_f);free(assembled_q);free(g);free(c);free(previous_state);free(next_state);
    for(uint32_t index=0;index<owned_count;++index)free(owned[index]);free(owned);
    if(dlclose(library))fail("oracle unload failed");return 0;
}
