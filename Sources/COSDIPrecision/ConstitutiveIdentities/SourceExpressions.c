#include "IdentityInternal.h"

static int node(cs_precision_constitutive_workspace *w, uint32_t kind, uint32_t source,
    uint32_t a, uint32_t b, uint64_t bits, uint32_t *out) {
  if (w->node_count == w->limits.maximum_nodes) return CS_IDENTITY_CAPACITY;
  uint32_t id = ++w->node_count;
  cs_precision_constitutive_node n = {kind,source,a,b,0,0,bits};
  if(kind==CS_ID_LITERAL)n.constant=1;
  else if(kind==CS_ID_NEG)n.constant=w->nodes[a].constant;
  else if(kind==CS_ID_ADD||kind==CS_ID_SUB||kind==CS_ID_MUL)
    n.constant=w->nodes[a].constant && w->nodes[b].constant;
  w->nodes[id]=n;*out=id;return 0;
}
/* RAW_DIFFERENCE is an exact ABI input formula, not a numeric range fact.
   Canonical atoms are scoped to this source/setup and reset for every replay. */
static int coordinate(cs_precision_constitutive_workspace *w,uint32_t raw,uint32_t *out){
  cs_precision_program *p=w->program;
  if(raw==CS_ID_NONE){*out=0;return 0;}
  if(raw>=p->header->raw_node_count)return CS_IDENTITY_INVALID;
  uint32_t active=p->map[raw];
  if(active==CS_ID_NONE){*out=0;return 0;}
  if(active>=p->active_count)return CS_IDENTITY_INVALID;
  active=w->quotient[active];
  if(active==CS_ID_NONE){*out=0;return 0;}
  if(active>=p->active_count||w->quotient[active]!=active)return CS_IDENTITY_INVALID;
  if(!cs_precision_charge(&p->math,1))return p->math.status;
  if(!w->coordinates[active]){
    int status=node(w,CS_ID_COORDINATE,active,0,0,0,&w->coordinates[active]);if(status)return status;
  }
  *out=w->coordinates[active];return 0;
}
int cs_constitutive_observe(void *context, cs_precision_program *p, uint32_t event,
    uint32_t index, uint32_t source) {
  cs_precision_constitutive_workspace *w=context;
  if(p!=w->program || !cs_precision_charge(&p->math,1))return p->math.status?p->math.status:CS_IDENTITY_INVALID;
  if(event==CS_SOURCE_RESET){
    w->node_count=0;w->atom_count=0;
    if(!cs_precision_charge(&p->math,(uint64_t)p->header->registers.count+p->header->roots.count+p->stage_capacity+p->active_count))return p->math.status;
    memset(w->registers,0,(size_t)p->header->registers.count*sizeof(uint32_t));
    memset(w->roots,0,(size_t)p->header->roots.count*sizeof(uint32_t));
    memset(w->staged,0,(size_t)p->stage_capacity*sizeof(uint32_t));
    memset(w->coordinates,0,(size_t)p->active_count*sizeof(uint32_t));
    w->nodes[0]=(cs_precision_constitutive_node){CS_ID_LITERAL,0,0,0,1,0,0};return 0;
  }
  if(event==CS_SOURCE_PHI_STAGE){w->staged[index]=w->registers[source];return 0;}
  if(event==CS_SOURCE_PHI_ASSIGN){w->registers[index]=w->staged[source];return 0;}
  if(event==CS_SOURCE_ROOT){w->roots[index]=w->registers[source];return 0;}
  if(event==CS_SOURCE_CONSTANT){
    const CSOsdiPrecisionConstant *c=&p->constants[index];
    uint32_t kind=c->type==1 && ((c->bits>>52)&2047)!=2047?CS_ID_LITERAL:CS_ID_ATOM;
    return node(w,kind,index,0,0,c->bits,&w->registers[c->destination]);
  }
  if(event==CS_SOURCE_INPUT){
    const CSOsdiPrecisionInput *q=&p->inputs[index];
    if(q->kind==CS_OSDI_PRECISION_RAW_DIFFERENCE){
      uint32_t a,b;int status=coordinate(w,q->index,&a);if(status)return status;
      status=coordinate(w,q->aux,&b);if(status)return status;
      if(a==b){w->registers[q->destination]=0;return 0;}
      if(!b){w->registers[q->destination]=a;return 0;}
      return node(w,CS_ID_SUB,index,a,b,0,&w->registers[q->destination]);
    }
    if(q->kind==11){w->registers[q->destination]=w->roots[q->index];return 0;}
    if(p->registers[q->destination].type==1 && q->kind==1){
      uint32_t fact=p->fact_index[q->index];
      const CSOsdiPrecisionParameter *parameter=&p->parameters[q->index];
      uint64_t bits=0;int literal=0;
      if(fact!=CS_ID_NONE && p->facts[fact].given){bits=p->facts[fact].bits;literal=1;}
      else if(parameter->flags&CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT){
        uint32_t peer=p->model_peers[q->index];
        uint32_t peer_fact=peer==CS_ID_NONE?CS_ID_NONE:p->fact_index[peer];
        bits=parameter->scope==2 && peer_fact!=CS_ID_NONE && p->facts[peer_fact].given
          ?p->facts[peer_fact].bits:parameter->primitive_default_bits;literal=1;
      }else if(q->aux!=CS_ID_NONE){w->registers[q->destination]=w->roots[q->aux];return 0;}
      else literal=1;
      if(literal && ((bits>>52)&2047)!=2047)
        return node(w,CS_ID_LITERAL,index,0,0,bits,&w->registers[q->destination]);
    }
    if(q->kind==3 && ((p->setup->temperature_binary64>>52)&2047)!=2047)
      return node(w,CS_ID_LITERAL,index,0,0,p->setup->temperature_binary64,&w->registers[q->destination]);
    /* Source setup facts above are exact bits. A singleton range coordinate,
       time, native cache or rounded interval is never a coefficient. */
    return node(w,CS_ID_ATOM,index,0,0,0,&w->registers[q->destination]);
  }
  if(event!=CS_SOURCE_INSTRUCTION)return CS_IDENTITY_INVALID;
  const CSOsdiPrecisionInstruction *i=&p->instructions[index];
  uint32_t a=w->registers[i->a],b=i->b==CS_ID_NONE?0:w->registers[i->b];
  if(i->opcode==12){w->registers[i->destination]=a;return 0;}
  if(i->opcode==74 && !p->values[i->a].failure){
    w->registers[i->destination]=w->registers[p->values[i->a].integer?i->b:i->aux];return 0;
  }
  uint32_t kind=CS_ID_ATOM;
  if(p->registers[i->destination].type==1){
    if(i->opcode==3)kind=CS_ID_NEG;
    else if(i->opcode==44)kind=CS_ID_ADD;
    else if(i->opcode==45)kind=CS_ID_SUB;
    else if(i->opcode==46)kind=CS_ID_MUL;
  }
  /* Every executed opaque occurrence is a fresh atom. Copies/phi preserve the
     exact version; reusing a source register after a loop cannot merge values. */
  return node(w,kind,index,a,b,0,&w->registers[i->destination]);
}
static uint32_t raw_root(cs_precision_constitutive_workspace *w,uint32_t kind,uint32_t row){
  const cs_precision_program *p=w->program;
  for(uint32_t i=0;i<p->header->roots.count;++i){
    if(!cs_precision_charge(&w->program->math,1))return CS_ID_NONE;
    const CSOsdiPrecisionRoot *r=&p->roots[i];
    if(r->kind==kind&&r->index==row)return w->roots[i];
  }
  return 0;
}
static void mark(cs_precision_constitutive_workspace *w,uint32_t id,uint32_t flag){w->nodes[id].needed|=flag;}
int cs_constitutive_prepare(cs_precision_constitutive_workspace *w){
  cs_precision_program *p=w->program;
  for(uint32_t i=0;i<=w->node_count;++i){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    w->nodes[i].needed=0;w->atom_index[i]=CS_ID_NONE;
    if(w->weight_live[i])mpq_set_ui(w->weights[i],0,1);
    if(w->constant_live[i])mpq_set_ui(w->constant_values[i],0,1);
  }
  for(uint32_t row=0;row<p->header->raw_node_count;++row){
    uint32_t f=raw_root(w,CS_OSDI_PRECISION_ROOT_F,row);
    if(f==CS_ID_NONE)return p->math.status;
    uint32_t q=raw_root(w,CS_OSDI_PRECISION_ROOT_Q,row);
    if(q==CS_ID_NONE)return p->math.status;
    mark(w,f,1);mark(w,q,1);
  }
  for(uint32_t id=w->node_count+1;id-->0;){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    cs_precision_constitutive_node *n=&w->nodes[id];
    if(!n->needed)continue;
    if(n->constant){mark(w,id,2);if(n->kind!=CS_ID_LITERAL){mark(w,n->a,2);if(n->kind!=CS_ID_NEG)mark(w,n->b,2);}continue;}
    if(n->kind==CS_ID_ADD||n->kind==CS_ID_SUB){mark(w,n->a,1);mark(w,n->b,1);}
    else if(n->kind==CS_ID_NEG)mark(w,n->a,1);
    else if(n->kind==CS_ID_MUL&&(w->nodes[n->a].constant||w->nodes[n->b].constant)){
      mark(w,n->a,w->nodes[n->a].constant?2:1);mark(w,n->b,w->nodes[n->b].constant?2:1);
    }else{
      if(w->atom_count==w->limits.maximum_atoms)return CS_IDENTITY_CAPACITY;
      w->atoms[w->atom_count]=id;w->atom_index[id]=++w->atom_count;
    }
  }
  for(uint32_t id=0;id<=w->node_count;++id){
    if(!cs_precision_charge(&p->math,1))return p->math.status;
    cs_precision_constitutive_node *n=&w->nodes[id];if(!(n->needed&2))continue;
    int status=cs_constitutive_touch(w,w->constant_values[id]);if(status)return status;
    if(n->kind==CS_ID_LITERAL)status=cs_constitutive_set_bits(w,w->constant_values[id],n->bits);
    else if(n->kind==CS_ID_NEG){
      status=cs_constitutive_scalar_valid(w,w->constant_values[n->a]);
      if(!status)mpq_neg(w->constant_values[id],w->constant_values[n->a]);
    }
    else if(n->kind==CS_ID_ADD||n->kind==CS_ID_SUB)
      status=cs_constitutive_add(w,w->constant_values[id],w->constant_values[n->a],w->constant_values[n->b],n->kind==CS_ID_SUB);
    else if(n->kind==CS_ID_MUL)status=cs_constitutive_mul(w,w->constant_values[id],w->constant_values[n->a],w->constant_values[n->b]);
    else return CS_IDENTITY_INVALID;
    if(status)return status;
  }
  return 0;
}
int cs_constitutive_expand(cs_precision_constitutive_workspace *w,uint32_t kind,
    const mpq_t *coefficients,mpq_t *out){
  cs_precision_program *p=w->program;int status=0;mpq_t product;mpq_init(product);
  for(uint32_t i=0;i<=w->node_count;++i){
    if(!cs_precision_charge(&p->math,1)){status=p->math.status;goto done;}if(w->weight_live[i])mpq_set_ui(w->weights[i],0,1);
  }
  for(uint32_t i=0;i<=w->atom_count;++i){status=cs_constitutive_touch(w,out[i]);if(status)goto done;mpq_set_ui(out[i],0,1);}
  for(uint32_t row=0;row<p->header->raw_node_count;++row){
    uint32_t root=raw_root(w,kind,row);
    if(root==CS_ID_NONE){status=p->math.status;goto done;}
    status=cs_constitutive_add(w,w->weights[root],w->weights[root],coefficients[row],0);if(status)goto done;
  }
  for(uint32_t id=w->node_count+1;id-->0;){
    if(!cs_precision_charge(&p->math,1)){status=p->math.status;goto done;}
    if(!w->weight_live[id] || !mpq_sgn(w->weights[id]))continue;
    cs_precision_constitutive_node *n=&w->nodes[id];
    if(n->constant){
      status=cs_constitutive_mul(w,product,w->weights[id],w->constant_values[id]);if(status)goto done;
      status=cs_constitutive_add(w,out[0],out[0],product,0);
    }else if(n->kind==CS_ID_ADD||n->kind==CS_ID_SUB){
      status=cs_constitutive_add(w,w->weights[n->a],w->weights[n->a],w->weights[id],0);if(status)goto done;
      status=cs_constitutive_add(w,w->weights[n->b],w->weights[n->b],w->weights[id],n->kind==CS_ID_SUB);
    }else if(n->kind==CS_ID_NEG)status=cs_constitutive_add(w,w->weights[n->a],w->weights[n->a],w->weights[id],1);
    else if(n->kind==CS_ID_MUL&&(w->nodes[n->a].constant||w->nodes[n->b].constant)){
      uint32_t c=w->nodes[n->a].constant?n->a:n->b,v=c==n->a?n->b:n->a;
      status=cs_constitutive_mul(w,product,w->weights[id],w->constant_values[c]);if(status)goto done;
      status=cs_constitutive_add(w,w->weights[v],w->weights[v],product,0);
    }else{
      uint32_t column=w->atom_index[id];if(column==CS_ID_NONE||column>w->atom_count){status=CS_IDENTITY_INVALID;goto done;}
      status=cs_constitutive_add(w,out[column],out[column],w->weights[id],0);
    }
    if(status)goto done;
  }
done:mpq_clear(product);return status;
}
