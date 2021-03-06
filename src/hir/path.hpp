/*
 */
#ifndef _HIR_PATH_HPP_
#define _HIR_PATH_HPP_
#pragma once

#include <common.hpp>
#include <tagged_union.hpp>
#include <hir/type_ptr.hpp>
#include <span.hpp>

namespace HIR {

struct Trait;

typedef ::std::function<const ::HIR::TypeRef&(const ::HIR::TypeRef&)> t_cb_resolve_type;
enum Compare {
    Equal,
    Fuzzy,
    Unequal,
};
static inline Compare& operator &=(Compare& x, const Compare& y) {
    if(x == Compare::Unequal) {
    }
    else if(y == Compare::Unequal) {
        x = Compare::Unequal;
    }
    else if(y == Compare::Fuzzy) {
        x = Compare::Fuzzy;
    }
    else {
        // keep as-is
    }
    return x;
}

/// Simple path - Absolute with no generic parameters
struct SimplePath
{
    ::std::string   m_crate_name;
    ::std::vector< ::std::string>   m_components;

    SimplePath():
        m_crate_name("")
    {
    }
    SimplePath(::std::string crate):
        m_crate_name( mv$(crate) )
    {
    }
    SimplePath(::std::string crate, ::std::vector< ::std::string> components):
        m_crate_name( mv$(crate) ),
        m_components( mv$(components) )
    {
    }

    SimplePath clone() const;
    
    SimplePath operator+(const ::std::string& s) const;
    bool operator==(const SimplePath& x) const {
        return m_crate_name == x.m_crate_name && m_components == x.m_components;
    }
    bool operator!=(const SimplePath& x) const {
        return !(*this == x);
    }
    bool operator<(const SimplePath& x) const {
        if( m_crate_name < x.m_crate_name ) return true;
        if( m_components < x.m_components ) return true;
        return false;
    }
    Ordering ord(const SimplePath& x) const {
        auto rv = ::ord(m_crate_name, x.m_crate_name);
        if(rv != OrdEqual)  return rv;
        rv = ::ord(m_components, x.m_components);
        return rv;
    }
    friend ::std::ostream& operator<<(::std::ostream& os, const SimplePath& x);
};


struct PathParams
{
    ::std::vector<TypeRef>  m_types;
    
    PathParams();
    PathParams clone() const;
    PathParams(const PathParams&) = delete;
    PathParams& operator=(const PathParams&) = delete;
    PathParams(PathParams&&) = default;
    PathParams& operator=(PathParams&&) = default;
    
    bool operator==(const PathParams& x) const;
    bool operator!=(const PathParams& x) const { return !(*this == x); }
    bool operator<(const PathParams& x) const { return ord(x) == OrdLess; }
    Ordering ord(const PathParams& x) const {
        return ::ord(m_types, x.m_types);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const PathParams& x);
};

/// Generic path - Simple path with one lot of generic params
class GenericPath
{
public:
    SimplePath  m_path;
    PathParams  m_params;

    GenericPath();
    GenericPath(::HIR::SimplePath sp);
    GenericPath(::HIR::SimplePath sp, ::HIR::PathParams params);
    
    GenericPath clone() const;
    
    bool operator==(const GenericPath& x) const;
    bool operator!=(const GenericPath& x) const { return !(*this == x); }
    bool operator<(const GenericPath& x) const { return ord(x) == OrdLess; }
    
    Ordering ord(const GenericPath& x) const {
        auto rv = ::ord(m_path, x.m_path);
        if(rv != OrdEqual)  return rv;
        return ::ord(m_params, x.m_params);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const GenericPath& x);
};

class TraitPath
{
public:
    GenericPath m_path;
    ::std::vector< ::std::string>   m_hrls;
    // TODO: Each bound should list its origin trait
    ::std::map< ::std::string, ::HIR::TypeRef>    m_type_bounds;
    
    const ::HIR::Trait* m_trait_ptr;
    
    TraitPath clone() const;
    bool operator==(const TraitPath& x) const;
    bool operator!=(const TraitPath& x) const { return !(*this == x); }
    
    Ordering ord(const TraitPath& x) const {
        ORD(m_path, x.m_path);
        ORD(m_hrls, x.m_hrls);
        return ::ord(m_type_bounds, x.m_type_bounds);
    }
    
    friend ::std::ostream& operator<<(::std::ostream& os, const TraitPath& x);
};

class Path
{
public:
    // Two possibilities
    // - UFCS
    // - Generic path
    TAGGED_UNION(Data, Generic,
    (Generic, GenericPath),
    (UfcsInherent, struct {
        ::std::unique_ptr<TypeRef>  type;
        ::std::string   item;
        PathParams  params;
        }),
    (UfcsKnown, struct {
        ::std::unique_ptr<TypeRef>  type;
        GenericPath trait;
        ::std::string   item;
        PathParams  params;
        }),
    (UfcsUnknown, struct {
        ::std::unique_ptr<TypeRef>  type;
        //GenericPath ??;
        ::std::string   item;
        PathParams  params;
        })
    );

    Data m_data;

    Path(Data data):
        m_data(mv$(data))
    {}
    Path(GenericPath _);
    Path(SimplePath _);
    
    Path(TypeRef ty, GenericPath trait, ::std::string item, PathParams item_params=PathParams());
    
    Path clone() const;
    Compare compare_with_placeholders(const Span& sp, const Path& x, t_cb_resolve_type resolve_placeholder) const;
    
    friend ::std::ostream& operator<<(::std::ostream& os, const Path& x);
};

}   // namespace HIR

#endif

