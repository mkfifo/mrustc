/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * main.cpp
 * - Compiler Entrypoint
 */
#include <iostream>
#include <iomanip>
#include <string>
#include <set>
#include "parse/lex.hpp"
#include "parse/parseerror.hpp"
#include "ast/ast.hpp"
#include "ast/crate.hpp"
#include <serialiser_texttree.hpp>
#include <cstring>
#include <main_bindings.hpp>
#include "resolve/main_bindings.hpp"
#include "hir_conv/main_bindings.hpp"
#include "hir_typeck/main_bindings.hpp"
#include "hir_expand/main_bindings.hpp"
#include "mir/main_bindings.hpp"

#include "expand/cfg.hpp"

int g_debug_indent_level = 0;
::std::string g_cur_phase;
::std::set< ::std::string>    g_debug_disable_map;

void init_debug_list()
{
    //g_debug_disable_map.insert( "Parse" );
    g_debug_disable_map.insert( "Expand" );
    g_debug_disable_map.insert( "Resolve" );
    g_debug_disable_map.insert( "Resolve UFCS paths" );
    g_debug_disable_map.insert( "Typecheck Expressions" );
}
bool debug_enabled()
{
    // TODO: Have an explicit enable list?
    if( g_debug_disable_map.count(g_cur_phase) != 0 ) {
        return false;
    }
    else {
        return true;
    }
    //return g_cur_phase == "Lower MIR";
}
::std::ostream& debug_output(int indent, const char* function)
{
    return ::std::cout << g_cur_phase << "- " << RepeatLitStr { " ", indent } << function << ": ";
}

struct ProgramParams
{
    static const unsigned int EMIT_C = 0x1;
    static const unsigned int EMIT_AST = 0x2;
    enum eLastStage {
        STAGE_PARSE,
        STAGE_EXPAND,
        STAGE_RESOLVE,
        STAGE_TYPECK,
        STAGE_BORROWCK,
        STAGE_ALL,
    } last_stage = STAGE_ALL;

    const char *infile = NULL;
    ::std::string   outfile;
    const char *crate_path = ".";
    unsigned emit_flags = EMIT_C;
    
    ProgramParams(int argc, char *argv[]);
};

template <typename Rv, typename Fcn>
Rv CompilePhase(const char *name, Fcn f) {
    ::std::cout << name << ": V V V" << ::std::endl;
    g_cur_phase = name;
    auto start = clock();
    auto rv = f();
    auto end = clock();
    g_cur_phase = "";
    
    //::std::cout << name << ": DONE (" << ::std::fixed << ::std::setprecision(2) << static_cast<double>(end - start) / static_cast<double>(CLOCKS_PER_SEC) << " s)" << ::std::endl;

    ::std::cout <<"(" << ::std::fixed << ::std::setprecision(2) << static_cast<double>(end - start) / static_cast<double>(CLOCKS_PER_SEC) << " s) ";
    ::std::cout << name << ": DONE";
    ::std::cout << ::std::endl;
    return rv;
}
template <typename Fcn>
void CompilePhaseV(const char *name, Fcn f) {
    CompilePhase<int>(name, [&]() { f(); return 0; });
}

