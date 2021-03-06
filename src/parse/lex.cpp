/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * parse/lex.cpp
 * - Lexer (converts input file to token stream)
 *
 * Provides:
 * - Lexer : The file->token lexer
 * - TTStream : A stream of tokens from a TokenTree
 * - TokenStream : Common interface for all token streams
 */
#include "lex.hpp"
#include "tokentree.hpp"
#include "parseerror.hpp"
#include "../common.hpp"
#include <cassert>
#include <iostream>
#include <cstdlib>  // strtol
#include <typeinfo>
#include <algorithm>	// std::count

//const bool DEBUG_PRINT_TOKENS = false;
const bool DEBUG_PRINT_TOKENS = true;

Lexer::Lexer(const ::std::string& filename):
    m_path(filename.c_str()),
    m_line(1),
    m_line_ofs(0),
    m_istream(filename.c_str()),
    m_last_char_valid(false)
{
    if( !m_istream.is_open() )
    {
        throw ::std::runtime_error("Unable to open file");
    }
    // Consume the BOM
    if( this->getc() == '\xef' )
    {
        if( this->getc() != '\xbb' ) {
        }
        if( this->getc() != '\xbf' ) {
        }
        m_line_ofs = 0;
    }
    else
    {
        this->ungetc();
    }
}


#define LINECOMMENT -1
#define BLOCKCOMMENT -2
#define SINGLEQUOTE -3
#define DOUBLEQUOTE -4
#define SHEBANG -5

