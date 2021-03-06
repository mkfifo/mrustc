/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * parse/root.cpp
 * - Parsing at the module level (highest-level parsing)
 *
 * Entrypoint:
 * - Parse_Crate : Handles crate attrbutes, and passes on to Parse_ModRoot
 * - Parse_ModRoot
 */
#include "../ast/ast.hpp"
#include "../ast/crate.hpp"
#include "parseerror.hpp"
#include "common.hpp"
#include <cassert>

template<typename T>
Spanned<T> get_spanned(TokenStream& lex, ::std::function<T()> f) {
    auto ps = lex.start_span();
    auto v = f();
    return Spanned<T> {
        lex.end_span(ps),
        mv$(v)
        };
}
#define GET_SPANNED(type, lex, val) get_spanned< type >(lex, [&](){ return val; })

::std::string dirname(::std::string input) {
    while( input.size() > 0 && input.back() != '/' ) {
        input.pop_back();
    }
    return input;
}

AST::MetaItem   Parse_MetaItem(TokenStream& lex);
void Parse_ModRoot(TokenStream& lex, AST::Module& mod, AST::MetaItems& mod_attrs, bool file_controls_dir, const ::std::string& path);

::std::vector< ::std::string> Parse_HRB(TokenStream& lex)
{
    TRACE_FUNCTION;
    Token   tok;
    
    ::std::vector< ::std::string>   lifetimes;
    GET_CHECK_TOK(tok, lex, TOK_LT);
    do {
        switch(GET_TOK(tok, lex))
        {
        case TOK_LIFETIME:
            lifetimes.push_back(tok.str());
            break;    
        default:
            throw ParseError::Unexpected(lex, tok, Token(TOK_LIFETIME));
        }
    } while( GET_TOK(tok, lex) == TOK_COMMA );
    CHECK_TOK(tok, TOK_GT);
    return lifetimes;
}
/// Parse type parameters in a definition
void Parse_TypeBound(TokenStream& lex, AST::GenericParams& ret, TypeRef checked_type, ::std::vector< ::std::string> lifetimes = {})
{
    TRACE_FUNCTION;
    Token tok;
    
    do
    {
        if(GET_TOK(tok, lex) == TOK_LIFETIME) {
            ret.add_bound(AST::GenericBound::make_TypeLifetime( {
                checked_type.clone(), tok.str()
                } ));
        }
        else if( tok.type() == TOK_QMARK ) {
            ret.add_bound(AST::GenericBound::make_MaybeTrait( {
                checked_type.clone(), Parse_Path(lex, PATH_GENERIC_TYPE)
                } ));
        }
        else {
            if( tok.type() == TOK_RWORD_FOR )
            {
                GET_CHECK_TOK(tok, lex, TOK_LT);
                do {
                    switch(GET_TOK(tok, lex))
                    {
                    case TOK_LIFETIME:
                        lifetimes.push_back(tok.str());
                        break;    
                    default:
                        throw ParseError::Unexpected(lex, tok, Token(TOK_LIFETIME));
                    }
                } while( GET_TOK(tok, lex) == TOK_COMMA );
                CHECK_TOK(tok, TOK_GT);
            }
            else {
                PUTBACK(tok, lex);
            }
            
            ret.add_bound( AST::GenericBound::make_IsTrait({
                checked_type.clone(), mv$(lifetimes), Parse_Path(lex, PATH_GENERIC_TYPE)
                }) );
        }
    } while( GET_TOK(tok, lex) == TOK_PLUS );
    PUTBACK(tok, lex);
}

/// Parse type parameters within '<' and '>' (definition)
AST::GenericParams Parse_GenericParams(TokenStream& lex)
{
    TRACE_FUNCTION;

    AST::GenericParams ret;
    Token tok;
    do {
        bool is_lifetime = false;
        if( GET_TOK(tok, lex) == TOK_GT ) {
            break ;
        }
        switch( tok.type() )
        {
        case TOK_IDENT:
            break;
        case TOK_LIFETIME:
            is_lifetime = true;
            break;
        default:
            // Oopsie!
            throw ParseError::Unexpected(lex, tok);
        }
        ::std::string param_name = tok.str();
        if( is_lifetime )
            ret.add_lft_param( param_name );
        else
            ret.add_ty_param( AST::TypeParam( param_name ) );
            
        if( GET_TOK(tok, lex) == TOK_COLON )
        {
            if( is_lifetime )
            {
                do {
                    GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                    ret.add_bound(AST::GenericBound::make_Lifetime( {param_name, tok.str()} ));
                } while( GET_TOK(tok, lex) == TOK_PLUS );
            }
            else
            {
                Parse_TypeBound(lex, ret, TypeRef(TypeRef::TagArg(), param_name));
                GET_TOK(tok, lex);
            }
        }
        
        if( !is_lifetime && tok.type() == TOK_EQUAL )
        {
            ret.ty_params().back().setDefault( Parse_Type(lex) );
            GET_TOK(tok, lex);
        }
    } while( tok.type() == TOK_COMMA );
    PUTBACK(tok, lex);
    return ret;
}