/// main!
int main(int argc, char *argv[])
{
    ProgramParams   params(argc, argv);
    
    // Set up cfg values
    // TODO: Target spec
    Cfg_SetFlag("linux");
    Cfg_SetValue("target_pointer_width", "64");
    Cfg_SetValue("target_endian", "little");
    Cfg_SetValue("target_arch", "x86-noasm");   // TODO: asm! macro
    Cfg_SetValueCb("target_has_atomic", [](const ::std::string& s) {
        if(s == "8")    return true;    // Has an atomic byte
        if(s == "ptr")  return true;    // Has an atomic pointer-sized value
        return false;
        });
    Cfg_SetValueCb("target_feature", [](const ::std::string& s) {
        return false;
        });
    
    
    try
    {
        // Parse the crate into AST
        AST::Crate crate = CompilePhase<AST::Crate>("Parse", [&]() {
            return Parse_Crate(params.infile);
            });

        if( params.last_stage == ProgramParams::STAGE_PARSE ) {
            return 0;
        }

        // Load external crates.
        CompilePhaseV("LoadCrates", [&]() {
            crate.load_externs();
            });
    
        // Iterate all items in the AST, applying syntax extensions
        CompilePhaseV("Expand", [&]() {
            Expand(crate);
            });

        // XXX: Dump crate before resolve
        CompilePhaseV("Temp output - Parsed", [&]() {
            Dump_Rust( FMT(params.outfile << "_0_pp.rs").c_str(), crate );
            });

        if( params.last_stage == ProgramParams::STAGE_EXPAND ) {
            return 0;
        }
        
        // Resolve names to be absolute names (include references to the relevant struct/global/function)
        // - This does name checking on types and free functions.
        // - Resolves all identifiers/paths to references
        CompilePhaseV("Resolve", [&]() {
            Resolve_Use(crate); // - Absolutise and resolve use statements
            Resolve_Index(crate); // - Build up a per-module index of avalable names (faster and simpler later resolve)
            Resolve_Absolutise(crate);  // - Convert all paths to Absolute or UFCS, and resolve variables
            });
        
        // XXX: Dump crate before typecheck
        CompilePhaseV("Temp output - Resolved", [&]() {
            Dump_Rust( FMT(params.outfile << "_1_res.rs").c_str(), crate );
            });

        if( params.last_stage == ProgramParams::STAGE_RESOLVE ) {
            return 0;
        }
        
        // --------------------------------------
        // HIR Section
        // --------------------------------------
        // Construc the HIR from the AST
        ::HIR::CratePtr hir_crate = CompilePhase< ::HIR::CratePtr>("HIR Lower", [&]() {
            return LowerHIR_FromAST(mv$( crate ));
            });
        // Deallocate the original crate
        crate = ::AST::Crate();

        // Replace type aliases (`type`) into the actual type
        CompilePhaseV("Resolve Type Aliases", [&]() {
            ConvertHIR_ExpandAliases(*hir_crate);
            });
        CompilePhaseV("Resolve Bind", [&]() {
            ConvertHIR_Bind(*hir_crate);
            });
        CompilePhaseV("Resolve UFCS paths", [&]() {
            ConvertHIR_ResolveUFCS(*hir_crate);
            });
        CompilePhaseV("Constant Evaluate", [&]() {
            ConvertHIR_ConstantEvaluate(*hir_crate);
            });
        
        
        // === Type checking ===
        // - This can recurse and call the MIR lower to evaluate constants
        
        // Check outer items first (types of constants/functions/statics/impls/...)
        // - Doesn't do any expressions except those in types
        CompilePhaseV("Typecheck Outer", [&]() {
            Typecheck_ModuleLevel(*hir_crate);
            });
        // Check the rest of the expressions (including function bodies)
        CompilePhaseV("Typecheck Expressions", [&]() {
            Typecheck_Expressions(*hir_crate);
            });
        // === HIR Expansion ===
        // Annotate how each node's result is used
        CompilePhaseV("Expand HIR Annotate", [&]() {
            HIR_Expand_AnnotateUsage(*hir_crate);
            });
        // - Now that all types are known, closures can be desugared
        CompilePhaseV("Expand HIR Closures", [&]() {
            HIR_Expand_Closures(*hir_crate);
            });
        // - And calls can be turned into UFCS
        CompilePhaseV("Expand HIR Calls", [&]() {
            HIR_Expand_UfcsEverything(*hir_crate);
            });
        // - Ensure that typeck worked (including Fn trait call insertion etc)
        CompilePhaseV("Typecheck Expressions (validate)", [&]() {
            Typecheck_Expressions_Validate(*hir_crate);
            });

        if( params.last_stage == ProgramParams::STAGE_TYPECK ) {
            return 0;
        }
        
        // Lower expressions into MIR
        CompilePhaseV("Lower MIR", [&]() {
            HIR_GenerateMIR(*hir_crate);
            });
        
        // Flatten modules into "mangled" set
        //g_cur_phase = "Flatten";
        //AST::Flat flat_crate = Convert_Flatten(crate);

        // Convert structures to C structures / tagged enums
        //Convert_Render(flat_crate, stdout);
    }
    catch(unsigned int) {}
    //catch(const CompileError::Base& e)
    //{
    //    ::std::cerr << "Parser Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const ::std::exception& e)
    //{
    //    ::std::cerr << "Misc Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const char* e)
    //{
    //    ::std::cerr << "Internal Compiler Error: " << e << ::std::endl;
    //    return 2;
    //}
    return 0;
}

ProgramParams::ProgramParams(int argc, char *argv[])
{
    // Hacky command-line parsing
    for( int i = 1; i < argc; i ++ )
    {
        const char* arg = argv[i];
        
        if( arg[0] != '-' )
        {
            this->infile = arg;
        }
        else if( arg[1] != '-' )
        {
            arg ++; // eat '-'
            for( ; *arg; arg ++ )
            {
                switch(*arg)
                {
                // "-o <file>" : Set output file
                case 'o':
                    if( i == argc - 1 ) {
                        // TODO: BAIL!
                        exit(1);
                    }
                    this->outfile = argv[++i];
                    break;
                default:
                    exit(1);
                }
            }
        }
        else
        {
            if( strcmp(arg, "--crate-path") == 0 ) {
                if( i == argc - 1 ) {
                    // TODO: BAIL!
                    exit(1);
                }
                this->crate_path = argv[++i];
            }
            else if( strcmp(arg, "--emit") == 0 ) {
                if( i == argc - 1 ) {
                    // TODO: BAIL!
                    exit(1);
                }
                
                arg = argv[++i];
                if( strcmp(arg, "ast") == 0 )
                    this->emit_flags = EMIT_AST;
                else if( strcmp(arg, "c") == 0 )
                    this->emit_flags = EMIT_C;
                else {
                    ::std::cerr << "Unknown argument to --emit : '" << arg << "'" << ::std::endl;
                    exit(1);
                }
            }
            else if( strcmp(arg, "--stop-after") == 0 ) {
                if( i == argc - 1 ) {
                    // TODO: BAIL!
                    exit(1);
                }
                
                arg = argv[++i];
                if( strcmp(arg, "parse") == 0 )
                    this->last_stage = STAGE_PARSE;
                else {
                    ::std::cerr << "Unknown argument to --stop-after : '" << arg << "'" << ::std::endl;
                    exit(1);
                }
            }
            else {
                exit(1);
            }
        }
    }
    
    if( this->outfile == "" )
    {
        this->outfile = (::std::string)this->infile + ".o";
    }
}