// NOTE: This array must be kept sorted, or symbols are will be skipped
#define TOKENT(str, sym)    {sizeof(str)-1, str, sym}
static const struct {
    unsigned char len;
    const char* chars;
    signed int type;
} TOKENMAP[] = {
  TOKENT("!" , TOK_EXCLAM),
  TOKENT("!=", TOK_EXCLAM_EQUAL),
  TOKENT("\"", DOUBLEQUOTE),
  TOKENT("#",  0),
  TOKENT("#!", SHEBANG),
  TOKENT("#![",TOK_CATTR_OPEN),
  TOKENT("#[", TOK_ATTR_OPEN),
  TOKENT("$",  TOK_DOLLAR),
  TOKENT("%" , TOK_PERCENT),
  TOKENT("%=", TOK_PERCENT_EQUAL),
  TOKENT("&" , TOK_AMP),
  TOKENT("&&", TOK_DOUBLE_AMP),
  TOKENT("&=", TOK_AMP_EQUAL),
  TOKENT("'" , SINGLEQUOTE),
  TOKENT("(" , TOK_PAREN_OPEN),
  TOKENT(")" , TOK_PAREN_CLOSE),
  TOKENT("*" , TOK_STAR),
  TOKENT("*=", TOK_STAR_EQUAL),
  TOKENT("+" , TOK_PLUS),
  TOKENT("+=", TOK_PLUS_EQUAL),
  TOKENT("," , TOK_COMMA),
  TOKENT("-" , TOK_DASH),
  TOKENT("-=", TOK_DASH_EQUAL),
  TOKENT("->", TOK_THINARROW),
  TOKENT(".",  TOK_DOT),
  TOKENT("..", TOK_DOUBLE_DOT),
  TOKENT("...",TOK_TRIPLE_DOT),
  TOKENT("/" , TOK_SLASH),
  TOKENT("/*", BLOCKCOMMENT),
  TOKENT("//", LINECOMMENT),
  TOKENT("/=", TOK_SLASH_EQUAL),
  // 0-9 :: Elsewhere
  TOKENT(":",  TOK_COLON),
  TOKENT("::", TOK_DOUBLE_COLON),
  TOKENT(";",  TOK_SEMICOLON),
  TOKENT("<",  TOK_LT),
  TOKENT("<<", TOK_DOUBLE_LT),
  TOKENT("<<=",TOK_DOUBLE_LT_EQUAL),
  TOKENT("<=", TOK_LTE),
  TOKENT("=" , TOK_EQUAL),
  TOKENT("==", TOK_DOUBLE_EQUAL),
  TOKENT("=>", TOK_FATARROW),
  TOKENT(">",  TOK_GT),
  TOKENT(">=", TOK_GTE),
  TOKENT(">>", TOK_DOUBLE_GT),
  TOKENT(">>=",TOK_DOUBLE_GT_EQUAL),
  TOKENT("?",  TOK_QMARK),
  TOKENT("@",  TOK_AT),
  // A-Z :: Elsewhere
  TOKENT("[",  TOK_SQUARE_OPEN),
  TOKENT("\\", TOK_BACKSLASH),
  TOKENT("]",  TOK_SQUARE_CLOSE),
  TOKENT("^",  TOK_CARET),
  TOKENT("^=", TOK_CARET_EQUAL),
  TOKENT("`",  TOK_BACKTICK),
  // a-z :: Elsewhere
  //TOKENT("b\"", DOUBLEQUOTE),

  TOKENT("{",  TOK_BRACE_OPEN),
  TOKENT("|",  TOK_PIPE),
  TOKENT("|=", TOK_PIPE_EQUAL),
  TOKENT("||", TOK_DOUBLE_PIPE),
  TOKENT("}",  TOK_BRACE_CLOSE),
  TOKENT("~",  TOK_TILDE),
};
#define LEN(arr)    (sizeof(arr)/sizeof(arr[0]))
static const struct {
    unsigned char len;
    const char* chars;
    signed int type;
} RWORDS[] = {
  TOKENT("_", TOK_UNDERSCORE),
  TOKENT("abstract",TOK_RWORD_ABSTRACT),
  TOKENT("alignof", TOK_RWORD_ALIGNOF),
  TOKENT("as",      TOK_RWORD_AS),
  TOKENT("be",      TOK_RWORD_BE),
  TOKENT("box",     TOK_RWORD_BOX),
  TOKENT("break",   TOK_RWORD_BREAK),
  TOKENT("const",   TOK_RWORD_CONST),
  TOKENT("continue",TOK_RWORD_CONTINUE),
  TOKENT("crate",   TOK_RWORD_CRATE),
  TOKENT("do",      TOK_RWORD_DO),
  TOKENT("else",    TOK_RWORD_ELSE),
  TOKENT("enum",    TOK_RWORD_ENUM),
  TOKENT("extern",  TOK_RWORD_EXTERN),
  TOKENT("false",   TOK_RWORD_FALSE),
  TOKENT("final",   TOK_RWORD_FINAL),
  TOKENT("fn",      TOK_RWORD_FN),
  TOKENT("for",     TOK_RWORD_FOR),
  TOKENT("if",      TOK_RWORD_IF),
  TOKENT("impl",    TOK_RWORD_IMPL),
  TOKENT("in",      TOK_RWORD_IN),
  TOKENT("let",     TOK_RWORD_LET),
  TOKENT("loop",    TOK_RWORD_LOOP),
  TOKENT("match",   TOK_RWORD_MATCH),
  TOKENT("mod",     TOK_RWORD_MOD),
  TOKENT("move",    TOK_RWORD_MOVE),
  TOKENT("mut",     TOK_RWORD_MUT),
  TOKENT("offsetof",TOK_RWORD_OFFSETOF),
  TOKENT("override",TOK_RWORD_OVERRIDE),
  TOKENT("priv",    TOK_RWORD_PRIV),
  TOKENT("proc",    TOK_RWORD_PROC),
  TOKENT("pub",     TOK_RWORD_PUB),
  TOKENT("pure",    TOK_RWORD_PURE),
  TOKENT("ref",     TOK_RWORD_REF),
  TOKENT("return",  TOK_RWORD_RETURN),
  TOKENT("self",    TOK_RWORD_SELF),
  TOKENT("sizeof",  TOK_RWORD_SIZEOF),
  TOKENT("static",  TOK_RWORD_STATIC),
  TOKENT("struct",  TOK_RWORD_STRUCT),
  TOKENT("super",   TOK_RWORD_SUPER),
  TOKENT("trait",   TOK_RWORD_TRAIT),
  TOKENT("true",    TOK_RWORD_TRUE),
  TOKENT("type",    TOK_RWORD_TYPE),
  TOKENT("typeof",  TOK_RWORD_TYPEOF),
  TOKENT("unsafe",  TOK_RWORD_UNSAFE),
  TOKENT("unsized", TOK_RWORD_UNSIZED),
  TOKENT("use",     TOK_RWORD_USE),
  TOKENT("virtual", TOK_RWORD_VIRTUAL),
  TOKENT("where",   TOK_RWORD_WHERE),
  TOKENT("while",   TOK_RWORD_WHILE),
  TOKENT("yield",   TOK_RWORD_YIELD),
};

signed int Lexer::getSymbol()
{
    Codepoint ch = this->getc();
    // 1. lsearch for character
    // 2. Consume as many characters as currently match
    // 3. IF: a smaller character or, EOS is hit - Return current best
    unsigned ofs = 0;
    signed int best = 0;
    for(unsigned i = 0; i < LEN(TOKENMAP); i ++)
    {
        const char* const chars = TOKENMAP[i].chars;
        const size_t len = TOKENMAP[i].len;

        if( ofs >= len || static_cast<uint32_t>(chars[ofs]) > ch.v ) {
            this->ungetc();
            return best;
        }

        while( chars[ofs] && ch == chars[ofs] )
        {
            try {
                ch = this->getc();
            }
            catch(Lexer::EndOfFile) {
                ch = 0;
            }
            ofs ++;
        }
        if( chars[ofs] == 0 )
        {
            best = TOKENMAP[i].type;
        }
    }

    this->ungetc();
    return best;
}

