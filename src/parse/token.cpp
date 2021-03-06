/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 */
#include "token.hpp"
#include <common.hpp>
#include <parse/parseerror.hpp>
#include "interpolated_fragment.hpp"
#include <ast/types.hpp>
#include <ast/ast.hpp>

Token::~Token()
{
    switch(m_type)
    {
    case TOK_INTERPOLATED_TYPE:
        delete reinterpret_cast<TypeRef*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_PATTERN:
        delete reinterpret_cast<AST::Pattern*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_PATH:
        delete reinterpret_cast<AST::Path*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_EXPR:
        delete reinterpret_cast<AST::ExprNode*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_STMT:
        delete reinterpret_cast<AST::ExprNode*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_BLOCK:
        delete reinterpret_cast<AST::ExprNode*>(m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_META:
        delete reinterpret_cast<AST::MetaItem*>(m_data.as_Fragment());
        break;
    default:
        break;
    }
    
}

Token::Token():
    m_type(TOK_NULL)
{
}
Token::Token(enum eTokenType type):
    m_type(type)
{
}
Token::Token(enum eTokenType type, ::std::string str):
    m_type(type),
    m_data(Data::make_String(mv$(str)))
{
}
Token::Token(uint64_t val, enum eCoreType datatype):
    m_type(TOK_INTEGER),
    m_data( Data::make_Integer({datatype, val}) )
{
}
Token::Token(double val, enum eCoreType datatype):
    m_type(TOK_FLOAT),
    m_data( Data::make_Float({datatype, val}) )
{
}
Token::Token(InterpolatedFragment& frag)
{
    switch(frag.m_type)
    {
    case InterpolatedFragment::TT:  throw "";
    case InterpolatedFragment::TYPE:
        m_type = TOK_INTERPOLATED_TYPE;
        m_data = new TypeRef( *reinterpret_cast<TypeRef*>(frag.m_ptr) );
        break;
    case InterpolatedFragment::PAT:
        m_type = TOK_INTERPOLATED_PATTERN;
        m_data = new AST::Pattern( reinterpret_cast<AST::Pattern*>(frag.m_ptr)->clone() );
        break;
    case InterpolatedFragment::PATH:
        m_type = TOK_INTERPOLATED_PATH;
        m_data = new AST::Path( *reinterpret_cast<AST::Path*>(frag.m_ptr) );
        break;
    case InterpolatedFragment::EXPR:
        m_type = TOK_INTERPOLATED_EXPR;
        m_data = reinterpret_cast<AST::ExprNode*>(frag.m_ptr)->clone().release();
        break;
    case InterpolatedFragment::STMT:
        m_type = TOK_INTERPOLATED_STMT;
        m_data = reinterpret_cast<AST::ExprNode*>(frag.m_ptr)->clone().release();
        break;
    case InterpolatedFragment::BLOCK:
        m_type = TOK_INTERPOLATED_BLOCK;
        m_data = reinterpret_cast<AST::ExprNode*>(frag.m_ptr)->clone().release();
        break;
    case InterpolatedFragment::META:
        m_type = TOK_INTERPOLATED_META;
        m_data = new AST::MetaItem( reinterpret_cast<AST::MetaItem*>(frag.m_ptr)->clone() );
        break;
    }
}

Token Token::clone() const
{
    Token   rv(m_type);
    rv.m_pos = m_pos;
    
    assert( m_data.tag() != Data::TAGDEAD );
    TU_MATCH(Data, (m_data), (e),
    (None,
        ),
    (String,
        rv.m_data = Data::make_String(e);
        ),
    (Integer,
        rv.m_data = Data::make_Integer(e);
        ),
    (Float,
        rv.m_data = Data::make_Float(e);
        ),
    (Fragment,
        assert(e);
        switch(m_type)
        {
        case TOK_INTERPOLATED_TYPE:
            rv.m_data = new TypeRef( *reinterpret_cast<TypeRef*>(e) );
            break;
        case TOK_INTERPOLATED_PATTERN:
            rv.m_data = new AST::Pattern( reinterpret_cast<AST::Pattern*>(e)->clone() );
            break;
        case TOK_INTERPOLATED_PATH:
            rv.m_data = new AST::Path( *reinterpret_cast<AST::Path*>(e) );
            break;
        case TOK_INTERPOLATED_EXPR:
            rv.m_data = reinterpret_cast<AST::ExprNode*>(e)->clone().release();
            break;
        case TOK_INTERPOLATED_STMT:
            rv.m_data = reinterpret_cast<AST::ExprNode*>(e)->clone().release();
            break;
        case TOK_INTERPOLATED_BLOCK:
            rv.m_data = reinterpret_cast<AST::ExprNode*>(e)->clone().release();
            break;
        case TOK_INTERPOLATED_META:
            rv.m_data = new AST::MetaItem( reinterpret_cast<AST::MetaItem*>(e)->clone() );
            break;
        default:
            assert(!"Token::clone() - fragment with invalid token type");
            break;
        }
        )
    )
    return rv;
}

::std::unique_ptr<AST::ExprNode> Token::take_frag_node()
{
    assert( m_type == TOK_INTERPOLATED_EXPR || m_type == TOK_INTERPOLATED_STMT || m_type == TOK_INTERPOLATED_BLOCK );
    auto ptr = m_data.as_Fragment();
    m_data.as_Fragment() = nullptr;
    return ::std::unique_ptr<AST::ExprNode>( reinterpret_cast<AST::ExprNode*>( ptr ) );
}

const char* Token::typestr(enum eTokenType type)
{
    switch(type)
    {
    #define _(t)    case t: return #t;
    #include "eTokenType.enum.h"
    #undef _
    }
    return ">>BUGCHECK: BADTOK<<";
}

enum eTokenType Token::typefromstr(const ::std::string& s)
{
    if(s == "")
        return TOK_NULL;
    #define _(t)    else if( s == #t ) return t;
    #include "eTokenType.enum.h"
    #undef _
    else
        return TOK_NULL;
}

struct EscapedString {
    const ::std::string& s;
    EscapedString(const ::std::string& s): s(s) {}
    
    friend ::std::ostream& operator<<(::std::ostream& os, const EscapedString& x) {
        for(auto b : x.s) {
            switch(b)
            {
            case '"':
                os << "\\\"";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '\n':
                os << "\\n";
                break;
            default:
                if( ' ' <= b && b < 0x7F )
                    os << b;
                else
                    os << "\\u{" << ::std::hex << (unsigned int)b << "}";
                break;
            }
        }
        return os;
    }
};

::std::string Token::to_str() const
{
    switch(m_type)
    {
    case TOK_NULL:  return "/*null*/"; 
    case TOK_EOF:   return "/*eof*/";

    case TOK_NEWLINE:    return "\n";
    case TOK_WHITESPACE: return " ";
    case TOK_COMMENT:    return "/*" + m_data.as_String() + "*/";
    case TOK_INTERPOLATED_TYPE: return "/*:ty*/";
    case TOK_INTERPOLATED_PATH: return "/*:path*/";
    case TOK_INTERPOLATED_PATTERN: return "/*:pat*/";
    case TOK_INTERPOLATED_EXPR: return "/*:expr*/";
    case TOK_INTERPOLATED_STMT: return "/*:stmt*/";
    case TOK_INTERPOLATED_BLOCK: return "/*:block*/";
    case TOK_INTERPOLATED_META: return "/*:meta*/";
    // Value tokens
    case TOK_IDENT:     return m_data.as_String();
    case TOK_MACRO:     return m_data.as_String() + "!";
    case TOK_LIFETIME:  return "'" + m_data.as_String();
    case TOK_INTEGER:   return FMT(m_data.as_Integer().m_intval);    // TODO: suffix for type
    case TOK_CHAR:      return FMT("'\\u{"<< ::std::hex << m_data.as_Integer().m_intval << "}");
    case TOK_FLOAT:     return FMT(m_data.as_Float().m_floatval);
    case TOK_STRING:    return FMT("\"" << EscapedString(m_data.as_String()) << "\"");
    case TOK_BYTESTRING:return FMT("b\"" << m_data.as_String() << "\"");
    case TOK_CATTR_OPEN:return "#![";
    case TOK_ATTR_OPEN: return "#[";
    case TOK_UNDERSCORE:return "_";
    // Symbols
    case TOK_PAREN_OPEN:    return "(";
    case TOK_PAREN_CLOSE:   return ")";
    case TOK_BRACE_OPEN:    return "{";
    case TOK_BRACE_CLOSE:   return "}";
    case TOK_LT:    return "<";
    case TOK_GT:    return ">";
    case TOK_SQUARE_OPEN:   return "[";
    case TOK_SQUARE_CLOSE:  return "]";
    case TOK_COMMA:     return ",";
    case TOK_SEMICOLON: return ";";
    case TOK_COLON:     return ":";
    case TOK_DOUBLE_COLON:  return ":";
    case TOK_STAR:  return "*";
    case TOK_AMP:   return "&";
    case TOK_PIPE:  return "|";

    case TOK_FATARROW:  return "=>";       // =>
    case TOK_THINARROW: return "->";      // ->

    case TOK_PLUS:  return "+";
    case TOK_DASH:  return "-";
    case TOK_EXCLAM:    return "!";
    case TOK_PERCENT:   return "%";
    case TOK_SLASH:     return "/";

    case TOK_DOT:   return ".";
    case TOK_DOUBLE_DOT:    return "...";
    case TOK_TRIPLE_DOT:    return "..";

    case TOK_EQUAL: return "=";
    case TOK_PLUS_EQUAL:    return "+=";
    case TOK_DASH_EQUAL:    return "-";
    case TOK_PERCENT_EQUAL: return "%=";
    case TOK_SLASH_EQUAL:   return "/=";
    case TOK_STAR_EQUAL:    return "*=";
    case TOK_AMP_EQUAL:     return "&=";
    case TOK_PIPE_EQUAL:    return "|=";

    case TOK_DOUBLE_EQUAL:  return "==";
    case TOK_EXCLAM_EQUAL:  return "!=";
    case TOK_GTE:    return ">=";
    case TOK_LTE:    return "<=";

    case TOK_DOUBLE_AMP:    return "&&";
    case TOK_DOUBLE_PIPE:   return "||";
    case TOK_DOUBLE_LT:     return "<<";
    case TOK_DOUBLE_GT:     return ">>";
    case TOK_DOUBLE_LT_EQUAL:   return "<=";
    case TOK_DOUBLE_GT_EQUAL:   return ">=";

    case TOK_DOLLAR:    return "$";

    case TOK_QMARK: return "?";
    case TOK_AT:    return "@";
    case TOK_TILDE:     return "~";
    case TOK_BACKSLASH: return "\\";
    case TOK_CARET:     return "^";
    case TOK_CARET_EQUAL:   return "^=";
    case TOK_BACKTICK:  return "`";

    // Reserved Words
    case TOK_RWORD_PUB:     return "pub";
    case TOK_RWORD_PRIV:    return "priv";
    case TOK_RWORD_MUT:     return "mut";
    case TOK_RWORD_CONST:   return "const";
    case TOK_RWORD_STATIC:  return "static";
    case TOK_RWORD_UNSAFE:  return "unsafe";
    case TOK_RWORD_EXTERN:  return "extern";

    case TOK_RWORD_CRATE:   return "crate";
    case TOK_RWORD_MOD:     return "mod";
    case TOK_RWORD_STRUCT:  return "struct";
    case TOK_RWORD_ENUM:    return "enum";
    case TOK_RWORD_TRAIT:   return "trait";
    case TOK_RWORD_FN:      return "fn";
    case TOK_RWORD_USE:     return "use";
    case TOK_RWORD_IMPL:    return "impl";
    case TOK_RWORD_TYPE:    return "type";

    case TOK_RWORD_WHERE:   return "where";
    case TOK_RWORD_AS:      return "as";

    case TOK_RWORD_LET:     return "let";
    case TOK_RWORD_MATCH:   return "match";
    case TOK_RWORD_IF:      return "if";
    case TOK_RWORD_ELSE:    return "else";
    case TOK_RWORD_LOOP:    return "loop";
    case TOK_RWORD_WHILE:   return "while";
    case TOK_RWORD_FOR:     return "for";
    case TOK_RWORD_IN:      return "in";
    case TOK_RWORD_DO:      return "do";

    case TOK_RWORD_CONTINUE:return "continue";
    case TOK_RWORD_BREAK:   return "break";
    case TOK_RWORD_RETURN:  return "return";
    case TOK_RWORD_YIELD:   return "yeild";
    case TOK_RWORD_BOX:     return "box";
    case TOK_RWORD_REF:     return "ref";

    case TOK_RWORD_FALSE:   return "false";
    case TOK_RWORD_TRUE:    return "true";
    case TOK_RWORD_SELF:    return "self";
    case TOK_RWORD_SUPER:   return "super";

    case TOK_RWORD_PROC:    return "proc";
    case TOK_RWORD_MOVE:    return "move";

    case TOK_RWORD_ABSTRACT:return "abstract";
    case TOK_RWORD_FINAL:   return "final";
    case TOK_RWORD_PURE:    return "pure";
    case TOK_RWORD_OVERRIDE:return "override";
    case TOK_RWORD_VIRTUAL: return "virtual";

    case TOK_RWORD_ALIGNOF: return "alignof";
    case TOK_RWORD_OFFSETOF:return "offsetof";
    case TOK_RWORD_SIZEOF:  return "sizeof";
    case TOK_RWORD_TYPEOF:  return "typeof";

    case TOK_RWORD_BE:      return "be";
    case TOK_RWORD_UNSIZED: return "unsized";
    }
    throw ParseError::BugCheck("Reached end of Token::to_str");
}

void operator%(::Serialiser& s, enum eTokenType c) {
    s << Token::typestr(c);
}
void operator%(::Deserialiser& s, enum eTokenType& c) {
    ::std::string   n;
    s.item(n);
    c = Token::typefromstr(n);
}
SERIALISE_TYPE(Token::, "Token", {
    s % m_type;
    s << Token::Data::tag_to_str(m_data.tag());
    TU_MATCH(Token::Data, (m_data), (e),
    (None, ),
    (String,
        s << e;
        ),
    (Integer,
        s % e.m_datatype;
        s.item( e.m_intval );
        ),
    (Float,
        s % e.m_datatype;
        s.item( e.m_floatval );
        ),
    (Fragment,
        assert(!"Serialising interpolated macro fragment");
        )
    )
},{
    s % m_type;
    Token::Data::Tag    tag;
    {
        ::std::string   tag_str;
        s.item( tag_str );
        tag = Token::Data::tag_from_str(tag_str);
    }
    switch(tag)
    {
    case Token::Data::TAGDEAD:  break;
    case Token::Data::TAG_None: break;
    case Token::Data::TAG_String:{ 
        ::std::string str;
        s.item( str );
        m_data = Token::Data::make_String(str);
        break; }
    case Token::Data::TAG_Integer:{ 
        enum eCoreType  dt;
        uint64_t    v;
        s % dt;
        s.item( v );
        m_data = Token::Data::make_Integer({dt, v});
        break; }
    case Token::Data::TAG_Float:{ 
        enum eCoreType  dt;
        double   v;
        s % dt;
        s.item( v );
        m_data = Token::Data::make_Float({dt, v});
        break; }
    case Token::Data::TAG_Fragment:
        assert(!"Serialising interpolated macro fragment");
    }
});

::std::ostream&  operator<<(::std::ostream& os, const Token& tok)
{
    os << Token::typestr(tok.type());
    switch(tok.type())
    {
    case TOK_STRING:
    case TOK_BYTESTRING:
    case TOK_IDENT:
    case TOK_MACRO:
    case TOK_LIFETIME:
        if( tok.m_data.is_String() )
            os << "\"" << EscapedString(tok.str()) << "\"";
        break;
    case TOK_INTEGER:
        if( tok.m_data.is_Integer() )
            os << ":" << tok.intval();
        break;
    case TOK_INTERPOLATED_TYPE:
        os << ":" << *reinterpret_cast<TypeRef*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_PATTERN:
        os << ":" << *reinterpret_cast<AST::Pattern*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_PATH:
        os << ":" << *reinterpret_cast<AST::Path*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_EXPR:
        os << ":" << *reinterpret_cast<AST::ExprNode*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_STMT:
        os << ":" << *reinterpret_cast<AST::ExprNode*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_BLOCK:
        os << ":" << *reinterpret_cast<AST::ExprNode*>(tok.m_data.as_Fragment());
        break;
    case TOK_INTERPOLATED_META:
        os << ":" << *reinterpret_cast<AST::MetaItem*>(tok.m_data.as_Fragment());
        break;
    default:
        break;
    }
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const Position& p)
{
    return os << p.filename << ":" << p.line;
}

