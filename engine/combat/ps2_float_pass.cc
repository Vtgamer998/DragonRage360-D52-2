// SPDX-License-Identifier: GPL-3.0-or-later
// Hard-float calls with software PS2 arithmetic, before SSA/optimization.
// Uses GCC's documented pass API. Does not change compiler/ELF attributes.
#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "tree-pass.h"
#include "context.h"
#include "function.h"
#include "basic-block.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-expr.h"
#include "diagnostic.h"
#include "fold-const.h"
int plugin_is_GPL_compatible;
namespace {
bool real(tree x) { return x && TREE_CODE(TREE_TYPE(x))==REAL_TYPE; }
tree bt(tree x) { return TYPE_PRECISION(TREE_TYPE(x))==32?uint32_type_node:uint64_type_node; }
tree temporary(gimple_stmt_iterator *it,tree type,tree value) {
    tree v=create_tmp_var(type,"dr_ps2");
    gsi_insert_before(it,gimple_build_assign(v,value),GSI_SAME_STMT);return v;
}
tree bitcast(gimple_stmt_iterator *it,tree x) {
    return temporary(it,bt(x),build1(VIEW_CONVERT_EXPR,bt(x),x));
}
tree call(gimple_stmt_iterator *it,const char *name,tree type,tree a,tree b=NULL_TREE) {
    tree ft=b?build_function_type_list(type,TREE_TYPE(a),TREE_TYPE(b),NULL_TREE)
             :build_function_type_list(type,TREE_TYPE(a),NULL_TREE);
    tree decl=build_fn_decl(name,ft);TREE_PUBLIC(decl)=1;DECL_EXTERNAL(decl)=1;
    gcall *s=b?gimple_build_call(decl,2,a,b):gimple_build_call(decl,1,a);
    tree v=create_tmp_var(type,"dr_call");gimple_call_set_lhs(s,v);
    gsi_insert_before(it,s,GSI_SAME_STMT);return v;
}
tree comparison(gimple_stmt_iterator *it,tree_code code,tree a,tree b) {
    if(TYPE_PRECISION(TREE_TYPE(a))==64) {
        const char *name=nullptr;
        switch(code) {
        case EQ_EXPR:name="dr_ps2_eqdf_bits";break;case NE_EXPR:name="dr_ps2_nedf_bits";break;
        case LT_EXPR:name="dr_ps2_ltdf_bits";break;case LE_EXPR:name="dr_ps2_ledf_bits";break;
        case GT_EXPR:name="dr_ps2_gtdf_bits";break;case GE_EXPR:name="dr_ps2_gedf_bits";break;
        case UNORDERED_EXPR:name="dr_ps2_unorddf_bits";break;case ORDERED_EXPR:name="dr_ps2_orddf_bits";break;
        default:error("PS2 lowering: unsupported double comparison");return boolean_false_node;
        }
        tree v=call(it,name,integer_type_node,bitcast(it,a),bitcast(it,b));
        return temporary(it,boolean_type_node,build2(NE_EXPR,boolean_type_node,v,integer_zero_node));
    }
    if(code==UNORDERED_EXPR)return boolean_false_node;
    if(code==ORDERED_EXPR)return boolean_true_node;
    switch(code) {
    case UNEQ_EXPR:code=EQ_EXPR;break;case LTGT_EXPR:code=NE_EXPR;break;
    case UNLT_EXPR:code=LT_EXPR;break;case UNLE_EXPR:code=LE_EXPR;break;
    case UNGT_EXPR:code=GT_EXPR;break;case UNGE_EXPR:code=GE_EXPR;break;default:break;
    }
    tree cmp=call(it,"dr_ps2_cmpsf_bits",integer_type_node,bitcast(it,a),bitcast(it,b));
    return temporary(it,boolean_type_node,build2(code,boolean_type_node,cmp,integer_zero_node));
}
tree lower(gimple_stmt_iterator *it,gassign *s) {
    tree_code code=gimple_assign_rhs_code(s);
    tree dst=gimple_assign_lhs(s),a=gimple_assign_rhs1(s);
    tree b=gimple_num_ops(s)>2?gimple_assign_rhs2(s):NULL_TREE;
    bool ar=real(a),dr=real(dst);
    if(ar && b && real(b) && TREE_CODE_CLASS(code)==tcc_comparison)
        return fold_convert(TREE_TYPE(dst),comparison(it,code,a,b));
    if(ar && dr) {
        bool sf=TYPE_PRECISION(TREE_TYPE(a))==32;
        tree v=NULL_TREE;const char *name=nullptr;
        switch(code) {
        case PLUS_EXPR:name=sf?"dr_ps2_addsf_bits":"dr_ps2_adddf_bits";break;
        case MINUS_EXPR:name=sf?"dr_ps2_subsf_bits":"dr_ps2_subdf_bits";break;
        case MULT_EXPR:name=sf?"dr_ps2_mulsf_bits":"dr_ps2_muldf_bits";break;
        case RDIV_EXPR:name=sf?"dr_ps2_divsf_bits":"dr_ps2_divdf_bits";break;
        case NEGATE_EXPR:case ABS_EXPR: {
            tree mask=build_int_cstu(bt(a),sf?0x80000000ULL:0x8000000000000000ULL);
            if(code==ABS_EXPR)mask=fold_build1(BIT_NOT_EXPR,bt(a),mask);
            v=temporary(it,bt(a),build2(code==ABS_EXPR?BIT_AND_EXPR:BIT_XOR_EXPR,bt(a),bitcast(it,a),mask));break;
        }
        case NOP_EXPR:case CONVERT_EXPR:
            if(TYPE_PRECISION(TREE_TYPE(a))==TYPE_PRECISION(TREE_TYPE(dst)))return NULL_TREE;
            v=call(it,sf?"dr_ps2_extendsfdf_bits":"dr_ps2_truncdfsf_bits",bt(dst),bitcast(it,a));break;
        default:
            if(gimple_assign_single_p(s))return NULL_TREE;
            error_at(gimple_location(s),"PS2 lowering: unsupported real operation %s",get_tree_code_name(code));return NULL_TREE;
        }
        if(name)v=call(it,name,bt(a),bitcast(it,a),bitcast(it,b));
        return build1(VIEW_CONVERT_EXPR,TREE_TYPE(dst),v);
    }
    if(code==FIX_TRUNC_EXPR && ar && INTEGRAL_TYPE_P(TREE_TYPE(dst))) {
        if(TYPE_PRECISION(TREE_TYPE(dst))>32){error_at(gimple_location(s),"PS2 lowering: conversion to integer wider than 32 bits");return NULL_TREE;}
        bool sf=TYPE_PRECISION(TREE_TYPE(a))==32,uns=TYPE_UNSIGNED(TREE_TYPE(dst));
        const char *fn=sf?(uns?"dr_ps2_fixunssf_bits":"dr_ps2_fixsf_bits"):(uns?"dr_ps2_fixunsdf_bits":"dr_ps2_fixdf_bits");
        return fold_convert(TREE_TYPE(dst),call(it,fn,uns?uint32_type_node:integer_type_node,bitcast(it,a)));
    }
    if(code==FLOAT_EXPR && dr && INTEGRAL_TYPE_P(TREE_TYPE(a))) {
        if(TYPE_PRECISION(TREE_TYPE(a))>32){error_at(gimple_location(s),"PS2 lowering: conversion from integer wider than 32 bits");return NULL_TREE;}
        bool sf=TYPE_PRECISION(TREE_TYPE(dst))==32,uns=TYPE_UNSIGNED(TREE_TYPE(a));
        tree t=uns?uint32_type_node:integer_type_node;
        tree x=temporary(it,t,fold_convert(t,a));
        const char *fn=sf?(uns?"dr_ps2_floatunssf_bits":"dr_ps2_floatsf_bits"):(uns?"dr_ps2_floatunsdf_bits":"dr_ps2_floatdf_bits");
        return build1(VIEW_CONVERT_EXPR,TREE_TYPE(dst),call(it,fn,bt(dst),x));
    }
    return NULL_TREE;
}
const pass_data data={GIMPLE_PASS,"drps2float",OPTGROUP_NONE,TV_NONE,PROP_gimple_any,0,0,0,0};
class ps2_pass:public gimple_opt_pass {
public:ps2_pass(gcc::context *c):gimple_opt_pass(data,c){}
unsigned execute(function *f) override {
    basic_block bb;
    FOR_EACH_BB_FN(bb,f)for(gimple_stmt_iterator it=gsi_start_bb(bb);!gsi_end_p(it);gsi_next(&it)) {
        gimple *s=gsi_stmt(it);
        if(is_gimple_assign(s)) {
            tree v=lower(&it,as_a<gassign *>(s));
            if(v){gassign *r=gimple_build_assign(gimple_assign_lhs(s),v);gimple_set_location(r,gimple_location(s));gsi_replace(&it,r,true);}
        } else if(gimple_code(s)==GIMPLE_COND && real(gimple_cond_lhs(s))) {
            tree v=comparison(&it,gimple_cond_code(s),gimple_cond_lhs(s),gimple_cond_rhs(s));
            gcond *c=as_a<gcond *>(s);gimple_cond_set_code(c,NE_EXPR);gimple_cond_set_lhs(c,v);
            gimple_cond_set_rhs(c,build_zero_cst(TREE_TYPE(v)));
        }
    }
    return 0;
}
};
}
int plugin_init(plugin_name_args *args,plugin_gcc_version *version) {
    if(!plugin_default_version_check(version,&gcc_version))return 1;
    register_pass_info info={new ps2_pass(g),"cfg",1,PASS_POS_INSERT_AFTER};
    register_callback(args->base_name,PLUGIN_PASS_MANAGER_SETUP,nullptr,&info);return 0;
}