bool issym(Codepoint ch)
{
    if( ::std::isalnum(ch.v) )
        return true;
    if( ch == '_' )
        return true;
    if( ch.v >= 128 )
        return true;
    return false;
}

Position Lexer::getPosition() const
{
    return Position(m_path, m_line, m_line_ofs);
}
Token Lexer::realGetToken()
{
    while(true)
    {
        Token tok = getTokenInt();
        //::std::cout << "getTokenInt: tok = " << tok << ::std::endl;
        switch(tok.type())
        {
        case TOK_NEWLINE:
            m_line ++;
            m_line_ofs = 0;
            //DEBUG("m_line = " << m_line << " (NL)");
            continue;
        case TOK_WHITESPACE:
            continue;
        case TOK_COMMENT: {
            ::std::string comment = tok.str();
            unsigned int c = ::std::count(comment.begin(), comment.end(), '\n');
            m_line += c;
            //DEBUG("m_line = " << m_line << " (comment w/ "<<c<<")");
            continue; }
        default:
            return tok;
        }
    }
}

Token Lexer::getTokenInt()
{
    if( this->m_next_token.type() != TOK_NULL )
    {
        return ::std::move(this->m_next_token);
    }
    try
    {
        Codepoint ch = this->getc();
        
        if( ch == '#' && m_line == 1 && m_line_ofs == 1 ) {
            switch( (ch = this->getc()).v )
            {
            case '!':
                switch( (ch = this->getc()).v )
                {
                case '/':
                    // SHEBANG!
                    while( ch != '\n' )
                        ch = this->getc();
                    return Token(TOK_NEWLINE);
                case '[':
                    return Token(TOK_CATTR_OPEN);
                default:
                    throw ParseError::BadChar(*this, ch.v);
                }
            case '[':
                return Token(TOK_ATTR_OPEN);
            default:
                this->ungetc();
                //return Token(TOK_HASH);
                throw ParseError::BadChar(*this, ch.v);
            }
        }

        if( ch == '\n' )
            return Token(TOK_NEWLINE);
        if( ch.isspace() )
        {
            while( (ch = this->getc()).isspace() && ch != '\n' )
                ;
            this->ungetc();
            return Token(TOK_WHITESPACE);
        }
        this->ungetc();

        const signed int sym = this->getSymbol();
        if( sym == 0 )
        {
            // No match at all, check for symbol
            auto ch = this->getc();
            if( ch.isdigit() )
            {
                enum eCoreType  num_type = CORETYPE_ANY;
                enum {
                    BIN,
                    OCT,
                    DEC,
                    HEX,
                } num_mode = DEC;
                
                
                // Handle integers/floats
                uint64_t    val = 0;
                if( ch == '0' ) {
                    // Octal/hex handling
                    ch = this->getc_num();
                    if( ch == 'x' ) {
                        num_mode = HEX;
                        while( (ch = this->getc_num()).isxdigit() )
                        {
                            val *= 16;
                            if(ch.v <= '9')
                                val += ch.v - '0';
                            else if( ch.v <= 'F' )
                                val += ch.v - 'A' + 10;
                            else if( ch.v <= 'f' )
                                val += ch.v - 'a' + 10;
                        }
                    }
                    else if( ch == 'b' ) {
                        num_mode = BIN;
                        while( (ch = this->getc_num()).isdigit() )
                        {
                            val *= 2;
                            if(ch.v == '0')
                                val += 0;
                            else if( ch.v == '1' )
                                val += 1;
                            else
                                throw ParseError::Generic("Invalid digit in binary literal");
                        }
                    }
                    else if( ch == 'o' ) {
                        num_mode = OCT;
                        while( (ch = this->getc_num()).isdigit() ) {
                            val *= 8;
                            if('0' <= ch.v && ch.v <= '7')
                                val += ch.v - '0';
                            else
                                throw ParseError::Generic("Invalid digit in octal literal");
                        }
                    }
                    else {
                        num_mode = DEC;
                        while( ch.isdigit() ) {
                            val *= 10;
                            val += ch.v - '0';
                            ch = this->getc_num();
                        }
                    }
                }
                else {
                    while( ch.isdigit() ) {
                        val *= 10;
                        val += ch.v - '0';
                        ch = this->getc_num();
                    }
                }

                if( ch == 'e' || ch == 'E' || ch == '.' ) {
                    if( num_mode != DEC )
                        throw ParseError::Todo("Non-decimal floats");
                    
                    if( ch == '.' )
                    {
                        ch = this->getc();
                        
                        // Double/Triple Dot
                        if( ch == '.' ) {
                            if( this->getc() == '.') {
                                this->m_next_token = Token(TOK_TRIPLE_DOT);
                            }
                            else {
                                this->ungetc();
                                this->m_next_token = Token(TOK_DOUBLE_DOT);
                            }
                            return Token(val, CORETYPE_ANY);
                        }
                        // Single dot - Still a float. (TODO)
                        else if( !ch.isdigit() )
                        {
                            this->ungetc();
                            if( issym(ch) )
                            {
                                this->m_next_token = Token(TOK_DOT);
                                return Token(val, CORETYPE_ANY);
                            }
                            else
                            {
                                double fval = static_cast<double>(val);
                                return Token(fval, CORETYPE_ANY);
                            }
                        }
                        else
                        {
                            // Digit, continue
                        }
                    }
                    
                    this->ungetc();
                    double fval = this->parseFloat(val);
                    if( issym(ch = this->getc()) )
                    {
                        ::std::string   suffix;
                        while( issym(ch) )
                        {
                            suffix += ch;
                            ch = this->getc();
                        }
                        this->ungetc();
                        
                        if(0)   ;
                        else if(suffix == "f32") num_type = CORETYPE_F32;
                        else if(suffix == "f64") num_type = CORETYPE_F64;
                        else
                            throw ParseError::Generic( FMT("Unknown number suffix " << suffix) );
                    }
                    else
                    {
                        this->ungetc();
                    }
                    return Token( fval, num_type);

                }
                else if( issym(ch)) {
                    // Unsigned
                    ::std::string   suffix;
                    while( issym(ch) )
                    {
                        suffix += ch;
                        ch = this->getc();
                    }
                    this->ungetc();
                    
                    if(0)   ;
                    else if(suffix == "i8")  num_type = CORETYPE_I8;
                    else if(suffix == "i16") num_type = CORETYPE_I16;
                    else if(suffix == "i32") num_type = CORETYPE_I32;
                    else if(suffix == "i64") num_type = CORETYPE_I64;
                    else if(suffix == "isize") num_type = CORETYPE_INT;
                    else if(suffix == "u8")  num_type = CORETYPE_U8;
                    else if(suffix == "u16") num_type = CORETYPE_U16;
                    else if(suffix == "u32") num_type = CORETYPE_U32;
                    else if(suffix == "u64") num_type = CORETYPE_U64;
                    else if(suffix == "usize") num_type = CORETYPE_UINT;
                    else if(suffix == "f32") num_type = CORETYPE_F32;
                    else if(suffix == "f64") num_type = CORETYPE_F64;
                    else
                        throw ParseError::Generic(*this, FMT("Unknown integer suffix '" << suffix << "'"));
                    return Token(val, num_type);
                }
                else {
                    this->ungetc();
                    return Token(val, num_type);
                }
            }
            // Byte/Raw strings
            else if( ch == 'b' || ch == 'r' )
            {
                bool is_byte = false;
                if(ch == 'b') {
                    is_byte = true;
                    ch = this->getc();
                }
                
                if(ch == 'r') {
                    return this->getTokenInt_RawString(is_byte);
                }
                else {
                    assert(is_byte);
                    
                    // Byte string
                    if( ch == '"' ) {
                        ::std::string str;
                        while( (ch = this->getc()) != '"' )
                        {
                            if( ch == '\\' ) {
                                auto v = this->parseEscape('"');
                                if( v != ~0u ) {
                                    if( v > 256 )
                                        throw ParseError::Generic(*this, "Value out of range for byte literal");
                                    str += (char)v;
                                }
                            }
                            else {
                                str += ch;
                            }
                        }
                        return Token(TOK_BYTESTRING, str);
                    }
                    // Byte constant
                    else if( ch == '\'' ) {
                        // Byte constant
                        ch = this->getc();
                        if( ch == '\\' ) {
                            uint32_t val = this->parseEscape('\'');
                            if( this->getc() != '\'' )
                                throw ParseError::Generic(*this, "Multi-byte character literal");
                            return Token((uint64_t)val, CORETYPE_U8);
                        }
                        else {
                            if( this->getc() != '\'' )
                                throw ParseError::Generic(*this, "Multi-byte character literal");
                            return Token((uint64_t)ch.v, CORETYPE_U8);
                        }
                    }
                    else {
                        assert(is_byte);
                        this->ungetc();
                        return this->getTokenInt_Identifier('b');
                    }
                }
            }
            // Symbols
            else if( issym(ch) )
            {
                return this->getTokenInt_Identifier(ch);
            }
            else
            {
                throw ParseError::BadChar(*this, ch.v);
            }
        }
        else if( sym > 0 )
        {
            return Token((enum eTokenType)sym);
        }
        else
        {
            switch(sym)
            {
            case LINECOMMENT: {
                // Line comment
                ::std::string   str;
                auto ch = this->getc();
                while(ch != '\n' && ch != '\r')
                {
                    str += ch;
                    ch = this->getc();
                }
                this->ungetc();
                return Token(TOK_COMMENT, str); }
            case BLOCKCOMMENT: {
                ::std::string   str;
                unsigned int level = 0;
                while(true)
                {
                    ch = this->getc();
                    
                    if( ch == '/' ) {
                        str += ch;
                        ch = this->getc();
                        if( ch == '*' ) {
                            level ++;
                        }
                        str += ch;
                    }
                    else {
                        if( ch == '*' ) {
                            ch = this->getc();
                            if( ch == '/' ) {
                                if( level == 0 )
                                    break;
                                level --;
                                str.push_back('*');
                                str.push_back('/');
                            }
                            else {
                                str.push_back('*');
                                str += ch;
                            }
                        }
                        else {
                            str += ch;
                        }
                    }
                }
                return Token(TOK_COMMENT, str); }
            case SINGLEQUOTE: {
                auto firstchar = this->getc();
                if( firstchar.v == '\\' ) {
                    // Character constant with an escape code
                    uint32_t val = this->parseEscape('\'');
                    if(this->getc() != '\'') {
                        throw ParseError::Todo("Proper error for lex failures");
                    }
                    return Token((uint64_t)val, CORETYPE_CHAR);
                }
                else {
                    ch = this->getc();
                    if( ch == '\'' ) {
                        // Character constant
                        return Token((uint64_t)firstchar.v, CORETYPE_CHAR);
                    }
                    else if( issym(firstchar.v) ) {
                        // Lifetime name
                        ::std::string   str;
                        str += firstchar;
                        while( issym(ch) )
                        {
                            str += ch;
                            ch = this->getc();
                        }
                        this->ungetc();
                        return Token(TOK_LIFETIME, str);
                    }
                    else {
                        throw ParseError::Todo("Lex Fail - Expected ' after character constant");
                    }
                }
                break; }
            case DOUBLEQUOTE: {
                ::std::string str;
                while( (ch = this->getc()) != '"' )
                {
                    if( ch == '\\' )
                    {
                        auto v = this->parseEscape('"');
                        if( v != ~0u )
                        {
                            str += Codepoint(v);
                        }
                    }
                    else
                    {
                        str += ch;
                    }
                }
                return Token(TOK_STRING, str);
                }
            default:
                assert(!"bugcheck");
            }
        }
    }
    catch(const Lexer::EndOfFile& e)
    {
        return Token(TOK_EOF);
    }
    //assert(!"bugcheck");
}

