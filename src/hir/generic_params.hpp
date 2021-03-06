/*
 */
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "type.hpp"

namespace HIR {

struct TypeParamDef
{
    ::std::string   m_name;
    ::HIR::TypeRef  m_default;
    bool    m_is_sized;
};

TAGGED_UNION(GenericBound, Lifetime,
    (Lifetime, struct {
        ::std::string   test;
        ::std::string   valid_for;
        }),
    (TypeLifetime, struct {
        ::HIR::TypeRef  type;
        ::std::string   valid_for;
        }),
    (TraitBound, struct {
        ::HIR::TypeRef  type;
        ::HIR::TraitPath    trait;
        })/*,
    (NotTrait, struct {
        ::HIR::TypeRef  type;
        ::HIR::GenricPath    trait;
        })*/,
    (TypeEquality, struct {
        ::HIR::TypeRef  type;
        ::HIR::TypeRef  other_type;
        })
    );
extern ::std::ostream& operator<<(::std::ostream& os, const GenericBound& x);

class GenericParams
{
public:
    ::std::vector<TypeParamDef>   m_types;
    ::std::vector< ::std::string>   m_lifetimes;
    
    ::std::vector<GenericBound>    m_bounds;
    
    //GenericParams() {}
    
    GenericParams clone() const;
    
    struct PrintArgs {
        const GenericParams& gp;
        PrintArgs(const GenericParams& gp): gp(gp) {}
        friend ::std::ostream& operator<<(::std::ostream& os, const PrintArgs& x);
    };
    PrintArgs fmt_args() const { return PrintArgs(*this); }
    struct PrintBounds {
        const GenericParams& gp;
        PrintBounds(const GenericParams& gp): gp(gp) {}
        friend ::std::ostream& operator<<(::std::ostream& os, const PrintBounds& x);
    };
    PrintBounds fmt_bounds() const { return PrintBounds(*this); }
};

}   // namespace HIR

