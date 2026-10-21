#include "IdentityInternal.h"

/* Exact intersection inside the admitted source-linear representation. The
   independent checker never trusts this elimination or its dimension result. */
int cs_constitutive_intersect(cs_precision_constitutive_workspace *w,uint32_t kind,
    mpq_t *basis,uint32_t count,uint32_t *new_count){
  uint32_t n=w->program->header->raw_node_count;
  if(count>n||!new_count)return CS_IDENTITY_INVALID;
  size_t width=(size_t)w->limits.maximum_atoms+1;int status=0;
  mpq_t left,right,pivot,value,sum;mpq_inits(left,right,pivot,value,sum,NULL);
  for(uint32_t row=0;row<count;++row){
    for(uint32_t col=0;col<count;++col){
      if(!cs_precision_charge(&w->program->math,1)){status=w->program->math.status;goto done;}
      status=cs_constitutive_touch(w,w->relations[(size_t)row*n+col]);if(status)goto done;
      mpq_set_ui(w->relations[(size_t)row*n+col],row==col,1);
    }
    status=cs_constitutive_expand(w,kind,(const mpq_t *)(basis+(size_t)row*n),w->matrix+(size_t)row*width);
    if(status)goto done;
  }
  uint32_t pivot_row=0;
  for(uint32_t col=0;col<=w->atom_count&&pivot_row<count;++col){
    uint32_t selected=pivot_row;
    while(selected<count&&!mpq_sgn(w->matrix[(size_t)selected*width+col])){
      if(!cs_precision_charge(&w->program->math,1)){status=w->program->math.status;goto done;}selected++;
    }
    if(selected==count)continue;
    if(selected!=pivot_row){
      for(uint32_t c=0;c<=w->atom_count;++c){
        if(!cs_precision_charge(&w->program->math,1)){status=w->program->math.status;goto done;}
        mpq_swap(w->matrix[(size_t)selected*width+c],w->matrix[(size_t)pivot_row*width+c]);
      }
      for(uint32_t c=0;c<count;++c)mpq_swap(w->relations[(size_t)selected*n+c],w->relations[(size_t)pivot_row*n+c]);
    }
    mpq_set(pivot,w->matrix[(size_t)pivot_row*width+col]);
    for(uint32_t row=pivot_row+1;row<count;++row){
      if(!cs_precision_charge(&w->program->math,1)){status=w->program->math.status;goto done;}
      mpq_set(value,w->matrix[(size_t)row*width+col]);if(!mpq_sgn(value))continue;
      for(uint32_t c=col;c<=w->atom_count;++c){
        status=cs_constitutive_mul(w,left,w->matrix[(size_t)row*width+c],pivot);if(status)goto done;
        status=cs_constitutive_mul(w,right,w->matrix[(size_t)pivot_row*width+c],value);if(status)goto done;
        status=cs_constitutive_add(w,w->matrix[(size_t)row*width+c],left,right,1);if(status)goto done;
      }
      for(uint32_t c=0;c<count;++c){
        status=cs_constitutive_mul(w,left,w->relations[(size_t)row*n+c],pivot);if(status)goto done;
        status=cs_constitutive_mul(w,right,w->relations[(size_t)pivot_row*n+c],value);if(status)goto done;
        status=cs_constitutive_add(w,w->relations[(size_t)row*n+c],left,right,1);if(status)goto done;
      }
    }
    pivot_row++;
  }
  uint32_t dimension=count-pivot_row;
  for(uint32_t row=0;row<dimension;++row){
    for(uint32_t c=0;c<=w->atom_count;++c)if(mpq_sgn(w->matrix[(size_t)(row+pivot_row)*width+c])){status=CS_IDENTITY_INVALID;goto done;}
    for(uint32_t col=0;col<n;++col){
      mpq_set_ui(sum,0,1);
      for(uint32_t i=0;i<count;++i){
        status=cs_constitutive_mul(w,left,w->relations[(size_t)(row+pivot_row)*n+i],basis[(size_t)i*n+col]);if(status)goto done;
        status=cs_constitutive_add(w,sum,sum,left,0);if(status)goto done;
      }
      status=cs_constitutive_touch(w,w->composed[(size_t)row*n+col]);if(status)goto done;
      mpq_set(w->composed[(size_t)row*n+col],sum);
    }
  }
  /* No old basis coefficient is overwritten until every composed row exists. */
  for(uint32_t row=0;row<dimension;++row)for(uint32_t col=0;col<n;++col){
    status=cs_constitutive_scalar_valid(w,w->composed[(size_t)row*n+col]);if(status)goto done;
    mpq_set(basis[(size_t)row*n+col],w->composed[(size_t)row*n+col]);
  }
  *new_count=dimension;
done:mpq_clears(left,right,pivot,value,sum,NULL);return status;
}