Token Lexer::getTokenInt_RawString(bool is_byte)
{
    // Raw string (possibly byte)
    Codepoint ch = this->getc();
    unsigned int hashes = 0;
    while(ch == '#')
    {
        hashes ++;
        ch = this->getc();
    }
    if( hashes == 0 && ch != '"' ) {
        this->ungetc(); // Unget the not '"'
        if( is_byte )
            return this->getTokenInt_Identifier('b', 'r');
        else
            return this->getTokenInt_Identifier('r');
    }
    auto terminator = ch;
    ::std::string   val;
    DEBUG("terminator = '" << terminator << "', hashes = " << hashes);

    unsigned terminating_hashes = 0;
    for(;;)
    {
        try {
            ch = this->getc();
        }
        catch( Lexer::EndOfFile e ) {
            throw ParseError::Generic(*this, "EOF reached in raw string");
        }
        
        if( terminating_hashes > 0 )
        {
            assert(terminating_hashes > 0);
            if( ch != '#' ) {
                val += terminator;
                while( terminating_hashes < hashes ) {
                    val += '#';
                    terminating_hashes += 1;
                }
                terminating_hashes = 0;
                
                this->ungetc();
            }
            else {
                terminating_hashes -= 1;
                if( terminating_hashes == 0 ) {
                    break;
                }
            }
        }
        else
        {
            if( ch == terminator ) {
                if( hashes == 0 ) {
                    break;
                }
                terminating_hashes = hashes;
            }
            else {
                val += ch;
            }
        }
    }
    return Token(is_byte ? TOK_BYTESTRING : TOK_STRING, val);
}
Token Lexer::getTokenInt_Identifier(Codepoint leader, Codepoint leader2)
{
    ::std::string   str;
    if( leader2 != '\0' )
        str += leader;
    auto ch = leader2 == '\0' ? leader : leader2;
    while( issym(ch) )
    {
        str += ch;
        ch = this->getc();
    }

    if( ch == '!' )
    {
        return Token(TOK_MACRO, str);
    }
    else
    {
        this->ungetc();
        for( unsigned int i = 0; i < LEN(RWORDS); i ++ )
        {
            if( str < RWORDS[i].chars ) break;
            if( str == RWORDS[i].chars )    return Token((enum eTokenType)RWORDS[i].type);
        }
        return Token(TOK_IDENT, str);
    }
}