/// Parse the contents of a 'where' clause
void Parse_WhereClause(TokenStream& lex, AST::GenericParams& params)
{
    TRACE_FUNCTION;
    Token   tok;
    
    do {
        GET_TOK(tok, lex);
        if( tok.type() == TOK_BRACE_OPEN ) {
            break;
        }
        
        if( tok.type() == TOK_LIFETIME )
        {
            auto lhs = mv$(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
            auto rhs = mv$(tok.str());
            params.add_bound( AST::GenericBound::make_Lifetime({lhs, rhs}) );
        }
        // Higher-ranked types/lifetimes
        else if( tok.type() == TOK_RWORD_FOR )
        {
            ::std::vector< ::std::string>   lifetimes = Parse_HRB(lex);
            
            TypeRef type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            Parse_TypeBound(lex, params, type, lifetimes);
        }
        else
        {
            PUTBACK(tok, lex);
            TypeRef type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            Parse_TypeBound(lex, params, type);
        }
    } while( GET_TOK(tok, lex) == TOK_COMMA );
    PUTBACK(tok, lex);
}

// Parse a single function argument
::std::pair< AST::Pattern, TypeRef> Parse_Function_Arg(TokenStream& lex, bool expect_named)
{
    TRACE_FUNCTION_F("expect_named = " << expect_named);
    Token   tok;
    
    AST::Pattern pat;
    
    // If any of the following
    // - Expecting a named parameter (i.e. defining a function in root or impl)
    // - Next token is an underscore (only valid as a pattern here)
    // - Next token is 'mut' (a mutable parameter slot)
    // - Next two are <ident> ':' (a trivial named parameter)
    // NOTE: When not expecting a named param, destructuring patterns are not allowed
    if( expect_named
      || LOOK_AHEAD(lex) == TOK_UNDERSCORE
      || LOOK_AHEAD(lex) == TOK_RWORD_MUT
      || (LOOK_AHEAD(lex) == TOK_IDENT && lex.lookahead(1) == TOK_COLON)
      )
    {
        // Function args can't be refuted
        pat = Parse_Pattern(lex, false);
        GET_CHECK_TOK(tok, lex, TOK_COLON);
    }
    
    TypeRef type = Parse_Type(lex);
    
    
    return ::std::make_pair( ::std::move(pat), ::std::move(type) );
}

/// Parse a function definition (after the 'fn <name>')
AST::Function Parse_FunctionDef(TokenStream& lex, ::std::string abi, AST::MetaItems& attrs, bool allow_self, bool can_be_prototype)
{
    TRACE_FUNCTION;

    Token   tok;

    // Parameters
    AST::GenericParams params;
    if( GET_TOK(tok, lex) == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
    }
    else {
        PUTBACK(tok, lex);
    }

    AST::Function::Arglist  args;

    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    GET_TOK(tok, lex);
    
    // Handle self
    if( tok.type() == TOK_AMP )
    {
        // By-reference method?
        // TODO: If a lifetime is seen (and not a prototype), it is definitely a self binding
        
        unsigned int ofs = 0;
        if( lex.lookahead(0) == TOK_LIFETIME )
            ofs ++;
        
        if( lex.lookahead(ofs) == TOK_RWORD_SELF || (lex.lookahead(ofs) == TOK_RWORD_MUT && lex.lookahead(ofs+1) == TOK_RWORD_SELF) )
        {
            auto ps = lex.start_span();
            ::std::string   lifetime;
            if( GET_TOK(tok, lex) == TOK_LIFETIME ) {
                lifetime = tok.str();
                GET_TOK(tok, lex);
            }
            if( tok.type() == TOK_RWORD_MUT )
            {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_SELF);
                args.push_back( ::std::make_pair( AST::Pattern(AST::Pattern::TagBind(), "self"), TypeRef(TypeRef::TagReference(), lex.end_span(ps), true, TypeRef("Self", 0xFFFF))) );
            }
            else
            {
                args.push_back( ::std::make_pair( AST::Pattern(AST::Pattern::TagBind(), "self"), TypeRef(TypeRef::TagReference(), lex.end_span(ps), false, TypeRef("Self", 0xFFFF))) );
            }
            DEBUG("TODO: UFCS / self lifetimes");
            if( allow_self == false )
                throw ParseError::Generic(lex, "Self binding not expected");
            //args.push_back( ::std::make_pair(
            //    AST::Pattern(),
            //    TypeRef(TypeRef::TagReference(), lifetime, (fcn_class == AST::Function::CLASS_MUTMETHOD), )
            //) );
            
            // Prime tok for next step
            GET_TOK(tok, lex);
        }
        else
        {
            // Unbound method
        }
    }
    else if( tok.type() == TOK_RWORD_MUT )
    {
        if( LOOK_AHEAD(lex) == TOK_RWORD_SELF )
        {
            GET_TOK(tok, lex);
            if( allow_self == false )
                throw ParseError::Generic(lex, "Self binding not expected");
            TypeRef ty;
            if( GET_TOK(tok, lex) == TOK_COLON ) {
                // Typed mut self
                ty = Parse_Type(lex);
            }
            else {
                PUTBACK(tok, lex);
                ty = TypeRef("Self", 0xFFFF);
            }
            args.push_back( ::std::make_pair( AST::Pattern(AST::Pattern::TagBind(), "self"), ty) );
            GET_TOK(tok, lex);
        }
    }
    else if( tok.type() == TOK_RWORD_SELF )
    {
        // By-value method
        if( allow_self == false )
            throw ParseError::Generic(lex, "Self binding not expected");
        TypeRef ty;
        if( GET_TOK(tok, lex) == TOK_COLON ) {
            // Typed mut self
            ty = Parse_Type(lex);
        }
        else {
            PUTBACK(tok, lex);
            ty = TypeRef("Self", 0xFFFF);
        }
        args.push_back( ::std::make_pair( AST::Pattern(AST::Pattern::TagBind(), "self"), ty) );
        GET_TOK(tok, lex);
    }
    else
    {
        // Unbound method
    }
    
    if( tok.type() != TOK_PAREN_CLOSE )
    {
        // Comma after self
        if( args.size() )
        {
            CHECK_TOK(tok, TOK_COMMA);
        }
        else {
            PUTBACK(tok, lex);
        }
        
        // Argument list
        do {
            if( LOOK_AHEAD(lex) == TOK_PAREN_CLOSE ) {
                GET_TOK(tok, lex);
                break;
            }
            if( LOOK_AHEAD(lex) == TOK_TRIPLE_DOT ) {
                GET_TOK(tok, lex);
                // TODO: Mark function as vardic
                GET_TOK(tok, lex);
                break; 
            }
            args.push_back( Parse_Function_Arg(lex, !can_be_prototype) );
        } while( GET_TOK(tok, lex) == TOK_COMMA );
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
    }
    else {
        // Eat 'tok', negative comparison
    }

    TypeRef ret_type = TypeRef(TypeRef::TagUnit(), Span(tok.get_pos()));
    if( GET_TOK(tok, lex) == TOK_THINARROW )
    {
        // Return type
        if( GET_TOK(tok, lex) == TOK_EXCLAM ) {
            ret_type = TypeRef(TypeRef::TagInvalid(), Span(tok.get_pos()));
        }
        else {
            PUTBACK(tok, lex);
            ret_type = Parse_Type(lex);
        }
    }
    else
    {
        PUTBACK(tok, lex);
    }

    if( GET_TOK(tok, lex) == TOK_RWORD_WHERE )
    {
        Parse_WhereClause(lex, params);
    }
    else {
        PUTBACK(tok, lex);
    }

    return AST::Function(::std::move(params), ::std::move(ret_type), ::std::move(args));
}

AST::Function Parse_FunctionDefWithCode(TokenStream& lex, ::std::string abi, AST::MetaItems& attrs, bool allow_self)
{
    Token   tok;
    auto ret = Parse_FunctionDef(lex, abi, attrs, allow_self, false);
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);
    PUTBACK(tok, lex);
    ret.set_code( Parse_ExprBlock(lex) );
    return ret;
}

AST::TypeAlias Parse_TypeAlias(TokenStream& lex, AST::MetaItems& meta_items)
{
    TRACE_FUNCTION;

    Token   tok;

    // Params
    tok = lex.getToken();
    AST::GenericParams params;
    if( tok.type() == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
        tok = lex.getToken();
    }
    
    if( tok.type() == TOK_RWORD_WHERE )
    {
        Parse_WhereClause(lex, params);
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_EQUAL);
    
    // Type
    TypeRef type = Parse_Type(lex);
    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
    
    return AST::TypeAlias( ::std::move(params), ::std::move(type) );
}

