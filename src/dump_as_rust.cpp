/*
 * MRustC - Mutabah's Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 * 
 * dump_as_rust.cpp
 * - Dumps the AST of a crate as rust code (annotated)
 */
#include "ast/expr.hpp"
#include <main_bindings.hpp>

#define IS(v, c)    (dynamic_cast<c*>(&v) != 0)

class RustPrinter:
    public AST::NodeVisitor
{
    ::std::ostream& m_os;
    int m_indent_level;
    bool m_expr_root;   //!< used to allow 'if' and 'match' to behave differently as standalone exprs
public:
    RustPrinter(::std::ostream& os):
        m_os(os),
        m_indent_level(0),
        m_expr_root(false)
    {}
    
    void handle_module(const AST::Module& mod);
    void handle_struct(const AST::Struct& s);
    void handle_enum(const AST::Enum& s);
    void handle_trait(const AST::Trait& s);

    void handle_function(const AST::Item<AST::Function>& f);

    virtual bool is_const() const override { return true; }
    virtual void visit(AST::ExprNode_Block& n) override {
        m_os << "{";
        inc_indent();
        bool is_first = true;
        for( auto& child : n.m_nodes )
        {
            if(is_first) {
                is_first = false;
            } else {
                m_os << ";";
            }
            m_os << "\n";
            m_os << indent();
            m_expr_root = true;
            if( !child.get() )
                m_os << "/* nil */";
            else
                AST::NodeVisitor::visit(child);
        }
        m_os << "\n";
        dec_indent();
        m_os << indent() << "}";
    }
    virtual void visit(AST::ExprNode_Macro& n) override {
        m_expr_root = false;
        m_os << n.m_name << "!( /* TODO: Macro TT */ )";
    }
    virtual void visit(AST::ExprNode_Return& n) override {
        m_expr_root = false;
        m_os << "return ";
        AST::NodeVisitor::visit(n.m_value);
    }
    virtual void visit(AST::ExprNode_LetBinding& n) override {
        m_expr_root = false;
        m_os << "let ";
        print_pattern(n.m_pat);
        m_os << " = ";
        AST::NodeVisitor::visit(n.m_value);
    }
    virtual void visit(AST::ExprNode_Assign& n) override {
        m_expr_root = false;
        AST::NodeVisitor::visit(n.m_slot);
        m_os << " = ";
        AST::NodeVisitor::visit(n.m_value);
    }
    virtual void visit(AST::ExprNode_CallPath& n) override {
        m_expr_root = false;
        m_os << n.m_path;
        m_os << "(";
        bool is_first = true;
        for( auto& arg : n.m_args )
        {
            if(is_first) {
                is_first = false;
            } else {
                m_os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        m_os << ")";
    }
    virtual void visit(AST::ExprNode_CallMethod& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_val);
        m_os << ")." << n.m_method;
        m_os << "(";
        bool is_first = true;
        for( auto& arg : n.m_args )
        {
            if(is_first) {
                is_first = false;
            } else {
                m_os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        m_os << ")";
    }
    virtual void visit(AST::ExprNode_CallObject&) override {
        m_expr_root = false;
        throw ::std::runtime_error("unimplemented ExprNode_CallObject");
    }
    virtual void visit(AST::ExprNode_Match& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;
        m_os << "match ";
        AST::NodeVisitor::visit(n.m_val);
        
        if(expr_root)
        {
            m_os << "\n";
            m_os << indent() << "{\n";
        }
        else
        {
            m_os << " {\n";
            inc_indent();
        }
        
        for( auto& arm : n.m_arms )
        {
            m_os << indent();
            print_pattern( arm.first );
            m_os << " => ";
            AST::NodeVisitor::visit(arm.second);
            m_os << ",\n";
        }
        
        if(expr_root)
        {
            m_os << indent() << "}";
        }
        else
        {
            m_os << indent() << "}";
            dec_indent();
        }
    }
    virtual void visit(AST::ExprNode_If& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;
        m_os << "if ";
        AST::NodeVisitor::visit(n.m_cond);
        if( expr_root )
        {
            m_os << "\n";
            m_os << indent();
        }
        else
        {
            m_os << " ";
        }
        AST::NodeVisitor::visit(n.m_true);
        if(n.m_false.get())
        {
            if( expr_root )
            {
                m_os << "\n";
                m_os << indent() << "else";
                // handle chained if statements nicely
                if( IS(*n.m_false, AST::ExprNode_If) ) {
                    m_expr_root = true;
                    m_os << " ";
                }
                else
                    m_os << "\n" << indent();
            }
            else
            {
                m_os << " else ";
            }
            AST::NodeVisitor::visit(n.m_false);
        }
    }
    virtual void visit(AST::ExprNode_Integer& n) override {
        m_expr_root = false;
        switch(n.m_datatype)
        {
        case CORETYPE_INVAL:    break;
        case CORETYPE_CHAR:
            m_os << "'\\u" << ::std::hex << n.m_value << ::std::dec << "'";
            break;
        case CORETYPE_F32:
        case CORETYPE_F64:
            break;
        case CORETYPE_U8:
        case CORETYPE_U16:
        case CORETYPE_U32:
        case CORETYPE_U64:
        case CORETYPE_UINT:
            m_os << "0x" << ::std::hex << n.m_value << ::std::dec;
            break;
        case CORETYPE_I8:
        case CORETYPE_I16:
        case CORETYPE_I32:
        case CORETYPE_I64:
        case CORETYPE_INT:
            m_os << n.m_value;
            break;
        }
    }
    virtual void visit(AST::ExprNode_StructLiteral& n) override {
        m_expr_root = false;
        m_os << n.m_path << " {\n";
        inc_indent();
        for( const auto& i : n.m_values )
        {
            m_os << indent() << i.first << ": ";
            AST::NodeVisitor::visit(i.second);
            m_os << ",\n";
        }
        if( n.m_base_value.get() )
        {
            m_os << indent() << ".. ";
            AST::NodeVisitor::visit(n.m_base_value);
            m_os << "\n";
        }
        m_os << indent() << "}";
        dec_indent();
    }
    virtual void visit(AST::ExprNode_Tuple& n) override {
        m_expr_root = false;
        m_os << "(";
        for( auto& item : n.m_values )
        {
            AST::NodeVisitor::visit(item);
            m_os << ", ";
        }
        m_os << ")";
    }
    virtual void visit(AST::ExprNode_NamedValue& n) override {
        m_expr_root = false;
        m_os << n.m_path;
    }
    virtual void visit(AST::ExprNode_Field& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_obj);
        m_os << ")." << n.m_name;
    }
    virtual void visit(AST::ExprNode_Deref&) override {
        m_expr_root = false;
        throw ::std::runtime_error("unimplemented ExprNode_Deref");
    }
    virtual void visit(AST::ExprNode_Cast& n) override {
        m_expr_root = false;
        AST::NodeVisitor::visit(n.m_value);
        m_os << " as " << n.m_type;
    }
    virtual void visit(AST::ExprNode_BinOp& n) override {
        m_expr_root = false;
        if( IS(*n.m_left, AST::ExprNode_Cast) )
            paren_wrap(n.m_left);
        else if( IS(*n.m_left, AST::ExprNode_BinOp) )
            paren_wrap(n.m_left);
        else
            AST::NodeVisitor::visit(n.m_left);
        m_os << " ";
        switch(n.m_type)
        {
        case AST::ExprNode_BinOp::CMPEQU: m_os << "=="; break;
        case AST::ExprNode_BinOp::CMPNEQU:m_os << "!="; break;
        case AST::ExprNode_BinOp::BITAND: m_os << "&";  break;
        case AST::ExprNode_BinOp::BITOR:  m_os << "|";  break;
        case AST::ExprNode_BinOp::BITXOR: m_os << "^";  break;
        case AST::ExprNode_BinOp::SHL:    m_os << "<<"; break;
        case AST::ExprNode_BinOp::SHR:    m_os << ">>"; break;
        }
        m_os << " ";
        if( IS(*n.m_right, AST::ExprNode_BinOp) )
            paren_wrap(n.m_right);
        else
            AST::NodeVisitor::visit(n.m_right);
    }


private:
    void paren_wrap(::std::unique_ptr<AST::ExprNode>& node) {
        m_os << "(";
        AST::NodeVisitor::visit(node);
        m_os << ")";
    }
    
    void print_params(const AST::TypeParams& params);
    void print_bounds(const AST::TypeParams& params);
    void print_pattern(const AST::Pattern& p);
    
    void inc_indent();
    RepeatLitStr indent();
    void dec_indent();
};

void Dump_Rust(const char *Filename, const AST::Crate& crate)
{
    ::std::ofstream os(Filename);
    RustPrinter printer(os);
    printer.handle_module(crate.root_module());
}

void RustPrinter::handle_module(const AST::Module& mod)
{
    bool need_nl = true;
    
    for( const auto& i : mod.imports() )
    {
        //if(need_nl) {
        //    m_os << "\n";
        //    need_nl = false;
        //}
        m_os << indent() << (i.is_pub ? "pub " : "") << "use " << i.data;
        if( i.name == "" )
        {
            m_os << "::*";
        }
        else if( i.data.nodes().back().name() != i.name )
        {
            m_os << " as " << i.name;
        }
        m_os << ";\n";
    }
    need_nl = true;
    
    for( const auto& sm : mod.submods() )
    {
        m_os << "\n";
        m_os << indent() << (sm.second ? "pub " : "") << "mod " << sm.first.name() << "\n";
        m_os << indent() << "{\n";
        inc_indent();
        handle_module(sm.first);
        dec_indent();
        m_os << indent() << "}\n";
        m_os << "\n";
    }
    
    for( const auto& i : mod.type_aliases() )
    {
        if(need_nl) {
            m_os << "\n";
            need_nl = false;
        }
        m_os << indent() << (i.is_pub ? "pub " : "") << "type " << i.name;
        print_params(i.data.params());
        m_os << " = " << i.data.type();
        print_bounds(i.data.params());
        m_os << ";\n";
    }
    need_nl = true;
    
    for( const auto& i : mod.structs() )
    {
        m_os << "\n";
        m_os << indent() << (i.is_pub ? "pub " : "") << "struct " << i.name;
        handle_struct(i.data);
    }
    
    for( const auto& i : mod.enums() )
    {
        m_os << "\n";
        m_os << indent() << (i.is_pub ? "pub " : "") << "enum " << i.name;
        handle_enum(i.data);
    }
    
    for( const auto& i : mod.traits() )
    {
        m_os << "\n";
        m_os << indent() << (i.is_pub ? "pub " : "") << "trait " << i.name;
        handle_trait(i.data);
    }
    
    for( const auto& i : mod.statics() )
    {
        if(need_nl) {
            m_os << "\n";
            need_nl = false;
        }
        m_os << indent() << (i.is_pub ? "pub " : "");
        switch( i.data.s_class() )
        {
        case AST::Static::CONST:  m_os << "const ";   break;
        case AST::Static::STATIC: m_os << "static ";   break;
        case AST::Static::MUT:    m_os << "static mut ";   break;
        }
        m_os << i.name << ": " << i.data.type() << " = ";
        i.data.value().visit_nodes(*this);
        m_os << ";\n";
    }
    
    for( const auto& i : mod.functions() )
    {
        m_os << "\n";
        handle_function(i);
    }

    for( const auto& i : mod.impls() )
    {
        m_os << "\n";
        m_os << indent() << "impl";
        print_params(i.params());
        if( i.trait() != TypeRef() )
        {
                m_os << " " << i.trait() << " for";
        }
        m_os << " " << i.type() << "\n";
        
        print_bounds(i.params());
        m_os << indent() << "{\n";
        inc_indent();
        for( const auto& t : i.types() )
        {
            m_os << indent() << "type " << t.name << " = " << t.data << ";\n";
        }
        for( const auto& t : i.functions() )
        {
            handle_function(t);
        }
        dec_indent();
        m_os << indent() << "}\n";
    }
}

void RustPrinter::print_params(const AST::TypeParams& params)
{
    if( params.n_params() > 0 )
    {
        bool is_first = true;
        m_os << "<";
        for( const auto& p : params.params() )
        {
            if( !is_first )
                m_os << ", ";
            m_os << p.name();
            is_first = false;
        }
        m_os << ">";
    }
}

void RustPrinter::print_bounds(const AST::TypeParams& params)
{
    if( params.bounds().size() )
    {
        m_os << indent() << "where\n";
        inc_indent();
        bool is_first = true;
        
        for( const auto& b : params.bounds() )
        {
            if( !is_first )
                m_os << ", ";
            is_first = false;
            
            m_os << indent() << b.name() << ": ";
            m_os << "\n";
        }
    
        dec_indent();
    }
}

void RustPrinter::print_pattern(const AST::Pattern& p)
{
    if( p.binding() != "" && p.type() == AST::Pattern::ANY ) {
        m_os << p.binding();
        return;
    }
    
    if( p.binding() != "" )
        m_os << p.binding() << " @ ";
    switch(p.type())
    {
    case AST::Pattern::ANY:
        m_os << "_";
        break;
    case AST::Pattern::VALUE:
        m_os << p.node();
        break;
    case AST::Pattern::MAYBE_BIND:
        m_os << "/*?*/" << p.path();
        break;
    case AST::Pattern::TUPLE_STRUCT:
        m_os << p.path();
    case AST::Pattern::TUPLE:
        m_os << "(";
        for(const auto& sp : p.sub_patterns())
            print_pattern(sp);
        m_os << ")";
        break;
    }
}

void RustPrinter::handle_struct(const AST::Struct& s)
{
    print_params(s.params());
    
    if( s.fields().size() == 0 )
    {
        m_os << "()\n";
        print_bounds(s.params());
        m_os << indent() << ";\n";
    }
    else if( s.fields().size() == 1 && s.fields()[0].name == "" )
    {
        m_os << "(" << "" <<")\n";
        print_bounds(s.params());
        m_os << indent() << ";\n";
    }
    else
    {
        m_os << "\n";
        print_bounds(s.params());
        
        m_os << indent() << "{\n";
        inc_indent();
        for( const auto& i : s.fields() )
        {
            m_os << indent() << (i.is_pub ? "pub " : "") << i.name << ": " << i.data.print_pretty() << "\n";
        }
        dec_indent();
        m_os << indent() << "}\n";
    }
    m_os << "\n";
}

void RustPrinter::handle_enum(const AST::Enum& s)
{
    print_params(s.params());
    m_os << "\n";
    print_bounds(s.params());

    m_os << indent() << "{\n";
    inc_indent();
    for( const auto& i : s.variants() )
    {
        m_os << indent() << i.name;
        if( i.data.sub_types().size() )
            m_os << i.data.print_pretty();
        m_os << ",\n";
    }
    dec_indent();
    m_os << indent() << "}\n";
    m_os << "\n";
}

void RustPrinter::handle_trait(const AST::Trait& s)
{
    print_params(s.params());
    m_os << "\n";
    print_bounds(s.params());

    m_os << indent() << "{\n";
    inc_indent();
    
    for( const auto& i : s.types() )
    {
        m_os << indent() << "type " << i.name << ";\n";
    }
    for( const auto& i : s.functions() )
    {
        handle_function(i);
    }
    
    dec_indent();
    m_os << indent() << "}\n";
    m_os << "\n";
}

void RustPrinter::handle_function(const AST::Item<AST::Function>& f)
{
    m_os << "\n";
    m_os << indent() << (f.is_pub ? "pub " : "") << "fn " << f.name;
    print_params(f.data.params());
    m_os << "(";
    bool is_first = true;
    for( const auto& a : f.data.args() )
    {
        if( !is_first )
            m_os << ", ";
        print_pattern( a.first );
        m_os << ": " << a.second.print_pretty();
        is_first = false;
    }
    m_os << ")";
    if( !f.data.rettype().is_unit() )
    {
        m_os << " -> " << f.data.rettype().print_pretty();
    }
    
    if( f.data.code().is_valid() )
    {
        m_os << "\n";
        print_bounds(f.data.params());
        
        m_os << indent();
        f.data.code().visit_nodes(*this);
        m_os << "\n";
        //m_os << indent() << f.data.code() << "\n";
    }
    else
    {
        print_bounds(f.data.params());
        m_os << ";\n";
    }
}

void RustPrinter::inc_indent()
{
    m_indent_level ++;
}
RepeatLitStr RustPrinter::indent()
{
    return RepeatLitStr { "    ", m_indent_level };
}
void RustPrinter::dec_indent()
{
    m_indent_level --;
}