// Takes the VERY lazy way of reading the float into a string then passing to strtod
double Lexer::parseFloat(uint64_t whole)
{
    const int MAX_LEN = 63;
    const int MAX_SIG = MAX_LEN - 1 - 4;
    char buf[MAX_LEN+1];
    int ofs = snprintf(buf, MAX_LEN+1, "%llu.", (unsigned long long)whole);

    auto ch = this->getc_num();
    #define PUTC(ch)    do { if( ofs < MAX_SIG ) { assert(ch.v < 127); buf[ofs] = ch.v; ofs ++; } else { throw ParseError::Generic("Oversized float"); } } while(0)
    while( ch.isdigit() )
    {
        PUTC(ch);
        ch = this->getc_num();
    }
    if( ch == 'e' || ch == 'E' )
    {
        PUTC(ch);
        ch = this->getc_num();
        if( ch == '-' || ch == '+' ) {
            PUTC(ch);
            ch = this->getc_num();
        }
        if( !ch.isdigit() )
            throw ParseError::Generic( FMT("Non-numeric '"<<ch<<"' in float exponent") );
        do {
            PUTC(ch);
            ch = this->getc_num();
        } while( ch.isdigit() );
    }
    this->ungetc();
    buf[ofs] = 0;
    
    DEBUG("buf = " << buf << ", ch = '" << ch << "'");
    
    return ::std::strtod(buf, NULL);
}