AST::Struct Parse_Struct(TokenStream& lex, const AST::MetaItems& meta_items)
{
    TRACE_FUNCTION;

    Token   tok;

    tok = lex.getToken();
    AST::GenericParams params;
    if( tok.type() == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
        if(GET_TOK(tok, lex) == TOK_RWORD_WHERE)
        {
            Parse_WhereClause(lex, params);
            tok = lex.getToken();
        }
    }
    if(tok.type() == TOK_PAREN_OPEN)
    {
        // Tuple structs
        // TODO: Using `StructItem` here isn't the best option. Should have another type
        ::std::vector<AST::TupleItem>  refs;
        while(GET_TOK(tok, lex) != TOK_PAREN_CLOSE)
        {
            AST::MetaItems  item_attrs;
            while( tok.type() == TOK_ATTR_OPEN )
            {
                item_attrs.push_back( Parse_MetaItem(lex) );
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                GET_TOK(tok, lex);
            }
            SET_ATTRS(lex, item_attrs);
            
            bool    is_pub = false;
            if(tok.type() == TOK_RWORD_PUB)
                is_pub = true;
            else
                PUTBACK(tok, lex);
            
            refs.push_back( AST::TupleItem( mv$(item_attrs), is_pub, Parse_Type(lex) ) );
            if( GET_TOK(tok, lex) != TOK_COMMA )
                break;
        }
        CHECK_TOK(tok, TOK_PAREN_CLOSE);

        if(LOOK_AHEAD(lex) == TOK_RWORD_WHERE)
        {
            GET_TOK(tok, lex);
            Parse_WhereClause(lex, params);
        }
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        //if( refs.size() == 0 )
        //    WARNING( , W000, "Use 'struct Name;' instead of 'struct Name();' ... ning-nong");
        return AST::Struct(mv$(params), mv$(refs));
    }
    else if(tok.type() == TOK_SEMICOLON)
    {
        // Unit-like struct
        return AST::Struct(mv$(params), ::std::vector<AST::TupleItem>());
    }
    else if(tok.type() == TOK_BRACE_OPEN)
    {
        ::std::vector<AST::StructItem>  items;
        while( GET_TOK(tok, lex) != TOK_BRACE_CLOSE )
        {
            AST::MetaItems  item_attrs;
            while( tok.type() == TOK_ATTR_OPEN )
            {
                item_attrs.push_back( Parse_MetaItem(lex) );
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                GET_TOK(tok, lex);
            }
            SET_ATTRS(lex, item_attrs);
            
            bool    is_pub = false;
            if(tok.type() == TOK_RWORD_PUB) {
                is_pub = true;
                GET_TOK(tok, lex);
            }
            
            CHECK_TOK(tok, TOK_IDENT);
            ::std::string   name = tok.str();
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            TypeRef type = Parse_Type(lex);
            
            items.push_back( AST::StructItem( mv$(item_attrs), is_pub, mv$(name), mv$(type) ) );
            if(GET_TOK(tok, lex) == TOK_BRACE_CLOSE)
                break;
            CHECK_TOK(tok, TOK_COMMA);
        }
        //if( items.size() == 0 )
        //    WARNING( , W000, "Use 'struct Name;' instead of 'struct Nam { };' ... ning-nong");
        return AST::Struct(::std::move(params), ::std::move(items));
    }
    else
    {
        throw ParseError::Unexpected(lex, tok);
    }
}

AST::Trait Parse_TraitDef(TokenStream& lex, AST::Module& mod, const AST::MetaItems& meta_items)
{
    TRACE_FUNCTION;

    Token   tok;
    
    AST::GenericParams params;
    if( GET_TOK(tok, lex) == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
        tok = lex.getToken();
    }
    
    // Trait bounds "trait Trait : 'lifetime + OtherTrait + OtherTrait2"
    ::std::vector<Spanned<AST::Path> >    supertraits;
    if(tok.type() == TOK_COLON)
    {
        do {
            if( GET_TOK(tok, lex) == TOK_LIFETIME ) {
                // TODO: Need a better way of indiciating 'static than just an invalid path
                supertraits.push_back( make_spanned( Span(tok.get_pos()), AST::Path() ) );
            }
            else {
                PUTBACK(tok, lex);
                supertraits.push_back( GET_SPANNED(::AST::Path, lex, Parse_Path(lex, PATH_GENERIC_TYPE)) );
            }
        } while( GET_TOK(tok, lex) == TOK_PLUS );
    }
    
    // TODO: Support "for Sized?"
    if(tok.type() == TOK_RWORD_WHERE)
    {
        //if( params.ty_params().size() == 0 )
        //    throw ParseError::Generic("Where clause with no generic params");
        Parse_WhereClause(lex, params);
        tok = lex.getToken();
    }

    
    AST::Trait trait( mv$(params), mv$(supertraits) );
        
    CHECK_TOK(tok, TOK_BRACE_OPEN);
    while( GET_TOK(tok, lex) != TOK_BRACE_CLOSE )
    {
        AST::MetaItems  item_attrs;
        while( tok.type() == TOK_ATTR_OPEN )
        {
            item_attrs.push_back( Parse_MetaItem(lex) );
            GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
            GET_TOK(tok, lex);
        }
        SET_ATTRS(lex, item_attrs);
        
        bool is_specialisable = false;
        if( tok.type() == TOK_IDENT && tok.str() == "default" ) {
            is_specialisable = true;
            GET_TOK(tok, lex);
        }
        // TODO: Mark specialisation
        (void)is_specialisable;
        
        ::std::string   abi = "rust";
        switch(tok.type())
        {
        case TOK_RWORD_STATIC: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = mv$(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            
            ::AST::Expr val;
            if(GET_TOK(tok, lex) == TOK_EQUAL) {
                val = Parse_Expr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);
            
            // TODO: Attributes on associated statics
            trait.add_static( mv$(name), ::AST::Static(AST::Static::STATIC, mv$(ty), val)/*, mv$(item_attrs)*/ );
            break; }
        case TOK_RWORD_CONST: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = mv$(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);
            
            ::AST::Expr val;
            if(GET_TOK(tok, lex) == TOK_EQUAL) {
                val = Parse_Expr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);
            
            // TODO: Attributes on associated constants
            trait.add_static( mv$(name), ::AST::Static(AST::Static::CONST, mv$(ty), val)/*, mv$(item_attrs)*/ );
            break; }
        // Associated type
        case TOK_RWORD_TYPE: {
            auto atype_params = ::AST::GenericParams { };
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            ::std::string name = tok.str();
            if( GET_TOK(tok, lex) == TOK_COLON )
            {
                // Bounded associated type
                Parse_TypeBound(lex, atype_params, TypeRef("Self", 0xFFFF));
                GET_TOK(tok, lex);
            }
            if( tok.type() == TOK_RWORD_WHERE ) {
                throw ParseError::Todo(lex, "Where clause on associated type");
            }
            
            TypeRef default_type;
            if( tok.type() == TOK_EQUAL ) {
                default_type = Parse_Type(lex);
                GET_TOK(tok, lex);
            }
            
            CHECK_TOK(tok, TOK_SEMICOLON);
            trait.add_type( ::std::move(name), ::std::move(default_type) );
            trait.items().back().data.as_Type().params() = mv$(atype_params);
            break; }

        // Functions (possibly unsafe)
        case TOK_RWORD_UNSAFE:
            item_attrs.push_back( AST::MetaItem("#UNSAFE") );
            if( GET_TOK(tok, lex) == TOK_RWORD_EXTERN )
        case TOK_RWORD_EXTERN:
            {
                abi = "C";
                if( GET_TOK(tok, lex) == TOK_STRING )
                    abi = tok.str();
                else
                    PUTBACK(tok, lex);
                
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_RWORD_FN);
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            ::std::string name = tok.str();
            // Self allowed, prototype-form allowed (optional names and no code)
            auto fcn = Parse_FunctionDef(lex, abi, item_attrs, true, true);
            if( GET_TOK(tok, lex) == TOK_BRACE_OPEN )
            {
                PUTBACK(tok, lex);
                fcn.set_code( Parse_ExprBlock(lex) );
            }
            else if( tok.type() == TOK_SEMICOLON )
            {
                // Accept it
            }
            else
            {
                throw ParseError::Unexpected(lex, tok);
            }
            // TODO: Store `item_attrs`
            trait.add_function( ::std::move(name), ::std::move(fcn) );
            break; }
        default:
            throw ParseError::Unexpected(lex, tok);
        }
    }
    
    return trait;
}

