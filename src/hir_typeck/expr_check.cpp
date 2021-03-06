/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * hir_typeck/expr_check.cpp
 * - Expression type checking
 */
#include <hir/visitor.hpp>
#include <hir/expr.hpp>
#include <hir_typeck/static.hpp>
#include "main_bindings.hpp"
#include <algorithm>

namespace {
    typedef ::std::vector< ::std::pair< ::HIR::Pattern, ::HIR::TypeRef> >   t_args;
    // -----------------------------------------------------------------------
    // Enumeration visitor
    // 
    // Iterates the HIR expression tree and extracts type "equations"
    // -----------------------------------------------------------------------
    class ExprVisitor_Validate:
        public ::HIR::ExprVisitor
    {
        const StaticTraitResolve&  m_resolve;
        //const t_args&   m_args;
        const ::HIR::TypeRef&   ret_type;
        ::std::vector< const ::HIR::TypeRef*>   closure_ret_types;
        
    public:
        ExprVisitor_Validate(const StaticTraitResolve& res, const t_args& args, const ::HIR::TypeRef& ret_type):
            m_resolve(res),
            //m_args(args),
            ret_type(ret_type)
        {
        }
        
        void visit_root(::HIR::ExprNode& node)
        {
            node.visit(*this);
            check_types_equal(node.span(), ret_type, node.m_res_type);
        }
        
        void visit(::HIR::ExprNode_Block& node) override
        {
            TRACE_FUNCTION_F(&node << " { ... }");
            for(auto& n : node.m_nodes)
            {
                n->visit(*this);
            }
            if( node.m_nodes.size() > 0 )
            {
                check_types_equal(node.span(), node.m_res_type, node.m_nodes.back()->m_res_type);
            }
        }
        void visit(::HIR::ExprNode_Return& node) override
        {
            TRACE_FUNCTION_F(&node << " return ...");
            // Check against return type
            const auto& ret_ty = ( this->closure_ret_types.size() > 0 ? *this->closure_ret_types.back() : this->ret_type );
            check_types_equal(ret_ty, node.m_value);
            node.m_value->visit(*this);
        }
        void visit(::HIR::ExprNode_Loop& node) override
        {
            TRACE_FUNCTION_F(&node << " loop { ... }");
            node.m_code->visit(*this);
        }
        void visit(::HIR::ExprNode_LoopControl& node) override
        {
            //TRACE_FUNCTION_F(&node << " " << (node.m_continue ? "continue" : "break") << " '" << node.m_label);
        }
        void visit(::HIR::ExprNode_Let& node) override
        {
            TRACE_FUNCTION_F(&node << " let " << node.m_pattern << ": " << node.m_type);
            if(node.m_value)
            {
                check_types_equal(node.span(), node.m_type, node.m_value->m_res_type);
                node.m_value->visit(*this);
            }
        }
        void visit(::HIR::ExprNode_Match& node) override
        {
            TRACE_FUNCTION_F(&node << " match ...");
            node.m_value->visit(*this);
            for(auto& arm : node.m_arms)
            {
                check_types_equal(node.span(), node.m_res_type, arm.m_code->m_res_type);
                arm.m_code->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_If& node) override
        {
            TRACE_FUNCTION_F(&node << " if ... { ... } else { ... }");
            node.m_cond->visit( *this );
            check_types_equal(node.span(), node.m_res_type, node.m_true->m_res_type);
            if( node.m_false )
            {
                check_types_equal(node.span(), node.m_res_type, node.m_false->m_res_type);
            }
        }
        void visit(::HIR::ExprNode_Assign& node) override
        {
            TRACE_FUNCTION_F(&node << "... ?= ...");
            
            if( node.m_op == ::HIR::ExprNode_Assign::Op::None ) {
                check_types_equal(node.span(), node.m_slot->m_res_type, node.m_value->m_res_type);
            }
            else {
                // Type inferrence using the +=
                // - "" as type name to indicate that it's just using the trait magic?
                const char *lang_item = nullptr;
                switch( node.m_op )
                {
                case ::HIR::ExprNode_Assign::Op::None:  throw "";
                case ::HIR::ExprNode_Assign::Op::Add: lang_item = "add_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Sub: lang_item = "sub_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Mul: lang_item = "mul_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Div: lang_item = "div_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Mod: lang_item = "rem_assign"; break;
                case ::HIR::ExprNode_Assign::Op::And: lang_item = "bitand_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Or : lang_item = "bitor_assign" ; break;
                case ::HIR::ExprNode_Assign::Op::Xor: lang_item = "bitxor_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Shr: lang_item = "shl_assign"; break;
                case ::HIR::ExprNode_Assign::Op::Shl: lang_item = "shr_assign"; break;
                }
                assert(lang_item);
                const auto& trait_path = this->get_lang_item_path(node.span(), lang_item);
                
                check_associated_type(node.span(),  ::HIR::TypeRef(),  trait_path, ::make_vec1(node.m_value->m_res_type.clone()), node.m_slot->m_res_type,  "");
            }
            
            node.m_slot->visit( *this );
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_BinOp& node) override
        {
            TRACE_FUNCTION_F(&node << "... "<<::HIR::ExprNode_BinOp::opname(node.m_op)<<" ...");
            
            switch(node.m_op)
            {
            case ::HIR::ExprNode_BinOp::Op::CmpEqu:
            case ::HIR::ExprNode_BinOp::Op::CmpNEqu:
            case ::HIR::ExprNode_BinOp::Op::CmpLt:
            case ::HIR::ExprNode_BinOp::Op::CmpLtE:
            case ::HIR::ExprNode_BinOp::Op::CmpGt:
            case ::HIR::ExprNode_BinOp::Op::CmpGtE: {
                check_types_equal(node.span(), ::HIR::TypeRef(::HIR::CoreType::Bool), node.m_res_type);
                
                const char* item_name = nullptr;
                switch(node.m_op)
                {
                case ::HIR::ExprNode_BinOp::Op::CmpEqu:  item_name = "eq";  break;
                case ::HIR::ExprNode_BinOp::Op::CmpNEqu: item_name = "eq";  break;
                case ::HIR::ExprNode_BinOp::Op::CmpLt:   item_name = "ord"; break;
                case ::HIR::ExprNode_BinOp::Op::CmpLtE:  item_name = "ord"; break;
                case ::HIR::ExprNode_BinOp::Op::CmpGt:   item_name = "ord"; break;
                case ::HIR::ExprNode_BinOp::Op::CmpGtE:  item_name = "ord"; break;
                default: break;
                }
                assert(item_name);
                const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                
                check_associated_type(node.span(),  ::HIR::TypeRef(),  op_trait, ::make_vec1(node.m_right->m_res_type.clone()), node.m_left->m_res_type,  "");
                break; }
            
            case ::HIR::ExprNode_BinOp::Op::BoolAnd:
            case ::HIR::ExprNode_BinOp::Op::BoolOr:
                // No validation needed, result forced in typeck
                break;
            default: {
                const char* item_name = nullptr;
                switch(node.m_op)
                {
                case ::HIR::ExprNode_BinOp::Op::CmpEqu:  throw "";
                case ::HIR::ExprNode_BinOp::Op::CmpNEqu: throw "";
                case ::HIR::ExprNode_BinOp::Op::CmpLt:   throw "";
                case ::HIR::ExprNode_BinOp::Op::CmpLtE:  throw "";
                case ::HIR::ExprNode_BinOp::Op::CmpGt:   throw "";
                case ::HIR::ExprNode_BinOp::Op::CmpGtE:  throw "";
                case ::HIR::ExprNode_BinOp::Op::BoolAnd: throw "";
                case ::HIR::ExprNode_BinOp::Op::BoolOr:  throw "";

                case ::HIR::ExprNode_BinOp::Op::Add: item_name = "add"; break;
                case ::HIR::ExprNode_BinOp::Op::Sub: item_name = "sub"; break;
                case ::HIR::ExprNode_BinOp::Op::Mul: item_name = "mul"; break;
                case ::HIR::ExprNode_BinOp::Op::Div: item_name = "div"; break;
                case ::HIR::ExprNode_BinOp::Op::Mod: item_name = "rem"; break;
                
                case ::HIR::ExprNode_BinOp::Op::And: item_name = "bitand"; break;
                case ::HIR::ExprNode_BinOp::Op::Or:  item_name = "bitor";  break;
                case ::HIR::ExprNode_BinOp::Op::Xor: item_name = "bitxor"; break;
                
                case ::HIR::ExprNode_BinOp::Op::Shr: item_name = "shr"; break;
                case ::HIR::ExprNode_BinOp::Op::Shl: item_name = "shl"; break;
                }
                assert(item_name);
                const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                
                check_associated_type(node.span(),  node.m_res_type,  op_trait, ::make_vec1(node.m_right->m_res_type.clone()), node.m_left->m_res_type,  "Output");
                break; }
            }
            
            node.m_left ->visit( *this );
            node.m_right->visit( *this );
        }
        
        void visit(::HIR::ExprNode_UniOp& node) override
        {
            TRACE_FUNCTION_F(&node << " " << ::HIR::ExprNode_UniOp::opname(node.m_op) << "...");
            switch(node.m_op)
            {
            case ::HIR::ExprNode_UniOp::Op::Invert:
                check_associated_type(node.span(), node.m_res_type,  this->get_lang_item_path(node.span(), "not"), {}, node.m_value->m_res_type, "Output");
                break;
            case ::HIR::ExprNode_UniOp::Op::Negate:
                check_associated_type(node.span(), node.m_res_type,  this->get_lang_item_path(node.span(), "neg"), {}, node.m_value->m_res_type, "Output");
                break;
            }
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_Borrow& node) override
        {
            TRACE_FUNCTION_F(&node << " &_ ...");
            check_types_equal(node.span(), node.m_res_type, ::HIR::TypeRef::new_borrow(node.m_type, node.m_value->m_res_type.clone()));
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_Index& node) override
        {
            TRACE_FUNCTION_F(&node << " ... [ ... ]");
            check_associated_type(node.span(),
                node.m_res_type,
                this->get_lang_item_path(node.span(), "index"), ::make_vec1(node.m_index->m_res_type.clone()), node.m_value->m_res_type, "Target"
                );
            
            node.m_value->visit( *this );
            node.m_index->visit( *this );
        }
        
        void visit(::HIR::ExprNode_Cast& node) override
        {
            TRACE_FUNCTION_F(&node << " ... as " << node.m_res_type);
            const Span& sp = node.span();
            
            const auto& src_ty = node.m_value->m_res_type;
            const auto& dst_ty = node.m_res_type;
            // Check castability
            TU_MATCH_DEF(::HIR::TypeRef::Data, (dst_ty.m_data), (de),
            (
                ERROR(sp, E0000, "Invalid cast to " << dst_ty);
                ),
            (Pointer,
                TU_MATCH_DEF(::HIR::TypeRef::Data, (src_ty.m_data), (se),
                (
                    ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                    ),
                (Pointer,
                    // TODO: Sized check - can't cast to a fat pointer from a thin one
                    ),
                (Primitive,
                    if( se != ::HIR::CoreType::Usize ) {
                        ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                    }
                    ),
                (Function,
                    if( de.type != ::HIR::BorrowType::Shared || *de.inner != ::HIR::TypeRef::new_unit() ) {
                        ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                    }
                    ),
                (Borrow,
                    this->check_types_equal(sp, *de.inner, *se.inner);
                    )
                )
                ),
            (Primitive,
                // TODO: Check cast to primitive
                )
            )
            
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_Unsize& node) override
        {
            TRACE_FUNCTION_F(&node << " ... : " << node.m_res_type);
            const Span& sp = node.span();
            
            const auto& src_ty = node.m_value->m_res_type;
            const auto& dst_ty = node.m_res_type;
            // Check unsizability (including trait impls)
            // NOTE: Unsize applies inside borrows
            TU_MATCH_DEF(::HIR::TypeRef::Data, (dst_ty.m_data), (e),
            (
                ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty);
                ),
            (TraitObject,
                ),
            (Slice,
                // TODO: Does unsize ever apply to arrays? - Yes.
                )
            )
            
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_Deref& node) override
        {
            TRACE_FUNCTION_F(&node << " *...");
            check_associated_type(node.span(),
                node.m_res_type,
                this->get_lang_item_path(node.span(), "deref"), {}, node.m_value->m_res_type, "Target"
                );

            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_TupleVariant& node) override
        {
            TRACE_FUNCTION_F(&node << " " << node.m_path << "(...,) [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            
            // - Create ivars in path, and set result type
            const auto& ty = node.m_res_type;
            
            const ::HIR::t_tuple_fields* fields_ptr = nullptr;
            ASSERT_BUG(sp, ty.m_data.is_Path(), "Result type of _TupleVariant isn't Path");
            TU_MATCH(::HIR::TypeRef::TypePathBinding, (ty.m_data.as_Path().binding), (e),
            (Unbound, ),
            (Opaque, ),
            (Enum,
                const auto& var_name = node.m_path.m_path.m_components.back();
                const auto& enm = *e;
                auto it = ::std::find_if(enm.m_variants.begin(), enm.m_variants.end(), [&](const auto&v)->auto{ return v.first == var_name; });
                assert(it != enm.m_variants.end());
                ASSERT_BUG(sp, it->second.is_Tuple(), "Pointed variant of TupleVariant (" << node.m_path << ") isn't a Tuple");
                fields_ptr = &it->second.as_Tuple();
                ),
            (Struct,
                ASSERT_BUG(sp, e->m_data.is_Tuple(), "Pointed struct in TupleVariant (" << node.m_path << ") isn't a Tuple");
                fields_ptr = &e->m_data.as_Tuple();
                )
            )
            assert(fields_ptr);
            const ::HIR::t_tuple_fields& fields = *fields_ptr;
            ASSERT_BUG(sp, fields.size() == node.m_args.size(), "");
            
            // Bind fields with type params (coercable)
            // TODO: Remove use of m_arg_types (maybe assert that cache is correct?)
            for( unsigned int i = 0; i < node.m_args.size(); i ++ )
            {
                const auto& des_ty_r = fields[i].ent;
                const auto* des_ty = &des_ty_r;
                if( monomorphise_type_needed(des_ty_r) ) {
                    assert( node.m_arg_types[i] != ::HIR::TypeRef() );
                    des_ty = &node.m_arg_types[i];
                }
                
                check_types_equal(*des_ty, node.m_args[i]);
            }
            
            for( auto& val : node.m_args ) {
                val->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_StructLiteral& node) override
        {
            TRACE_FUNCTION_F(&node << " " << node.m_path << "{...} [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            if( node.m_base_value) {
                check_types_equal( node.m_base_value->span(), node.m_res_type, node.m_base_value->m_res_type );
            }
            
            // - Create ivars in path, and set result type
            const auto& ty = node.m_res_type;
            ASSERT_BUG(sp, ty.m_data.is_Path(), "Result type of _StructLiteral isn't Path");
            
            const ::HIR::t_struct_fields* fields_ptr = nullptr;
            TU_MATCH(::HIR::TypeRef::TypePathBinding, (ty.m_data.as_Path().binding), (e),
            (Unbound, ),
            (Opaque, ),
            (Enum,
                const auto& var_name = node.m_path.m_path.m_components.back();
                const auto& enm = *e;
                auto it = ::std::find_if(enm.m_variants.begin(), enm.m_variants.end(), [&](const auto&v)->auto{ return v.first == var_name; });
                assert(it != enm.m_variants.end());
                fields_ptr = &it->second.as_Struct();
                ),
            (Struct,
                fields_ptr = &e->m_data.as_Named();
                )
            )
            assert(fields_ptr);
            const ::HIR::t_struct_fields& fields = *fields_ptr;
            
            // Bind fields with type params (coercable)
            for( auto& val : node.m_values)
            {
                const auto& name = val.first;
                auto it = ::std::find_if(fields.begin(), fields.end(), [&](const auto& v)->bool{ return v.first == name; });
                assert(it != fields.end());
                const auto& des_ty_r = it->second.ent;
                auto& des_ty_cache = node.m_value_types[it - fields.begin()];
                const auto* des_ty = &des_ty_r;
                
                DEBUG(name << " : " << des_ty_r);
                if( monomorphise_type_needed(des_ty_r) ) {
                    assert( des_ty_cache != ::HIR::TypeRef() );
                    des_ty = &des_ty_cache;
                }
                check_types_equal(*des_ty, val.second);
            }
            
            for( auto& val : node.m_values ) {
                val.second->visit( *this );
            }
            if( node.m_base_value ) {
                node.m_base_value->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_UnitVariant& node) override
        {
            TRACE_FUNCTION_F(&node << " " << node.m_path << " [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            const auto& ty = node.m_res_type;
            ASSERT_BUG(sp, ty.m_data.is_Path(), "Result type of _StructLiteral isn't Path");
            
            TU_MATCH(::HIR::TypeRef::TypePathBinding, (ty.m_data.as_Path().binding), (e),
            (Unbound, ),
            (Opaque, ),
            (Enum,
                const auto& var_name = node.m_path.m_path.m_components.back();
                const auto& enm = *e;
                auto it = ::std::find_if(enm.m_variants.begin(), enm.m_variants.end(), [&](const auto&v)->auto{ return v.first == var_name; });
                assert(it != enm.m_variants.end());
                assert( it->second.is_Unit() || it->second.is_Value() );
                ),
            (Struct,
                assert( e->m_data.is_Unit() );
                )
            )
        }

        void visit(::HIR::ExprNode_CallPath& node) override
        {
            const auto& sp = node.span();
            TRACE_FUNCTION_F(&node << " " << node.m_path << "(..., )");
            
            for( auto& val : node.m_args ) {
                val->visit( *this );
            }
            
            // Do function resolution again, this time with concrete types.
            const auto& path = node.m_path;
            /*const*/ auto& cache = node.m_cache;
            
            const ::HIR::Function*  fcn_ptr = nullptr;
            ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)>    monomorph_cb;
            
            TU_MATCH(::HIR::Path::Data, (path.m_data), (e),
            (Generic,
                const auto& path_params = e.m_params;
                
                const auto& fcn = m_resolve.m_crate.get_function_by_path(sp, e.m_path);
                fcn_ptr = &fcn;
                cache.m_fcn_params = &fcn.m_params;
                
                monomorph_cb = [&](const auto& gt)->const auto& {
                        const auto& e = gt.m_data.as_Generic();
                        if( e.name == "Self" || e.binding == 0xFFFF )
                            TODO(sp, "Handle 'Self' when monomorphising");
                        if( e.binding < 256 ) {
                            BUG(sp, "Impl-level parameter on free function (#" << e.binding << " " << e.name << ")");
                        }
                        else if( e.binding < 512 ) {
                            auto idx = e.binding - 256;
                            if( idx >= path_params.m_types.size() ) {
                                BUG(sp, "Generic param out of input range - " << idx << " '"<<e.name<<"' >= " << path_params.m_types.size());
                            }
                            return path_params.m_types[idx];
                        }
                        else {
                            BUG(sp, "Generic bounding out of total range");
                        }
                    };
                ),
            (UfcsKnown,
                const auto& trait_params = e.trait.m_params;
                const auto& path_params = e.params;
                
                const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, e.trait.m_path);
                if( trait.m_values.count(e.item) == 0 ) {
                    BUG(sp, "Method '" << e.item << "' of trait " << e.trait.m_path << " doesn't exist");
                }
                
                const auto& fcn = trait.m_values.at(e.item).as_Function();
                cache.m_fcn_params = &fcn.m_params;
                cache.m_top_params = &trait.m_params;
                
                // Add a bound requiring the Self type impl the trait
                check_associated_type(sp, ::HIR::TypeRef(), e.trait.m_path, mv$(e.trait.m_params.clone().m_types), *e.type, "");
                
                fcn_ptr = &fcn;
                
                monomorph_cb = [&](const auto& gt)->const auto& {
                        const auto& ge = gt.m_data.as_Generic();
                        if( ge.binding == 0xFFFF ) {
                            return *e.type;
                        }
                        else if( ge.binding < 256 ) {
                            auto idx = ge.binding;
                            if( idx >= trait_params.m_types.size() ) {
                                BUG(sp, "Generic param (impl) out of input range - " << idx << " '"<<ge.name<<"' >= " << trait_params.m_types.size());
                            }
                            return trait_params.m_types[idx];
                        }
                        else if( ge.binding < 512 ) {
                            auto idx = ge.binding - 256;
                            if( idx >= path_params.m_types.size() ) {
                                BUG(sp, "Generic param out of input range - " << idx << " '"<<ge.name<<"' >= " << path_params.m_types.size());
                            }
                            return path_params.m_types[idx];
                        }
                        else {
                            BUG(sp, "Generic bounding out of total range");
                        }
                    };
                ),
            (UfcsUnknown,
                TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                ),
            (UfcsInherent,
                // - Locate function (and impl block)
                const ::HIR::TypeImpl* impl_ptr = nullptr;
                m_resolve.m_crate.find_type_impls(*e.type, [&](const auto& ty)->const auto& { return ty; },
                    [&](const auto& impl) {
                        DEBUG("- impl" << impl.m_params.fmt_args() << " " << impl.m_type);
                        auto it = impl.m_methods.find(e.item);
                        if( it == impl.m_methods.end() )
                            return false;
                        fcn_ptr = &it->second.data;
                        impl_ptr = &impl;
                        return true;
                    });
                if( !fcn_ptr ) {
                    ERROR(sp, E0000, "Failed to locate function " << path);
                }
                assert(impl_ptr);
                
                cache.m_fcn_params = &fcn_ptr->m_params;
                
                
                // If the impl block has parameters, figure out what types they map to
                // - The function params are already mapped (from fix_param_count)
                auto& impl_params = cache.m_ty_impl_params;
                if( impl_ptr->m_params.m_types.size() > 0 ) {
                    impl_params.m_types.resize( impl_ptr->m_params.m_types.size() );
                    impl_ptr->m_type.match_generics(sp, *e.type, [&](const auto&x)->const auto&{return x;}, [&](auto idx, const auto& ty) {
                        assert( idx < impl_params.m_types.size() );
                        impl_params.m_types[idx] = ty.clone();
                        return ::HIR::Compare::Equal;
                        });
                    for(const auto& ty : impl_params.m_types)
                        assert( !( ty.m_data.is_Infer() && ty.m_data.as_Infer().index == ~0u) );
                }
                
                // Create monomorphise callback
                const auto& fcn_params = e.params;
                monomorph_cb = [&](const auto& gt)->const auto& {
                        const auto& ge = gt.m_data.as_Generic();
                        if( ge.binding == 0xFFFF ) {
                            return *e.type;
                        }
                        else if( ge.binding < 256 ) {
                            auto idx = ge.binding;
                            if( idx >= impl_params.m_types.size() ) {
                                BUG(sp, "Generic param out of input range (impl) - " << idx << " '" << ge.name << "' >= " << impl_params.m_types.size());
                            }
                            return impl_params.m_types[idx];
                        }
                        else if( ge.binding < 512 ) {
                            auto idx = ge.binding - 256;
                            if( idx >= fcn_params.m_types.size() ) {
                                BUG(sp, "Generic param out of input range (item) - " << idx << " '" << ge.name << "' >= " << fcn_params.m_types.size());
                            }
                            return fcn_params.m_types[idx];
                        }
                        else {
                            BUG(sp, "Generic bounding out of total range");
                        }
                    };
                )
            )

            assert( fcn_ptr );
            const auto& fcn = *fcn_ptr;
            
            // --- Monomorphise the argument/return types (into current context)
            cache.m_arg_types.clear();
            for(const auto& arg : fcn.m_args) {
                DEBUG("Arg " << arg.first << ": " << arg.second);
                cache.m_arg_types.push_back( monomorphise_type_with(sp, arg.second,  monomorph_cb, false) );
                m_resolve.expand_associated_types(sp, cache.m_arg_types.back());
                DEBUG("= " << cache.m_arg_types.back());
            }
            DEBUG("Ret " << fcn.m_return);
            cache.m_arg_types.push_back( monomorphise_type_with(sp, fcn.m_return,  monomorph_cb, false) );
            m_resolve.expand_associated_types(sp, cache.m_arg_types.back());
            DEBUG("= " << cache.m_arg_types.back());
            
            // Check types
            for(unsigned int i = 0; i < node.m_args.size(); i ++) {
                DEBUG("CHECK ARG " << i << " " << node.m_cache.m_arg_types[i] << " == " << node.m_args[i]->m_res_type);
                check_types_equal(node.span(), node.m_cache.m_arg_types[i], node.m_args[i]->m_res_type);
            }
            DEBUG("CHECK RV " << node.m_res_type << " == " << node.m_cache.m_arg_types.back());
            check_types_equal(node.span(), node.m_res_type,  node.m_cache.m_arg_types.back());
            
            cache.m_monomorph_cb = mv$(monomorph_cb);
            
            // Bounds (encoded as associated)
            for(const auto& bound : cache.m_fcn_params->m_bounds)
            {
                TU_MATCH(::HIR::GenericBound, (bound), (be),
                (Lifetime,
                    ),
                (TypeLifetime,
                    ),
                (TraitBound,
                    auto real_type = monomorphise_type_with(sp, be.type, cache.m_monomorph_cb);
                    auto real_trait = monomorphise_genericpath_with(sp, be.trait.m_path, cache.m_monomorph_cb, false);
                    DEBUG("Bound " << be.type << ":  " << be.trait);
                    DEBUG("= (" << real_type << ": " << real_trait << ")");
                    const auto& trait_params = real_trait.m_params;
                    
                    const auto& trait_path = be.trait.m_path.m_path;
                    check_associated_type(sp, ::HIR::TypeRef(), trait_path, mv$(trait_params.clone().m_types), real_type, "");
                    
                    // TODO: Either - Don't include the above impl bound, or change the below trait to the one that has that type
                    for( const auto& assoc : be.trait.m_type_bounds ) {
                        ::HIR::GenericPath  type_trait_path;
                        m_resolve.trait_contains_type(sp, real_trait, *be.trait.m_trait_ptr, assoc.first,  type_trait_path);
                        
                        auto other_ty = monomorphise_type_with(sp, assoc.second, cache.m_monomorph_cb, true);
                        
                        check_associated_type(sp, other_ty,  type_trait_path.m_path, mv$(type_trait_path.m_params.m_types), real_type, assoc.first.c_str());
                    }
                    ),
                (TypeEquality,
                    auto real_type_left = monomorphise_type_with(sp, be.type, cache.m_monomorph_cb);
                    auto real_type_right = monomorphise_type_with(sp, be.other_type, cache.m_monomorph_cb);
                    m_resolve.expand_associated_types(sp, real_type_left);
                    m_resolve.expand_associated_types(sp, real_type_right);
                    check_types_equal(sp, real_type_left, real_type_right);
                    )
                )
            }
        }
        void visit(::HIR::ExprNode_CallValue& node) override
        {
            TRACE_FUNCTION_F(&node << " (...)(..., )");
            // TODO: Don't use m_arg_types (do full resolution again)
            ASSERT_BUG(node.span(), node.m_arg_types.size() > 0, "CallValue cache not populated");
            for(unsigned int i = 0; i < node.m_args.size(); i ++)
            {
                check_types_equal(node.span(), node.m_arg_types[i], node.m_args[i]->m_res_type);
            }
            check_types_equal(node.span(), node.m_res_type, node.m_arg_types.back());
            
            // Don't bother checking for a FnOnce impl, if the cache is populated it was found
            
            node.m_value->visit( *this );
            for( auto& val : node.m_args ) {
                val->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_CallMethod& node) override
        {
            TRACE_FUNCTION_F(&node << " (...)." << node.m_method << "(...,) - " << node.m_method_path);
            // TODO: Don't use m_cache
            ASSERT_BUG(node.span(), node.m_cache.m_arg_types.size() > 0, "CallMethod cache not populated");
            ASSERT_BUG(node.span(), node.m_cache.m_arg_types.size() == 1 + node.m_args.size() + 1, "CallMethod cache mis-sized");
            check_types_equal(node.m_cache.m_arg_types[0], node.m_value);
            for(unsigned int i = 0; i < node.m_args.size(); i ++)
            {
                check_types_equal(node.m_cache.m_arg_types[1+i], node.m_args[i]);
            }
            check_types_equal(node.span(), node.m_res_type, node.m_cache.m_arg_types.back());
            
            node.m_value->visit( *this );
            for( auto& val : node.m_args ) {
                val->visit( *this );
            }
        }
        
        void visit(::HIR::ExprNode_Field& node) override
        {
            TRACE_FUNCTION_F(&node << " (...)." << node.m_field);
            const auto& sp = node.span();
            const auto& str_ty = node.m_value->m_res_type;
            
            bool is_index = ( '0' <= node.m_field[0] && node.m_field[0] <= '9' );
            if( str_ty.m_data.is_Tuple() )
            {
                ASSERT_BUG(sp, is_index, "Non-index _Field on tuple");
            }
            else if( str_ty.m_data.is_Closure() )
            {
                ASSERT_BUG(sp, is_index, "Non-index _Field on closure");
            }
            else
            {
                ASSERT_BUG(sp, str_ty.m_data.is_Path(), "Value type of _Field isn't Path - " << str_ty);
                const auto& ty_e = str_ty.m_data.as_Path();
                ASSERT_BUG(sp, ty_e.binding.is_Struct(), "Value type of _Field isn't a Struct - " << str_ty);
                //const auto& str = *ty_e.binding.as_Struct();
                
                // TODO: Triple-check result, but that probably isn't needed
            }
            
            node.m_value->visit( *this );
        }
        void visit(::HIR::ExprNode_Tuple& node) override
        {
            TRACE_FUNCTION_F(&node << " (...,)");
            const auto& tys = node.m_res_type.m_data.as_Tuple();
            
            ASSERT_BUG(node.span(), tys.size() == node.m_vals.size(), "Bad element count in tuple literal - " << tys.size() << " != " << node.m_vals.size());
            for(unsigned int i = 0; i < node.m_vals.size(); i ++)
            {
                check_types_equal(node.span(), tys[i], node.m_vals[i]->m_res_type);
            }
            
            for( auto& val : node.m_vals ) {
                val->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_ArrayList& node) override
        {
            TRACE_FUNCTION_F(&node << " [...,]");
            // Cleanly equate into array (with coercions)
            const auto& inner_ty = *node.m_res_type.m_data.as_Array().inner;
            for( auto& val : node.m_vals ) {
                check_types_equal(val->span(), inner_ty, val->m_res_type);
            }
            
            for( auto& val : node.m_vals ) {
                val->visit( *this );
            }
        }
        void visit(::HIR::ExprNode_ArraySized& node) override
        {
            TRACE_FUNCTION_F(&node << " [...; "<<node.m_size_val<<"]");
            
            //check_types_equal(node.m_size->span(), ::HIR::TypeRef(::HIR::Primitive::Usize), node.m_size->m_res_type);
            const auto& inner_ty = *node.m_res_type.m_data.as_Array().inner;
            check_types_equal(node.m_val->span(), inner_ty, node.m_val->m_res_type);
            
            node.m_val->visit( *this );
            node.m_size->visit( *this );
        }
        
        void visit(::HIR::ExprNode_Literal& node) override
        {
            // No validation needed
        }
        void visit(::HIR::ExprNode_PathValue& node) override
        {
            TRACE_FUNCTION_F(&node << " " << node.m_path);
            const auto& sp = node.span();
            
            TU_MATCH(::HIR::Path::Data, (node.m_path.m_data), (e),
            (Generic,
                switch(node.m_target)
                {
                case ::HIR::ExprNode_PathValue::UNKNOWN:
                    BUG(sp, "Unknown target PathValue encountered with Generic path");
                case ::HIR::ExprNode_PathValue::FUNCTION:
                    // TODO: Is validate needed?
                    assert( node.m_res_type.m_data.is_Function() );
                    break;
                case ::HIR::ExprNode_PathValue::STRUCT_CONSTR: {
                    } break;
                case ::HIR::ExprNode_PathValue::STATIC: {
                    } break;
                case ::HIR::ExprNode_PathValue::CONSTANT: {
                    } break;
                }
                ),
            (UfcsUnknown,
                BUG(sp, "Encountered UfcsUnknown");
                ),
            (UfcsKnown,
                check_associated_type(sp, ::HIR::TypeRef(),  e.trait.m_path, mv$(e.trait.m_params.clone().m_types), e.type->clone(), "");
                
                const auto& trait = this->m_resolve.m_crate.get_trait_by_path(sp, e.trait.m_path);
                auto it = trait.m_values.find( e.item );
                if( it == trait.m_values.end() || it->second.is_None() ) {
                    ERROR(sp, E0000, "`" << e.item << "` is not a value member of trait " << e.trait.m_path);
                }
                TU_MATCH( ::HIR::TraitValueItem, (it->second), (ie),
                (None, throw ""; ),
                (Constant,
                    TODO(sp, "Monomorpise associated constant type - " << ie.m_type);
                    ),
                (Static,
                    TODO(sp, "Monomorpise associated static type - " << ie.m_type);
                    ),
                (Function,
                    assert( node.m_res_type.m_data.is_Function() );
                    )
                )
                ),
            (UfcsInherent,
                )
            )
        }
        
        void visit(::HIR::ExprNode_Variable& node) override
        {
            // TODO: Check against variable slot? Nah.
        }
        
        void visit(::HIR::ExprNode_Closure& node) override
        {
            TRACE_FUNCTION_F(&node << " |...| ...");
            
            if( node.m_code )
            {
                check_types_equal(node.m_code->span(), node.m_return, node.m_code->m_res_type);
                this->closure_ret_types.push_back( &node.m_return );
                node.m_code->visit( *this );
                this->closure_ret_types.pop_back( );
            }
        }
        
    private:
        void check_types_equal(const ::HIR::TypeRef& l, const ::HIR::ExprNodeP& node) const
        {
            check_types_equal(node->span(), l, node->m_res_type);
        }
        void check_types_equal(const Span& sp, const ::HIR::TypeRef& l, const ::HIR::TypeRef& r) const
        {
            if( l.m_data.is_Diverge() || r.m_data.is_Diverge() ) {
                // Diverge, matches everything.
                // TODO: Is this always true?
            }
            else if( l != r ) {
                ERROR(sp, E0000, "Type mismatch - " << l << " != " << r);
            }
            else {
                // All good
            }
        }
        void check_associated_type(const Span& sp,
                const ::HIR::TypeRef& res,
                const ::HIR::SimplePath& trait, const ::std::vector< ::HIR::TypeRef>& params, const ::HIR::TypeRef& ity, const char* name
            ) const
        {
        }
        
        const ::HIR::SimplePath& get_lang_item_path(const Span& sp, const char* name) const
        {
            return m_resolve.m_crate.get_lang_item_path(sp, name);
        }
    };
    
    
    class OuterVisitor:
        public ::HIR::Visitor
    {
        StaticTraitResolve  m_resolve;
    public:
        OuterVisitor(const ::HIR::Crate& crate):
            m_resolve(crate)
        {}
        
        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visit_expr(::HIR::ExprPtr& exp) override {
            BUG(Span(), "visit_expr hit in OuterVisitor");
        }
        
        void visit_type(::HIR::TypeRef& ty) override
        {
            TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Array, e,
                this->visit_type( *e.inner );
                DEBUG("Array size " << ty);
                t_args  tmp;
                if( e.size ) {
                    ExprVisitor_Validate    ev(m_resolve, {}, ::HIR::TypeRef(::HIR::CoreType::Usize));
                    ev.visit_root( *e.size );
                }
            )
            else {
                ::HIR::Visitor::visit_type(ty);
            }
        }
        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->m_resolve.set_item_generics(item.m_params);
            if( item.m_code )
            {
                DEBUG("Function code " << p);
                ExprVisitor_Validate    ev(m_resolve, item.m_args, item.m_return);
                ev.visit_root( *item.m_code );
            }
            else
            {
                DEBUG("Function code " << p << " (none)");
            }
        }
        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            if( item.m_value )
            {
                t_args  tmp;
                ExprVisitor_Validate    ev(m_resolve, tmp, item.m_type);
                ev.visit_root(*item.m_value);
            }
        }
        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            if( item.m_value )
            {
                t_args  tmp;
                ExprVisitor_Validate    ev(m_resolve, tmp, item.m_type);
                ev.visit_root(*item.m_value);
            }
        }
        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            //auto _ = this->m_ms.set_item_generics(item.m_params);
            // TODO: Use a different type depding on repr()
            auto enum_type = ::HIR::TypeRef(::HIR::CoreType::Isize);
            
            for(auto& var : item.m_variants)
            {
                TU_IFLET(::HIR::Enum::Variant, var.second, Value, e,
                    DEBUG("Enum value " << p << " - " << var.first);
                    
                    t_args  tmp;
                    ExprVisitor_Validate    ev(m_resolve, tmp, enum_type);
                    ev.visit_root(*e);
                )
            }
        }
        
        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override
        {
            auto _ = this->m_resolve.set_impl_generics(item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }
        void visit_type_impl(::HIR::TypeImpl& impl) override
        {
            TRACE_FUNCTION_F("impl " << impl.m_type);
            auto _ = this->m_resolve.set_impl_generics(impl.m_params);
            
            ::HIR::Visitor::visit_type_impl(impl);
        }
        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override
        {
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << trait_path << " for " << impl.m_type);
            auto _ = this->m_resolve.set_impl_generics(impl.m_params);
            
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        }
    };
}

void Typecheck_Expressions_Validate(::HIR::Crate& crate)
{
    OuterVisitor    ov(crate);
    ov.visit_crate( crate );
}