uint32_t Lexer::parseEscape(char enclosing)
{
    auto ch = this->getc();
    switch(ch.v)
    {
    case 'x':
    case 'u': {
        // Unicode (up to six hex digits)
        uint32_t    val = 0;
        ch = this->getc();
        bool req_close_brace = false;
        if( ch == '{' ) {
            req_close_brace = true;
            ch = this->getc();
        }
        if( !ch.isxdigit() )
            throw ParseError::Generic(*this, FMT("Found invalid character '\\x" << ::std::hex << ch.v << "' in \\u sequence" ) );
        while( ch.isxdigit() )
        {
            char    tmp[2] = {static_cast<char>(ch.v), 0};
            val *= 16;
            val += ::std::strtol(tmp, NULL, 16);
            ch = this->getc();
        }
        if( !req_close_brace )
            this->ungetc();
        else if( ch != '}' )
            throw ParseError::Generic(*this, "Expected terminating } in \\u sequence");
        else {
        }
        return val; }
    case '0':
        return '\0';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case 'r':
        return '\r';
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case '\r':
    case '\n':
        m_line ++;
        while( ch.isspace() )
            ch = this->getc();
        this->ungetc();
        if( ch == enclosing )
            return ~0;
        else
            return ch.v;
    default:
        throw ParseError::Todo( FMT("Unknown escape sequence \\" << ch) );
    }
}

char Lexer::getc_byte()
{
    char rv = m_istream.get();
    if( m_istream.eof() )
        throw Lexer::EndOfFile();
    return rv;
}
Codepoint Lexer::getc()
{
    if( m_last_char_valid )
    {
        m_last_char_valid = false;
    }
    else
    {
        m_last_char = this->getc_cp();
        m_line_ofs += 1;
    }
    //::std::cout << "getc(): '" << m_last_char << "'" << ::std::endl;
    return m_last_char;
}

Codepoint Lexer::getc_num()
{
    Codepoint ch;
    do {
        ch = this->getc();
    } while( ch == '_' );
    return ch;
}
Codepoint Lexer::getc_cp()
{
    uint8_t v1 = this->getc_byte();
    if( v1 < 128 ) {
        return {v1};
    }
    else if( (v1 & 0xC0) == 0x80 ) {
        // Invalid (continuation)
        return {0xFFFE};
    }
    else if( (v1 & 0xE0) == 0xC0 ) {
        // Two bytes
        uint8_t e1 = this->getc_byte();
        if( (e1 & 0xC0) != 0x80 )  return {0xFFFE};
        
        uint32_t outval
            = ((v1 & 0x1F) << 6)
            | ((e1 & 0x3F) <<0)
            ;
        return {outval};
    }
    else if( (v1 & 0xF0) == 0xE0 ) {
        // Three bytes
        uint8_t e1 = this->getc_byte();
        if( (e1 & 0xC0) != 0x80 )  return {0xFFFE};
        uint8_t e2 = this->getc_byte();
        if( (e2 & 0xC0) != 0x80 )  return {0xFFFE};
        
        uint32_t outval
            = ((v1 & 0x0F) << 12)
            | ((e1 & 0x3F) <<  6)
            | ((e2 & 0x3F) <<  0)
            ;
        return {outval};
    }
    else if( (v1 & 0xF8) == 0xF0 ) {
        // Four bytes
        uint8_t e1 = this->getc_byte();
        if( (e1 & 0xC0) != 0x80 )  return {0xFFFE};
        uint8_t e2 = this->getc_byte();
        if( (e2 & 0xC0) != 0x80 )  return {0xFFFE};
        uint8_t e3 = this->getc_byte();
        if( (e3 & 0xC0) != 0x80 )  return {0xFFFE};
        
        uint32_t outval
            = ((v1 & 0x07) << 18)
            | ((e1 & 0x3F) << 12)
            | ((e2 & 0x3F) <<  6)
            | ((e3 & 0x3F) <<  0)
            ;
        return {outval};
    }
    else {
        throw ParseError::Generic("Invalid UTF-8 (too long)");
    }
}

