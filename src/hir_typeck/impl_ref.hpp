/*
 */
#pragma once

#include <hir/type.hpp>

namespace HIR {
    class TraitImpl;
}

struct ImplRef
{
    TAGGED_UNION(Data, TraitImpl,
    (TraitImpl, struct {
        ::std::vector<const ::HIR::TypeRef*>   params;
        ::std::vector<::HIR::TypeRef>   params_ph;
        const ::HIR::SimplePath*    trait_path;
        const ::HIR::TraitImpl* impl;
        }),
    (BoundedPtr, struct {
        const ::HIR::TypeRef*    type;
        const ::HIR::PathParams* trait_args;
        const ::std::map< ::std::string, ::HIR::TypeRef>*    assoc;
        }),
    (Bounded, struct {
        ::HIR::TypeRef    type;
        ::HIR::PathParams trait_args;
        ::std::map< ::std::string, ::HIR::TypeRef>    assoc;
        })
    );
    
    Data    m_data;
    
    ImplRef():
        m_data(Data::make_TraitImpl({ {}, {}, nullptr, nullptr }))
    {}
    ImplRef(::std::vector<const ::HIR::TypeRef*> params, const ::HIR::SimplePath& trait, const ::HIR::TraitImpl& impl, ::std::vector< ::HIR::TypeRef> params_ph={}):
        m_data(Data::make_TraitImpl({ mv$(params), mv$(params_ph), &trait, &impl }))

    {}
    ImplRef(const ::HIR::TypeRef* type, const ::HIR::PathParams* args, const ::std::map< ::std::string, ::HIR::TypeRef>* assoc):
        m_data(Data::make_BoundedPtr({ type, mv$(args), mv$(assoc) }))
    {}
    ImplRef(::HIR::TypeRef type, ::HIR::PathParams args, ::std::map< ::std::string, ::HIR::TypeRef> assoc):
        m_data(Data::make_Bounded({ mv$(type), mv$(args), mv$(assoc) }))
    {}
    
    bool is_valid() const {
        return !(m_data.is_TraitImpl() && m_data.as_TraitImpl().impl == nullptr);
    }
    
    bool more_specific_than(const ImplRef& other) const;
    
    ::HIR::TypeRef get_impl_type() const;
    ::HIR::PathParams get_trait_params() const;
    
    ::HIR::TypeRef get_trait_ty_param(unsigned int) const;
    
    bool type_is_specializable(const char* name) const;
    ::HIR::TypeRef get_type(const char* name) const;
    
    friend ::std::ostream& operator<<(::std::ostream& os, const ImplRef& x);
};