AST::Enum Parse_EnumDef(TokenStream& lex, AST::Module& mod, const AST::MetaItems& meta_items)
{
    TRACE_FUNCTION;

    Token   tok;
    
    tok = lex.getToken();
    // Type params supporting "where"
    AST::GenericParams params;
    if( tok.type() == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
        if(GET_TOK(tok, lex) == TOK_RWORD_WHERE)
        {
            Parse_WhereClause(lex, params);
            tok = lex.getToken();
        }
    }
    
    // Body
    CHECK_TOK(tok, TOK_BRACE_OPEN);
    ::std::vector<AST::EnumVariant>   variants;
    while( GET_TOK(tok, lex) != TOK_BRACE_CLOSE )
    {
        auto sp = lex.start_span();
        
        AST::MetaItems  item_attrs;
        while( tok.type() == TOK_ATTR_OPEN )
        {
            item_attrs.push_back( Parse_MetaItem(lex) );
            GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
            GET_TOK(tok, lex);
        }
        SET_ATTRS(lex, item_attrs);
        
        CHECK_TOK(tok, TOK_IDENT);
        ::std::string   name = tok.str();
        if( GET_TOK(tok, lex) == TOK_PAREN_OPEN )
        {
            ::std::vector<TypeRef>  types;
            // Get type list
            do
            {
                if(LOOK_AHEAD(lex) == TOK_PAREN_CLOSE)
                {
                    GET_TOK(tok, lex);
                    break;
                }
                
                AST::MetaItems  field_attrs;
                while( LOOK_AHEAD(lex) == TOK_ATTR_OPEN )
                {
                    GET_TOK(tok, lex);
                    field_attrs.push_back( Parse_MetaItem(lex) );
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                }
                
                types.push_back( Parse_Type(lex) );
            } while( GET_TOK(tok, lex) == TOK_COMMA );
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            GET_TOK(tok, lex);
            variants.push_back( AST::EnumVariant(mv$(item_attrs), mv$(name), mv$(types)) );
        }
        else if( tok.type() == TOK_BRACE_OPEN )
        {
            ::std::vector<::AST::StructItem>   fields;
            do
            {
                if(LOOK_AHEAD(lex) == TOK_BRACE_CLOSE)
                {
                    GET_TOK(tok, lex);
                    break;
                }
                
                AST::MetaItems  field_attrs;
                while( LOOK_AHEAD(lex) == TOK_ATTR_OPEN )
                {
                    GET_TOK(tok, lex);
                    field_attrs.push_back( Parse_MetaItem(lex) );
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                }
                
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = mv$(tok.str());
                GET_CHECK_TOK(tok, lex, TOK_COLON);
                auto ty = Parse_Type(lex);
                // TODO: Field attributes
                fields.push_back( ::AST::StructItem(mv$(field_attrs), true, mv$(name), mv$(ty)) );
            } while( GET_TOK(tok, lex) == TOK_COMMA );
            CHECK_TOK(tok, TOK_BRACE_CLOSE);
            GET_TOK(tok, lex);
            
            variants.push_back( AST::EnumVariant(mv$(item_attrs), mv$(name), mv$(fields)) );
        }
        else if( tok.type() == TOK_EQUAL )
        {
            auto node = Parse_Expr(lex);
            variants.push_back( AST::EnumVariant(mv$(item_attrs), mv$(name), mv$(node)) );
            GET_TOK(tok, lex);
        }
        else
        {
            variants.push_back( AST::EnumVariant(mv$(item_attrs), mv$(name), ::std::vector<TypeRef>()) );
        }
        
        if( tok.type() != TOK_COMMA )
            break;
    }
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    
    return AST::Enum( mv$(params), mv$(variants) );
}