void Lexer::ungetc()
{
//    ::std::cout << "ungetc(): " << m_last_char_valid << " '" << m_last_char << "'" << ::std::endl;
    assert(!m_last_char_valid);
    m_last_char_valid = true;
}

TTStream::TTStream(const TokenTree& input_tt)
{
    DEBUG("input_tt = [" << input_tt << "]");
    m_stack.push_back( ::std::make_pair(0, &input_tt) );
}
TTStream::~TTStream()
{
}
Token TTStream::realGetToken()
{
    while(m_stack.size() > 0)
    {
        // If current index is above TT size, go up
        unsigned int& idx = m_stack.back().first;
        const TokenTree& tree = *m_stack.back().second;

        if(idx == 0 && tree.is_token()) {
            idx ++;
            return tree.tok();
        }

        if(idx < tree.size())
        {
            const TokenTree&    subtree = tree[idx];
            idx ++;
            if( subtree.size() == 0 ) {
                return subtree.tok().clone();
            }
            else {
                m_stack.push_back( ::std::make_pair(0, &subtree ) );
            }
        }
        else {
            m_stack.pop_back();
        }
    }
    return Token(TOK_EOF);
}
Position TTStream::getPosition() const
{
    return Position("TTStream", 0,0);
}


TTStreamO::TTStreamO(TokenTree input_tt):
    m_input_tt( mv$(input_tt) )
{
    m_stack.push_back( ::std::make_pair(0, nullptr) );
}
TTStreamO::~TTStreamO()
{
}
Token TTStreamO::realGetToken()
{
    while(m_stack.size() > 0)
    {
        // If current index is above TT size, go up
        unsigned int& idx = m_stack.back().first;
        const TokenTree& tree = *( m_stack.back().second ? m_stack.back().second : &m_input_tt );

        if(idx == 0 && tree.is_token()) {
            idx ++;
            m_last_pos = tree.tok().get_pos();
            return tree.tok();
        }

        if(idx < tree.size())
        {
            const TokenTree&    subtree = tree[idx];
            idx ++;
            if( subtree.size() == 0 ) {
                m_last_pos = subtree.tok().get_pos();
                return subtree.tok();
            }
            else {
                m_stack.push_back( ::std::make_pair(0, &subtree ) );
            }
        }
        else {
            m_stack.pop_back();
        }
    }
    return Token(TOK_EOF);
}
Position TTStreamO::getPosition() const
{
    return m_last_pos;
}


TokenStream::TokenStream():
    m_cache_valid(false)
{
}
TokenStream::~TokenStream()
{
}

Token TokenStream::innerGetToken()
{
    Token ret = this->realGetToken();
    if( ret.get_pos().filename == "" )
        ret.set_pos( this->getPosition() );
    //DEBUG("ret.get_pos() = " << ret.get_pos());
    return ret;
}
Token TokenStream::getToken()
{
    if( m_cache_valid )
    {
        m_cache_valid = false;
        return mv$(m_cache);
    }
    else if( m_lookahead.size() )
    {
        Token ret = mv$( m_lookahead.front() );
        m_lookahead.erase(m_lookahead.begin());
        if( DEBUG_PRINT_TOKENS ) {
            ::std::cout << "getToken[" << typeid(*this).name() << "] - " << ret.get_pos() << "-" << ret << ::std::endl;
        }
        return ret;
    }
    else
    {
        Token ret = this->innerGetToken();
        if( DEBUG_PRINT_TOKENS ) {
            ::std::cout << "getToken[" << typeid(*this).name() << "] - " << ret.get_pos() << "-" << ret << ::std::endl;
        }
        return ret;
    }
}
void TokenStream::putback(Token tok)
{
    if( m_cache_valid )
    {
        DEBUG("" << getPosition() << " - Double putback: " << tok << " but " << m_cache);
        throw ParseError::BugCheck("Double putback");
    }
    else
    {
        m_cache_valid = true;
        m_cache = mv$(tok);
    }
}

