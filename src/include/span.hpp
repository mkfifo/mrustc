/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * include/span.hpp
 * - Spans and error handling
 */

#pragma once

enum ErrorType
{
    E0000,
};
enum WarningType
{
    W0000,
};

struct ProtoSpan
{
    ::std::string   filename;
    
    unsigned int start_line;
    unsigned int start_ofs;
};
struct Span
{
    ::std::string   filename;
    
    unsigned int start_line;
    unsigned int start_ofs;
    unsigned int end_line;
    unsigned int end_ofs;
    
    Span(::std::string filename, unsigned int start_line, unsigned int start_ofs,  unsigned int end_line, unsigned int end_ofs):
        filename(filename),
        start_line(start_line),
        start_ofs(start_ofs),
        end_line(end_line),
        end_ofs(end_ofs)
    {}
    Span():
        filename("")/*,
        start_line(0), start_ofs(0),
        end_line(0), end_ofs(0) // */
    {}
    
    void bug(::std::function<void(::std::ostream&)> msg) const;
    void error(ErrorType tag, ::std::function<void(::std::ostream&)> msg) const;
    void warning(WarningType tag, ::std::function<void(::std::ostream&)> msg) const;
    void note(::std::function<void(::std::ostream&)> msg) const;
};

#define ERROR(span, code, msg)  do { (span).error(code, [&](::std::ostream& os) { os << msg; }); throw ::std::runtime_error("Error fell through" #code); } while(0)
#define BUG(span, msg)  do { (span).bug([&](::std::ostream& os) { os << msg; }); throw ::std::runtime_error("Bug fell through"); } while(0)
