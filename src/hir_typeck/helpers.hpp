/*
 */
#pragma once

#include <hir/type.hpp>
#include <hir/hir.hpp>
#include <hir/expr.hpp>
#include "impl_ref.hpp"

// TODO/NOTE - This is identical to ::HIR::t_cb_resolve_type
typedef ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)>   t_cb_generic;

extern bool monomorphise_type_needed(const ::HIR::TypeRef& tpl);
extern bool monomorphise_pathparams_needed(const ::HIR::PathParams& tpl);
static inline bool monomorphise_genericpath_needed(const ::HIR::GenericPath& tpl) {
    return monomorphise_pathparams_needed(tpl.m_params);
}
extern bool monomorphise_path_needed(const ::HIR::Path& tpl);
extern bool monomorphise_traitpath_needed(const ::HIR::TraitPath& tpl);
extern bool monomorphise_type_needed(const ::HIR::TypeRef& tpl);
extern ::HIR::PathParams monomorphise_path_params_with(const Span& sp, const ::HIR::PathParams& tpl, t_cb_generic callback, bool allow_infer);
extern ::HIR::GenericPath monomorphise_genericpath_with(const Span& sp, const ::HIR::GenericPath& tpl, t_cb_generic callback, bool allow_infer);
extern ::HIR::TraitPath monomorphise_traitpath_with(const Span& sp, const ::HIR::TraitPath& tpl, t_cb_generic callback, bool allow_infer);
extern ::HIR::TypeRef monomorphise_type_with(const Span& sp, const ::HIR::TypeRef& tpl, t_cb_generic callback, bool allow_infer=true);
extern ::HIR::TypeRef monomorphise_type(const Span& sp, const ::HIR::GenericParams& params_def, const ::HIR::PathParams& params,  const ::HIR::TypeRef& tpl);

extern void check_type_class_primitive(const Span& sp, const ::HIR::TypeRef& type, ::HIR::InferClass ic, ::HIR::CoreType ct);

class HMTypeInferrence
{
public:
    struct FmtType {
        const HMTypeInferrence& ctxt;
        const ::HIR::TypeRef& ty;
        FmtType(const HMTypeInferrence& ctxt, const ::HIR::TypeRef& ty):
            ctxt(ctxt),
            ty(ty)
        {}
        friend ::std::ostream& operator<<(::std::ostream& os, const FmtType& x) {
            x.ctxt.print_type(os, x.ty);
            return os;
        }
    };
    struct FmtPP {
        const HMTypeInferrence& ctxt;
        const ::HIR::PathParams& pps;
        FmtPP(const HMTypeInferrence& ctxt, const ::HIR::PathParams& pps):
            ctxt(ctxt),
            pps(pps)
        {}
        friend ::std::ostream& operator<<(::std::ostream& os, const FmtPP& x) {
            x.ctxt.print_pathparams(os, x.pps);
            return os;
        }
    };

public: // ?? - Needed once, anymore?
    struct IVar
    {
        unsigned int alias; // If not ~0, this points to another ivar
        ::std::unique_ptr< ::HIR::TypeRef> type;    // Type (only nullptr if alias!=0)
        
        IVar():
            alias(~0u),
            type(new ::HIR::TypeRef())
        {}
        bool is_alias() const { return alias != ~0u; }
    };
    
    ::std::vector< IVar>    m_ivars;
    bool    m_has_changed;
    
public:
    HMTypeInferrence():
        m_has_changed(false)
    {}
    
    bool peek_changed() const {
        return m_has_changed;
    }
    bool take_changed() {
        bool rv = m_has_changed;
        m_has_changed = false;
        return rv;
    }
    void mark_change() {
        if( !m_has_changed ) {
            DEBUG("- CHANGE");
            m_has_changed = true;
        }
    }
    
    void compact_ivars();
    bool apply_defaults();
    
    void dump() const;
    
    void print_type(::std::ostream& os, const ::HIR::TypeRef& tr) const;
    void print_pathparams(::std::ostream& os, const ::HIR::PathParams& pps) const;
    
    FmtType fmt_type(const ::HIR::TypeRef& tr) const {
        return FmtType(*this, tr);
    }
    FmtPP fmt(const ::HIR::PathParams& v) const {
        return FmtPP(*this, v);
    }
    
    /// Add (and bind) all '_' types in `type`
    void add_ivars(::HIR::TypeRef& type);
    // (helper) Add ivars to path parameters
    void add_ivars_params(::HIR::PathParams& params);
    
    ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)> callback_resolve_infer() const {
        return [&](const auto& ty)->const auto& {
                if( ty.m_data.is_Infer() ) 
                    return this->get_type(ty);
                else
                    return ty;
            };
    }
    
    // Mutation
    unsigned int new_ivar();
    ::HIR::TypeRef new_ivar_tr();
    void set_ivar_to(unsigned int slot, ::HIR::TypeRef type);
    void ivar_unify(unsigned int left_slot, unsigned int right_slot);

    // Lookup
    ::HIR::TypeRef& get_type(::HIR::TypeRef& type);
    const ::HIR::TypeRef& get_type(const ::HIR::TypeRef& type) const;
    
    void expand_ivars(::HIR::TypeRef& type);
    void expand_ivars_params(::HIR::PathParams& params);

    // Helpers
    bool pathparams_contain_ivars(const ::HIR::PathParams& pps) const;
    bool type_contains_ivars(const ::HIR::TypeRef& ty) const;
    bool pathparams_equal(const ::HIR::PathParams& pps_l, const ::HIR::PathParams& pps_r) const;
    bool types_equal(const ::HIR::TypeRef& l, const ::HIR::TypeRef& r) const;