eTokenType TokenStream::lookahead(unsigned int i)
{
    const unsigned int MAX_LOOKAHEAD = 3;
    
    if( m_cache_valid )
    {
        if( i == 0 )
            return m_cache.type();
        i --;
    }
    
    if( i >= MAX_LOOKAHEAD )
        throw ParseError::BugCheck("Excessive lookahead");
    
    while( i >= m_lookahead.size() )
    {
        DEBUG("lookahead - read #" << m_lookahead.size());
        m_lookahead.push_back( this->innerGetToken() );
    }
    
    DEBUG("lookahead(" << i << ") = " << m_lookahead[i]);
    return m_lookahead[i].type();
}

ProtoSpan TokenStream::start_span() const
{
    auto p = this->getPosition();
    return ProtoSpan {
        .filename = p.filename,
        .start_line = p.line,
        .start_ofs = p.ofs,
        };
}
Span TokenStream::end_span(ProtoSpan ps) const
{
    auto p = this->getPosition();
    return Span(
        ps.filename,
        ps.start_line, ps.start_ofs,
        p.line, p.ofs
        );
}


TokenTree TokenTree::clone() const
{
    if( m_subtrees.size() == 0 ) {
        return TokenTree(m_tok.clone());
    }
    else {
        ::std::vector< TokenTree>   ents;
        ents.reserve( m_subtrees.size() );
        for(const auto& sub : m_subtrees)
            ents.push_back( sub.clone() );
        return TokenTree( mv$(ents) );
    }
}
SERIALISE_TYPE_A(TokenTree::, "TokenTree", {
    s.item(m_tok);
    s.item(m_subtrees);
})

bool Codepoint::isspace() const {
    switch(this->v)
    {
    case '\t':
    case '\r':
    case '\n':
    case ' ':
        return true;
    default:
        return false;
    }
}
bool Codepoint::isdigit() const {
    return this->v < 128 && std::isdigit(static_cast<int>(this->v));
}
bool Codepoint::isxdigit() const {
    return this->v < 128 && std::isxdigit(static_cast<int>(this->v));
}

::std::string& operator+=(::std::string& s, const Codepoint& cp)
{
    if( cp.v < 0x80 ) {
        s += (char)cp.v;
    }
    else if( cp.v < (0x1F+1)<<(1*6) ) {
        
        s += (char)(0xC0 | ((cp.v >> 6) & 0x1F));
        s += (char)(0x80 | ((cp.v >> 0) & 0x3F));
    }
    else if( cp.v <= (0x0F+1)<<(2*6) ) {
        s += (char)(0xE0 | ((cp.v >> 12) & 0x0F));
        s += (char)(0x80 | ((cp.v >>  6) & 0x3F));
        s += (char)(0x80 | ((cp.v >>  0) & 0x3F));
    }
    else if( cp.v <= (0x07+1)<<(2*6) ) {
        s += (char)(0xF0 | ((cp.v >> 18) & 0x07));
        s += (char)(0x80 | ((cp.v >> 12) & 0x3F));
        s += (char)(0x80 | ((cp.v >>  6) & 0x3F));
        s += (char)(0x80 | ((cp.v >>  0) & 0x3F));
    }
    else {
        throw ::std::runtime_error("BUGCHECK: Bad unicode codepoint encountered");
    }
    return s;
}
::std::ostream& operator<<(::std::ostream& os, const Codepoint& cp)
{
    if( cp.v < 0x80 ) {
        os << (char)cp.v;
    }
    else if( cp.v < (0x1F+1)<<(1*6) ) {
        os << (char)(0xC0 | ((cp.v >> 6) & 0x1F));
        os << (char)(0x80 | ((cp.v >> 0) & 0x3F));
    }
    else if( cp.v <= (0x0F+1)<<(2*6) ) {
        os << (char)(0xE0 | ((cp.v >> 12) & 0x0F));
        os << (char)(0x80 | ((cp.v >>  6) & 0x3F));
        os << (char)(0x80 | ((cp.v >>  0) & 0x3F));
    }
    else if( cp.v <= (0x07+1)<<(2*6) ) {
        os << (char)(0xF0 | ((cp.v >> 18) & 0x07));
        os << (char)(0x80 | ((cp.v >> 12) & 0x3F));
        os << (char)(0x80 | ((cp.v >>  6) & 0x3F));
        os << (char)(0x80 | ((cp.v >>  0) & 0x3F));
    }
    else {
        throw ::std::runtime_error("BUGCHECK: Bad unicode codepoint encountered");
    }
    return os;
}