/// Parse a meta-item declaration (either #![ or #[)
AST::MetaItem Parse_MetaItem(TokenStream& lex)
{
    TRACE_FUNCTION;
    Token tok;
    GET_TOK(tok, lex);
    
    if( tok.type() == TOK_INTERPOLATED_META ) {
        return mv$(tok.frag_meta());
    }
    
    CHECK_TOK(tok, TOK_IDENT);
    ::std::string   name = tok.str();
    switch(GET_TOK(tok, lex))
    {
    case TOK_EQUAL:
        GET_CHECK_TOK(tok, lex, TOK_STRING);
        return AST::MetaItem(name, tok.str());
    case TOK_PAREN_OPEN: {
        ::std::vector<AST::MetaItem>    items;
        do {
            if(LOOK_AHEAD(lex) == TOK_PAREN_CLOSE) {
                GET_TOK(tok, lex);
                break;
            }
            items.push_back(Parse_MetaItem(lex));
        } while(GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
        return AST::MetaItem(name, mv$(items)); }
    default:
        PUTBACK(tok, lex);
        return AST::MetaItem(name);
    }
}

void Parse_Impl(TokenStream& lex, AST::Module& mod, AST::MetaItems attrs, bool is_unsafe=false)
{
    TRACE_FUNCTION;
    Token   tok;
    auto ps = lex.start_span();

    AST::GenericParams params;
    // 1. (optional) type parameters
    if( GET_TOK(tok, lex) == TOK_LT )
    {
        params = Parse_GenericParams(lex);
        GET_CHECK_TOK(tok, lex, TOK_GT);
    }
    else {
        PUTBACK(tok, lex);
    }
    // 2. Either a trait name (with type params), or the type to impl
    
    Spanned<AST::Path>   trait_path;
    TypeRef impl_type;
    // - Handle negative impls, which must be a trait
    // "impl !Trait for Type {}"
    if( GET_TOK(tok, lex) == TOK_EXCLAM )
    {
        trait_path = GET_SPANNED(::AST::Path, lex, Parse_Path(lex, PATH_GENERIC_TYPE));
        GET_CHECK_TOK(tok, lex, TOK_RWORD_FOR);
        impl_type = Parse_Type(lex, true);
        
        if( GET_TOK(tok, lex) == TOK_RWORD_WHERE )
        {
            Parse_WhereClause(lex, params);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_BRACE_OPEN);
        // negative impls can't have any content
        GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
        
        mod.add_neg_impl( AST::ImplDef(lex.end_span(ps), AST::MetaItems(), mv$(params), mv$(trait_path), mv$(impl_type) ) );
        return ;
    }
    else
    {
        // - Don't care which at this stage
        PUTBACK(tok, lex);
        
        impl_type = Parse_Type(lex, true);
        
        if( GET_TOK(tok, lex) == TOK_RWORD_FOR )
        {
            if( !impl_type.is_path() )
                throw ParseError::Generic(lex, "Trait was not a path");
            trait_path = Spanned< AST::Path> {
                impl_type.span(),
                mv$(impl_type.path())
                };
            // Implementing a trait for another type, get the target type
            if( GET_TOK(tok, lex) == TOK_DOUBLE_DOT )
            {
                // Default impl
                impl_type = TypeRef(TypeRef::TagInvalid(), lex.getPosition());
            }
            else
            {
                PUTBACK(tok, lex);
                impl_type = Parse_Type(lex, true);
            }
        }
        else {
            PUTBACK(tok, lex);
        }
    }
    
    // Where clause
    if( GET_TOK(tok, lex) == TOK_RWORD_WHERE )
    {
        Parse_WhereClause(lex, params);
    }
    else {
        PUTBACK(tok, lex);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    while( LOOK_AHEAD(lex) == TOK_CATTR_OPEN )
    {
        GET_TOK(tok, lex);
        attrs.push_back( Parse_MetaItem(lex) );
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
    
    AST::Impl   impl( AST::ImplDef( lex.end_span(ps), mv$(attrs), mv$(params), mv$(trait_path), mv$(impl_type) ) );

    // A sequence of method implementations
    while( GET_TOK(tok, lex) != TOK_BRACE_CLOSE )
    {
        auto ps = lex.start_span();
        if( tok.type() == TOK_MACRO )
        {
            impl.add_macro_invocation( Parse_MacroInvocation( ps, AST::MetaItems(), mv$(tok.str()), lex ) );
            // - Silently consume ';' after the macro
            if( GET_TOK(tok, lex) != TOK_SEMICOLON )
                PUTBACK(tok, lex);
        }
        else
        {
            PUTBACK(tok, lex);
            Parse_Impl_Item(lex, impl);
        }
    }

    mod.add_impl( ::std::move(impl) );
}

void Parse_Impl_Item(TokenStream& lex, AST::Impl& impl)
{
    TRACE_FUNCTION;
    Token   tok;
   
    GET_TOK(tok, lex);
    
    AST::MetaItems  item_attrs;
    while( tok.type() == TOK_ATTR_OPEN )
    {
        item_attrs.push_back( Parse_MetaItem(lex) );
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
        GET_TOK(tok, lex);
    }
    SET_ATTRS(lex, item_attrs);
    
    auto ps = lex.start_span(); 
    
    bool is_public = false;
    if(tok.type() == TOK_RWORD_PUB) {
        is_public = true;
        GET_TOK(tok, lex);
    }
    
    bool is_specialisable = false;
    if( tok.type() == TOK_IDENT && tok.str() == "default" ) {
        is_specialisable = true;
        GET_TOK(tok, lex);
    }
    
    if(tok.type() == TOK_RWORD_UNSAFE) {
        item_attrs.push_back( AST::MetaItem("#UNSAFE") );
        GET_TOK(tok, lex);
    }
    
    ::std::string   abi = "rust";
    switch(tok.type())
    {
    case TOK_RWORD_TYPE: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        ::std::string name = tok.str();
        GET_CHECK_TOK(tok, lex, TOK_EQUAL);
        impl.add_type(is_public, is_specialisable, name, Parse_Type(lex));
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        break; }
    case TOK_RWORD_CONST:
        GET_TOK(tok, lex);
        if( tok.type() != TOK_RWORD_FN && tok.type() != TOK_RWORD_UNSAFE )
        {
            CHECK_TOK(tok, TOK_IDENT);
            auto name = mv$(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            auto val = Parse_Expr(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            
            auto i = ::AST::Static(AST::Static::CONST, mv$(ty), mv$(val));
            // TODO: Attributes on associated constants
            impl.add_static( is_public, is_specialisable, mv$(name),  mv$(i) /*, mv$(item_attrs)*/ );
            break ;
        }
        else if( tok.type() == TOK_RWORD_UNSAFE )
        {
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
            // TODO: Use a better marker
            item_attrs.push_back( AST::MetaItem("#UNSAFE") );
        }
        // TODO: Mark `const fn` as const (properly)
        item_attrs.push_back( AST::MetaItem("#CONST") );
        if( 0 )
        // FALL
    case TOK_RWORD_EXTERN:
        {
            abi = "C";
            if( GET_TOK(tok, lex) == TOK_STRING )
                abi = tok.str();
            else
                PUTBACK(tok, lex);
            
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_RWORD_FN);
        // FALL
    case TOK_RWORD_FN: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        ::std::string name = tok.str();
        DEBUG("Function " << name);
        // - Self allowed, can't be prototype-form
        auto fcn = Parse_FunctionDefWithCode(lex, abi, item_attrs, true);
        impl.add_function(is_public, is_specialisable, mv$(name), mv$(fcn));
        break; }

    default:
        throw ParseError::Unexpected(lex, tok);
    }
    
    impl.items().back().data->span = lex.end_span(ps);
    impl.items().back().data->attrs = mv$(item_attrs);    // Empty for functions
}

void Parse_ExternBlock(TokenStream& lex, AST::Module& mod, ::std::string abi, ::AST::MetaItems block_attrs)
{
    TRACE_FUNCTION;
    Token   tok;

    while( GET_TOK(tok, lex) == TOK_CATTR_OPEN )
    {
        block_attrs.push_back( Parse_MetaItem(lex) );
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
    PUTBACK(tok, lex);
    // TODO: Use `block_attrs`
    
    while( GET_TOK(tok, lex) != TOK_BRACE_CLOSE )
    {
        AST::MetaItems  meta_items;
        while( tok.type() == TOK_ATTR_OPEN )
        {
            meta_items.push_back( Parse_MetaItem(lex) );
            GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
            GET_TOK(tok, lex);
        }
        SET_ATTRS(lex, meta_items);
        
        bool is_public = false;
        if( tok.type() == TOK_RWORD_PUB ) {
            is_public = true;
            GET_TOK(tok, lex);
        }
        switch(tok.type())
        {
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            // parse function as prototype
            // - no self
            auto i = Parse_FunctionDef(lex, abi, meta_items, false, true);
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            break; }
        case TOK_RWORD_STATIC: {
            bool is_mut = false;
            if( GET_TOK(tok, lex) == TOK_RWORD_MUT )
                is_mut = true;
            else
                PUTBACK(tok, lex);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = mv$(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            
            auto static_class = is_mut ? ::AST::Static::MUT : ::AST::Static::STATIC;
            auto i = ::AST::Static(static_class,  type, ::AST::Expr());
            mod.add_static(is_public, mv$(name), mv$(i), mv$(meta_items));
            break; }
        default:
            throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_STATIC});
        }
    }
}

void Parse_Use_Wildcard(Span sp, AST::Path base_path, ::std::function<void(AST::UseStmt, ::std::string)> fcn)
{
    fcn( AST::UseStmt(mv$(sp), mv$(base_path)), "" ); // HACK! Empty path indicates wilcard import
}
void Parse_Use_Set(TokenStream& lex, const ProtoSpan& ps, const AST::Path& base_path, ::std::function<void(AST::UseStmt, ::std::string)> fcn)
{
    TRACE_FUNCTION;

    Token   tok;
    do {
        AST::Path   path;
        ::std::string   name;
        if( GET_TOK(tok, lex) == TOK_RWORD_SELF ) {
            path = ::AST::Path(base_path);
            name = base_path[base_path.size()-1].name();
        }
        else if( tok.type() == TOK_BRACE_CLOSE ) {
            break ;
        }
        else {
            CHECK_TOK(tok, TOK_IDENT);
            path = base_path + AST::PathNode(tok.str(), {});
            name = mv$(tok.str());
        }
        if( GET_TOK(tok, lex) == TOK_RWORD_AS ) {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = mv$(tok.str());
        }
        else {
            PUTBACK(tok, lex);
        }
        fcn(AST::UseStmt(lex.end_span(ps), mv$(path)), mv$(name));
    } while( GET_TOK(tok, lex) == TOK_COMMA );
    PUTBACK(tok, lex);
}

void Parse_Use(TokenStream& lex, ::std::function<void(AST::UseStmt, ::std::string)> fcn)
{
    TRACE_FUNCTION;

    Token   tok;
    AST::Path   path = AST::Path("", {});
    ::std::vector<AST::PathNode>    nodes;
    ProtoSpan   span_start = lex.start_span();
    
    switch( GET_TOK(tok, lex) )
    {
    case TOK_RWORD_SELF:
        path = AST::Path( AST::Path::TagSelf(), {} );    // relative path
        break;
    case TOK_RWORD_SUPER: {
        unsigned int count = 1;
        while( LOOK_AHEAD(lex) == TOK_DOUBLE_COLON && lex.lookahead(1) == TOK_RWORD_SUPER ) {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            GET_CHECK_TOK(tok, lex, TOK_RWORD_SUPER);
            count += 1;
        }
        path = AST::Path( AST::Path::TagSuper(), count, {} );
        break; }
    case TOK_IDENT:
        path.append( AST::PathNode(tok.str(), {}) );
        break;
    // Leading :: is allowed and ignored for the $crate feature
    case TOK_DOUBLE_COLON:
        // Absolute path
        // HACK! mrustc emits $crate as `::"crate-name"`
        if( LOOK_AHEAD(lex) == TOK_STRING )
        {
            GET_CHECK_TOK(tok, lex, TOK_STRING);
            path = ::AST::Path(tok.str(), {});
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        }
        else {
            PUTBACK(tok, lex);
        }
        break;
    case TOK_BRACE_OPEN:
        Parse_Use_Set(lex, span_start, path, fcn);
        GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
        return;
    default:
        throw ParseError::Unexpected(lex, tok);
    }
    while( GET_TOK(tok, lex) == TOK_DOUBLE_COLON )
    {
        if( GET_TOK(tok, lex) == TOK_IDENT )
        {
            path.append( AST::PathNode(tok.str(), {}) );
        }
        else
        {
            //path.set_span( lex.end_span(span_start) );
            switch( tok.type() )
            {
            case TOK_BRACE_OPEN:
                Parse_Use_Set(lex, span_start, mv$(path), fcn);
                GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
                break ;
            case TOK_STAR:
                Parse_Use_Wildcard( lex.end_span(span_start), mv$(path), fcn );
                break ;
            default:
                throw ParseError::Unexpected(lex, tok);
            }
            // early return - This branch is either the end of the use statement, or a syntax error
            return ;
        }
    }
    //path.set_span( lex.end_span(span_start) );
    
    ::std::string name;
    // This should only be allowed if the last token was an ident
    // - Above checks ensure this
    if( tok.type() == TOK_RWORD_AS )
    {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        name = tok.str();
    }
    else
    {
        PUTBACK(tok, lex);
        assert(path.nodes().size() > 0);
        name = path.nodes().back().name();
    }
    
    fcn( AST::UseStmt(lex.end_span(span_start), mv$(path)), name);
}


::AST::MacroInvocation Parse_MacroInvocation(ProtoSpan span_start, ::AST::MetaItems meta_items, ::std::string name, TokenStream& lex)
{
    Token   tok;
    ::std::string   ident;
    if( GET_TOK(tok, lex) == TOK_IDENT ) {
        ident = mv$(tok.str());
    }
    else {
        PUTBACK(tok, lex);
    }
    TokenTree tt = Parse_TT(lex, true);
    return ::AST::MacroInvocation( lex.end_span(span_start), mv$(meta_items), mv$(name), mv$(ident), mv$(tt));
}

void Parse_ExternCrate(TokenStream& lex, AST::Module& mod, bool is_public, AST::MetaItems meta_items)
{
    Token   tok;
    ::std::string   path, name;
    switch( GET_TOK(tok, lex) )
    {
    // `extern crate "crate-name" as crate_name;`
    // NOTE: rustc doesn't allow this, keep in mrustc for for reparse support
    case TOK_STRING:
        path = mv$(tok.str());
        GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        name = mv$(tok.str());
        break;
    // `extern crate crate_name;`
    // `extern crate crate_name as other_name;`
    case TOK_IDENT:
        name = mv$(tok.str());
        if(GET_TOK(tok, lex) == TOK_RWORD_AS) {
            path = mv$(name);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = mv$(tok.str());
        }
        else {
            PUTBACK(tok, lex);
            name = path;
        }
        break;
    default:
        throw ParseError::Unexpected(lex, tok, {TOK_STRING, TOK_IDENT});
    }
    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
    
    mod.add_ext_crate(is_public, mv$(path), mv$(name), mv$(meta_items));
}

void Parse_Mod_Item(TokenStream& lex, bool file_controls_dir, const ::std::string& file_path, AST::Module& mod, bool is_public, AST::MetaItems meta_items)
{
    SET_MODULE(lex, mod);
    lex.parse_state().parent_attrs = &meta_items;
    
    //TRACE_FUNCTION;
    Token   tok;

    auto ps = lex.start_span();
    #define APPLY_SPAN_ITEM()   mod.items().back().data.span = lex.end_span(ps)
    // The actual item!
    switch( GET_TOK(tok, lex) )
    {
    // `use ...`
    case TOK_RWORD_USE:
        Parse_Use(lex, [&mod,is_public,&file_path,&meta_items](AST::UseStmt p, std::string s) {
                DEBUG(file_path << " - use " << p << " as '" << s << "'");
                mod.add_alias(is_public, mv$(p), s, meta_items.clone());
            });
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        break;
    
    case TOK_RWORD_EXTERN:
        switch( GET_TOK(tok, lex) )
        {
        // `extern "<ABI>" fn ...`
        // `extern "<ABI>" { ...`
        case TOK_STRING: {
            ::std::string abi = tok.str();
            switch(GET_TOK(tok, lex))
            {
            // `extern "<ABI>" fn ...`
            case TOK_RWORD_FN: {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto i = Parse_FunctionDefWithCode(lex, abi, meta_items, false);
                mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
                APPLY_SPAN_ITEM();
                break; }
            // `extern "<ABI>" { ...`
            case TOK_BRACE_OPEN:
                Parse_ExternBlock(lex, mod, mv$(abi), mv$(meta_items));
                break;
            default:
                throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_BRACE_OPEN});
            }
            break; }
        // `extern fn ...`
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto i = Parse_FunctionDefWithCode(lex, "C", meta_items, false);
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        // `extern { ...`
        case TOK_BRACE_OPEN:
            Parse_ExternBlock(lex, mod, "C", mv$(meta_items));
            break;
        // `extern crate "crate-name" as crate_name;`
        // `extern crate crate_name;`
        case TOK_RWORD_CRATE:
            Parse_ExternCrate(lex, mod, is_public, mv$(meta_items));
            break;
        default:
            throw ParseError::Unexpected(lex, tok, {TOK_STRING, TOK_RWORD_FN, TOK_BRACE_OPEN, TOK_RWORD_CRATE});
        }
        break;
    
    // `const NAME`
    // `const fn`
    case TOK_RWORD_CONST: {
        switch( GET_TOK(tok, lex) )
        {
        case TOK_IDENT: {
            ::std::string name = tok.str();

            GET_CHECK_TOK(tok, lex, TOK_COLON);
            TypeRef type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            AST::Expr val = Parse_Expr(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            mod.add_static(is_public, name, AST::Static(AST::Static::CONST, type, val), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        case TOK_RWORD_UNSAFE: {
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            // TODO: Mark as const and unsafe
            meta_items.push_back( AST::MetaItem("#UNSAFE") );
            meta_items.push_back( AST::MetaItem("#CONST") );
            auto i = Parse_FunctionDefWithCode(lex, "rust", meta_items, false);
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            // TODO: Mark as const
            meta_items.push_back( AST::MetaItem("#CONST") );
            // - self not allowed, not prototype
            auto i = Parse_FunctionDefWithCode(lex, "rust", meta_items, false);
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        default:
            throw ParseError::Unexpected(lex, tok, {TOK_IDENT, TOK_RWORD_FN});
        }
        break; }
    // `static NAME`
    // `static mut NAME`
    case TOK_RWORD_STATIC: {
        bool is_mut = false;
        if(GET_TOK(tok, lex) == TOK_RWORD_MUT) {
            is_mut = true;
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_IDENT);
        ::std::string name = tok.str();

        GET_CHECK_TOK(tok, lex, TOK_COLON);
        TypeRef type = Parse_Type(lex);

        GET_CHECK_TOK(tok, lex, TOK_EQUAL);

        AST::Expr val = Parse_Expr(lex);

        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        mod.add_static(is_public, name,
            AST::Static((is_mut ? AST::Static::MUT : AST::Static::STATIC), type, val),
            mv$(meta_items)
            );
        APPLY_SPAN_ITEM();
        break; }

    // `unsafe fn`
    // `unsafe trait`
    // `unsafe impl`
    case TOK_RWORD_UNSAFE:
        meta_items.push_back( AST::MetaItem("#UNSAFE") );
        switch(GET_TOK(tok, lex))
        {
        // `unsafe extern fn`
        case TOK_RWORD_EXTERN: {
            ::std::string   abi = "C";
            if(GET_TOK(tok, lex) == TOK_STRING) {
                abi = mv$(tok.str());
            }
            else {
                PUTBACK(tok, lex);
            }
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto i = Parse_FunctionDefWithCode(lex, abi, meta_items, false);
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        // `unsafe fn`
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            // TODO: Mark as unsafe
            meta_items.push_back( AST::MetaItem("#UNSAFE") );
            // - self not allowed, not prototype
            auto i = Parse_FunctionDefWithCode(lex, "rust", meta_items, false);
            //i.set_unsafe();
            mod.add_function(is_public, tok.str(), mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        // `unsafe trait`
        case TOK_RWORD_TRAIT: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            ::std::string name = tok.str();
            // TODO: Mark as unsafe
            auto i = Parse_TraitDef(lex, mod, meta_items);
            //i.set_unsafe();
            mod.add_trait(is_public, name, mv$(i), mv$(meta_items));
            APPLY_SPAN_ITEM();
            break; }
        // `unsafe impl`
        case TOK_RWORD_IMPL:
            Parse_Impl(lex, mod, mv$(meta_items), true);
            break;
        default:
            throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_TRAIT, TOK_RWORD_IMPL});
        }
        break;
    // `fn`
    case TOK_RWORD_FN: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        ::std::string name = tok.str();
        // - self not allowed, not prototype
        auto i = Parse_FunctionDefWithCode(lex, "rust", meta_items, false);
        mod.add_function(is_public, name, mv$(i), mv$(meta_items));
        APPLY_SPAN_ITEM();
        break; }
    // `type`
    case TOK_RWORD_TYPE: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = mv$(tok.str());
        auto i = Parse_TypeAlias(lex, meta_items);
        mod.add_typealias(is_public, mv$(name), mv$(i), mv$(meta_items));
        APPLY_SPAN_ITEM();
        break; }
    // `struct`
    case TOK_RWORD_STRUCT: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = mv$(tok.str());
        auto i = Parse_Struct(lex, meta_items);
        mod.add_struct( is_public, name, mv$(i), mv$(meta_items) );
        APPLY_SPAN_ITEM();
        break; }
    // `enum`
    case TOK_RWORD_ENUM: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        ::std::string name = tok.str();
        auto i = Parse_EnumDef(lex, mod, meta_items);
        mod.add_enum(is_public, name, mv$(i), mv$(meta_items));
        APPLY_SPAN_ITEM();
        break; }
    // `impl`
    case TOK_RWORD_IMPL:
        Parse_Impl(lex, mod, mv$(meta_items));
        break;
    // `trait`
    case TOK_RWORD_TRAIT: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        ::std::string name = tok.str();
        auto i = Parse_TraitDef(lex, mod, meta_items);
        mod.add_trait(is_public, name, mv$(i), mv$(meta_items));
        APPLY_SPAN_ITEM();
        break; }

    case TOK_RWORD_MOD: {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = mv$(tok.str());
        DEBUG("Sub module '" << name << "'");
        AST::Module submod( mod.path() + name );
        
        // Rules for external files (/ path handling):
        // - IF using stdin (path='-') - Disallow and propagate '-' as path
        // - IF a #[path] attribute was passed, allow
        // - IF in crate root or mod.rs, allow (input flag)
        // - else, disallow and set flag
        ::std::string path_attr = (meta_items.has("path") ? meta_items.get("path")->string() : "");
        
        ::std::string   sub_path;
        bool    sub_file_controls_dir = true;
        if( file_path == "-" ) {
            if( path_attr.size() ) {
                throw ParseError::Generic(lex, "Attempt to set path when reading stdin");
            }
            sub_path = "-";
        }
        else if( path_attr.size() > 0 )
        {
            sub_path = dirname(file_path) + path_attr;
        }
        else if( file_controls_dir )
        {
            sub_path = dirname(file_path) + name;
        }
        else
        {
            sub_path = file_path;
            sub_file_controls_dir = false;
        }
        DEBUG("Mod '" << name << "', sub_path = " << sub_path);
        
        switch( GET_TOK(tok, lex) )
        {
        case TOK_BRACE_OPEN: {
            Parse_ModRoot(lex, submod, meta_items, sub_file_controls_dir, sub_path+"/");
            GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
            break; }
        case TOK_SEMICOLON:
            if( sub_path == "-" ) {
                throw ParseError::Generic(lex, "Cannot load module from file when reading stdin");
            }
            else if( path_attr.size() == 0 && ! file_controls_dir )
            {
                throw ParseError::Generic(lex, "Can't load from files outside of mod.rs or crate root");
            }
            else
            {
                ::std::string newpath_dir  = sub_path + "/";
                ::std::string newpath_file = path_attr.size() > 0 ? sub_path : sub_path + ".rs";
                ::std::ifstream ifs_dir (newpath_dir + "mod.rs");
                ::std::ifstream ifs_file(newpath_file);
                if( ifs_dir.is_open() && ifs_file.is_open() )
                {
                    // Collision
                    throw ParseError::Generic(lex, "Both modname.rs and modname/mod.rs exist");
                }
                else if( ifs_dir.is_open() )
                {
                    // Load from dir
                    Lexer sub_lex(newpath_dir + "mod.rs");
                    Parse_ModRoot(sub_lex, submod, meta_items, sub_file_controls_dir, newpath_dir);
                    GET_CHECK_TOK(tok, sub_lex, TOK_EOF);
                }
                else if( ifs_file.is_open() )
                {
                    // Load from file
                    Lexer sub_lex(newpath_file);
                    Parse_ModRoot(sub_lex, submod, meta_items, sub_file_controls_dir, newpath_file);
                    GET_CHECK_TOK(tok, sub_lex, TOK_EOF);
                }
                else
                {
                    // Can't find file
                    throw ParseError::Generic(lex, FMT("Can't find file for '" << name << "' in '" << file_path << "'") );
                }
            }
            break;
        default:
            throw ParseError::Generic("Expected { or ; after module name");
        }
        submod.prescan();
        mod.add_submod(is_public, mv$(name), mv$(submod), mv$(meta_items));
        APPLY_SPAN_ITEM();
        break; }

    default:
        throw ParseError::Unexpected(lex, tok);
    }
    #undef APPLY_SPAN_ITEM
}