private:
    IVar& get_pointed_ivar(unsigned int slot) const;
};


class TraitResolution
{
    const HMTypeInferrence& m_ivars;
    
    const ::HIR::Crate& m_crate;
    const ::HIR::GenericParams* m_impl_params;
    const ::HIR::GenericParams* m_item_params;
    
    ::std::map< ::HIR::TypeRef, ::HIR::TypeRef> m_type_equalities;
    
public:
    TraitResolution(const HMTypeInferrence& ivars, const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::GenericParams* item_params):
        m_ivars(ivars),
        m_crate(crate),
        m_impl_params( impl_params ),
        m_item_params( item_params )
    {
        prep_indexes();
    }
    
    const ::HIR::GenericParams& impl_params() const {
        static ::HIR::GenericParams empty;
        return m_impl_params ? *m_impl_params : empty;
    }
    const ::HIR::GenericParams& item_params() const {
        static ::HIR::GenericParams empty;
        return m_item_params ? *m_item_params : empty;
    }
    
    void prep_indexes();
    
    ::HIR::Compare compare_pp(const Span& sp, const ::HIR::PathParams& left, const ::HIR::PathParams& right) const;
    
    void compact_ivars(HMTypeInferrence& m_ivars);
    
    /// Check if a trait bound applies, using the passed function to expand Generic/Infer types
    bool check_trait_bound(const Span& sp, const ::HIR::TypeRef& type, const ::HIR::GenericPath& trait, t_cb_generic placeholder) const;
    
    bool has_associated_type(const ::HIR::TypeRef& ty) const;
    /// Expand any located associated types in the input, operating in-place and returning the result
    ::HIR::TypeRef expand_associated_types(const Span& sp, ::HIR::TypeRef input) const;
    void expand_associated_types__UfcsKnown(const Span& sp, ::HIR::TypeRef& input) const;
    const ::HIR::TypeRef& expand_associated_types(const Span& sp, const ::HIR::TypeRef& input, ::HIR::TypeRef& tmp) const {
        if( this->has_associated_type(input) ) {
            return (tmp = this->expand_associated_types(sp, input.clone()));
        }
        else {
            return input;
        }
    }
    
    /// Iterate over in-scope bounds (function then top)
    bool iterate_bounds( ::std::function<bool(const ::HIR::GenericBound&)> cb) const;

    typedef ::std::function<bool(const ::HIR::TypeRef&, const ::HIR::PathParams&, const ::std::map< ::std::string,::HIR::TypeRef>&)> t_cb_trait_impl;
    typedef ::std::function<bool(ImplRef, ::HIR::Compare)> t_cb_trait_impl_r;
    
    /// Searches for a trait impl that matches the provided trait name and type
    bool find_trait_impls(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& type,  t_cb_trait_impl_r callback) const;
    
    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool find_named_trait_in_trait(const Span& sp,
            const ::HIR::SimplePath& des, const ::HIR::PathParams& params,
            const ::HIR::Trait& trait_ptr, const ::HIR::SimplePath& trait_path, const ::HIR::PathParams& pp,
            const ::HIR::TypeRef& self_type,
            t_cb_trait_impl callback
            ) const;
    /// Search for a trait implementation in current bounds
    bool find_trait_impls_bound(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& type,  t_cb_trait_impl_r callback) const;
    /// Search for a trait implementation in the crate
    bool find_trait_impls_crate(const Span& sp, const ::HIR::SimplePath& trait, const ::HIR::PathParams& params, const ::HIR::TypeRef& type,  t_cb_trait_impl_r callback) const;
    
    /// Locate the named method by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderef_find_method(const Span& sp, const HIR::t_trait_list& traits, const ::HIR::TypeRef& top_ty, const ::std::string& method_name,  /* Out -> */::HIR::Path& fcn_path) const;
    /// Locate the named field by applying auto-dereferencing.
    /// \return Number of times deref was applied (or ~0 if _ was hit)
    unsigned int autoderef_find_field(const Span& sp, const ::HIR::TypeRef& top_ty, const ::std::string& name,  /* Out -> */::HIR::TypeRef& field_type) const;
    
    /// Apply an automatic dereference
    const ::HIR::TypeRef* autoderef(const Span& sp, const ::HIR::TypeRef& ty,  ::HIR::TypeRef& tmp_type) const;

    bool find_field(const Span& sp, const ::HIR::TypeRef& ty, const ::std::string& name,  /* Out -> */::HIR::TypeRef& field_type) const;
    bool find_method(const Span& sp, const HIR::t_trait_list& traits, const ::HIR::TypeRef& ty, const ::std::string& method_name,  /* Out -> */::HIR::Path& fcn_path) const;
    
    /// Locates a named method in a trait, and returns the path of the trait that contains it (with fixed parameters)
    bool trait_contains_method(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const ::std::string& name,  ::HIR::GenericPath& out_path) const;
    bool trait_contains_type(const Span& sp, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait_ptr, const ::std::string& name,  ::HIR::GenericPath& out_path) const;
};