void Parse_ModRoot_Items(TokenStream& lex, AST::Module& mod, bool file_controls_dir, const ::std::string& path)
{
    Token   tok;

    for(;;)
    {
        // Check 1 - End of module (either via a closing brace, or EOF)
        switch(GET_TOK(tok, lex))
        {
        case TOK_BRACE_CLOSE:
        case TOK_EOF:
            PUTBACK(tok, lex);
            return;
        default:
            PUTBACK(tok, lex);
            break;
        }

        // Attributes on the following item
        AST::MetaItems  meta_items;
        while( GET_TOK(tok, lex) == TOK_ATTR_OPEN )
        {
            meta_items.push_back( Parse_MetaItem(lex) );
            GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
        }
        PUTBACK(tok, lex);
        DEBUG("meta_items = " << meta_items);

        // root-level macros
        auto ps = lex.start_span();
        if( GET_TOK(tok, lex) == TOK_MACRO )
        {
            ::std::string   name = mv$(tok.str());
            mod.add_macro_invocation( Parse_MacroInvocation( ps, mv$(meta_items), mv$(name), lex ) );
            // - Silently consume ';' after the macro
            // TODO: Check the tt next token before parsing to tell if this is needed
            if( GET_TOK(tok, lex) != TOK_SEMICOLON )
                PUTBACK(tok, lex);
            continue ;
        }
        else {
            PUTBACK(tok, lex);
        }
    
        // Module visibility
        bool    is_public = false;
        if( GET_TOK(tok, lex) == TOK_RWORD_PUB ) {
            is_public = true;
        }
        else {
            PUTBACK(tok, lex);
        }

        Parse_Mod_Item(lex, file_controls_dir,path,  mod, is_public, mv$(meta_items));
    }
}

void Parse_ModRoot(TokenStream& lex, AST::Module& mod, AST::MetaItems& mod_attrs, bool file_controls_dir, const ::std::string& path)
{
    TRACE_FUNCTION;

    Token   tok;

    // Attributes on module/crate (will continue loop)
    while( GET_TOK(tok, lex) == TOK_CATTR_OPEN )
    {
        AST::MetaItem item = Parse_MetaItem(lex);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);

        mod_attrs.push_back( mv$(item) );
    }
    PUTBACK(tok, lex);

    Parse_ModRoot_Items(lex, mod, file_controls_dir, path);
}

AST::Crate Parse_Crate(::std::string mainfile)
{
    Token   tok;
    
    Lexer lex(mainfile);
    
    size_t p = mainfile.find_last_of('/');
    ::std::string mainpath = (p != ::std::string::npos ? ::std::string(mainfile.begin(), mainfile.begin()+p+1) : "./");
     
    AST::Crate  crate;

    Parse_ModRoot(lex, crate.root_module(), crate.m_attrs, true, mainpath);
    
    return crate;
}
