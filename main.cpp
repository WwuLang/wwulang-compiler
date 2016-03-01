/*
 * Garrett and Cody Wilson
 * CPTR 350 - Computer Architecture
 *
 * WwuLang Compiler
 *
 * References:
 *
 * Example used to figure out Spirit parsing and AST generation:
 * http://boost.org/doc/libs/1_60_0/libs/spirit/example/qi/compiler_tutorial/calc4.cpp
 *
 * Example used to take AST to the LLVM IR
 * http://llvm.org/docs/tutorial/LangImpl3.html
 *
 * Example used for mutable variable support
 * http://llvm.org/docs/tutorial/LangImpl7.html
 */

// Allow easy debugging of parser (not working yet)
// #define BOOST_SPIRIT_DEBUG

// This will make it build faster (supposedly). We just have to specify
// what type the elements of our grammar are.
#define BOOST_SPIRIT_NO_PREDEFINED_TERMINALS

#include <map>
#include <cctype>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <iostream>

// For going from AST to LLVM IR
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/NoFolder.h>

// Boost libraries for parsing and constructing the AST
#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/qi_lit.hpp>
#include <boost/spirit/include/qi_real.hpp>
#include <boost/spirit/include/qi_char_class.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/fusion/include/adapt_struct.hpp>

// The Abstract Syntax Tree (AST)
namespace client { namespace ast {
    // This part may be nothing
    struct nil {};

    // This part may be an entire expression. The forward declaration since we
    // have cyclic references (operand may be an expression, but an expression
    // consists of operands).
    struct expression;

    // Each part of the tree may be any one of these types.
    //
    // Note: we'll postfix a name with an underscore when the name is
    // reserved or we're declaring an instance of a class with the same
    // name.
    typedef boost::variant<
        float,
        std::string,
        boost::recursive_wrapper<expression>
    > operand;

    // This part may consist of some operator (e.g. +) and then some other part
    // of the expression, which may consist of another number (using the
    // float variant), nothing if this is the end (using nil), etc.
    struct operation
    {
        char operator_;
        operand operand_;
    };

    // The topmost part will be an "expression", which consists of one or more
    // things (nothing, numbers, or other expressions)
    struct expression
    {
        operand first;
        // Note: this means we'll be using the Kleene star as these in the
        // AST have to correspond directly with how we describe what we
        // can parse
        std::vector<operation> rest;
    };

    // Define a variable to take on the value of an expression
    struct assignment
    {
        std::string variable;
        expression expression_;
    };

    // A program line can either be an assignment or an expression, or nothing
    typedef boost::variant<
        /*nil,*/
        assignment,
        expression
    > program_line;

    // A program can consist of multiple lines
    typedef std::vector<program_line> program;
}}

// Makes this AST struct a "first-class fusion citizen that the grammar can
// utilize." I.e., this is important. For some reason. Don't know why.
BOOST_FUSION_ADAPT_STRUCT(
    client::ast::operation,
    (char, operator_) // Note: you get bizarre errors if you have a comma here
    (client::ast::operand, operand_)
)
BOOST_FUSION_ADAPT_STRUCT(
    client::ast::expression,
    (client::ast::operand, first)
    (std::vector<client::ast::operation>, rest)
)
BOOST_FUSION_ADAPT_STRUCT(
    client::ast::assignment,
    (std::string, variable)
    (client::ast::expression, expression_)
)

// Needed for code generation
const std::string MainName = "main";
static std::unique_ptr<llvm::Module> TheModule;
static std::map<std::string, llvm::AllocaInst*> NamedValues;

// With constant folding (the default):
//typedef llvm::IRBuilder<> builder_type;
// Without constant folding (more interesting IR output):
typedef llvm::IRBuilder<true, llvm::NoFolder> builder_type;

static builder_type Builder(llvm::getGlobalContext());

// Output an error and return null rather than a valid LLVM Value pointer
llvm::Value* ErrorV(const char *s)
{
    std::cerr << "Error: " << s << std::endl;
    return nullptr;
}

// After we create the AST, process it in some manner
namespace client { namespace ast {
    // Output the AST visually
    //
    // We'll be creating one instance of this class and then calling the ()
    // operators passing in the next item in the AST. This is called a
    // 'functor' (nothing to do with that of category theory) or a 'function
    // object.'
    struct printer
    {
        typedef void result_type;

        // When we get nothing, do nothing
        void operator()(nil) const { }

        // If we get a number, print out the number
        void operator()(float n) const
        {
            std::cout << n;
        }

        // If we get a string, print out the variable name
        void operator()(const std::string& s) const
        {
            std::cout << s;
        }

        // If we get an "operation", which consists of an operator (e.g. +) and
        // another operand
        void operator()(const operation& x) const
        {
            // Continue looking at this subtree. E.g., if this operand is nil,
            // then do nothing, but if it's a number, output it, etc.
            boost::apply_visitor(*this, x.operand_);

            // Display what operator this operation consisted of
            switch (x.operator_)
            {
                case '+': std::cout << " +"; break;
                case '-': std::cout << " -"; break;
                case '*': std::cout << " *"; break;
                case '/': std::cout << " /"; break;
                default: std::cout << " ?"; break;
            }
        }

        // If we got an expression, process the first part and then all the
        // other parts as well, outputting spaces between
        void operator()(const expression& x) const
        {
            boost::apply_visitor(*this, x.first);

            for (const operation& op : x.rest)
            {
                std::cout << ' ';
                (*this)(op);
            }
        }

        // For an assignment, output that the result will be read into this
        // variable
        void operator()(const assignment& x) const
        {
            (*this)(x.expression_);
            std::cout << " =" << x.variable << " ";
        }

        // For the whole thing
        void operator()(const program& x) const
        {
            // Don't just call (*this)(x) because then it'll call this same
            // function creating an infinite loop. We want to call this
            // function on what type it actually took on as part of the
            // variant, i.e. whether it's an expression or assignment.
            for (const program_line& line : x)
                boost::apply_visitor(*this, line);
        }
    };

	// From LLVM example
	//
	/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
	/// the function.  This is used for mutable variables etc.
    static llvm::AllocaInst* CreateEntryBlockAlloca(llvm::Function* func,
            const std::string& variableName)
	{
        builder_type TmpBuilder(&func->getEntryBlock(),
                func->getEntryBlock().begin());
        return TmpBuilder.CreateAlloca(
                llvm::Type::getDoubleTy(llvm::getGlobalContext()),
                0, variableName.c_str());
	}

    // Go from AST to LLVM IR
    struct compiler
    {
        typedef llvm::Value* result_type;

        // When we get nothing, do nothing
        llvm::Value* operator()(nil) const { return nullptr; }

        // If we get a number, create an LLVM floating-point number
        llvm::Value* operator()(float n) const
        {
            return llvm::ConstantFP::get(llvm::getGlobalContext(),
                    llvm::APFloat(n));
        }

        // If we get a string, it's a variable name
        llvm::Value* operator()(const std::string& s) const
        {
            // Look up the variable name
            std::map<std::string, llvm::AllocaInst*>::const_iterator it =
                NamedValues.find(s);

            if (it == NamedValues.end() || !it->second)
                return ErrorV("Unknown variable name");

            // Load the value from memory
            return Builder.CreateLoad(it->second, s.c_str());
        }

        // If we get an "operation", which consists of an operator (e.g. +) and
        // another operand
        llvm::Value* operator()(const operation& x, llvm::Value* lhs) const
        {
            // Ignore if left-hand side null
            if (!lhs)
                return nullptr;

            // Continue looking at this subtree. E.g., if this operand is nil,
            // then do nothing, but if it's a number, make it floating point,
            // etc.
            llvm::Value* rhs = boost::apply_visitor(*this, x.operand_);

            // Ignore if right-hand side null
            if (!rhs)
                return nullptr;

            // Create the appropriate operation of the left- and right-hand
            // sides
            switch (x.operator_)
            {
                case '+': return Builder.CreateFAdd(lhs, rhs, "addtmp"); break;
                case '-': return Builder.CreateFSub(lhs, rhs, "subtmp"); break;
                case '*': return Builder.CreateFMul(lhs, rhs, "multmp"); break;
                case '/': return Builder.CreateFDiv(lhs, rhs, "divtmp"); break;
                default:  return ErrorV("invalid binary operator"); break;
            }
        }

        // If we got an expression, process the first part and then all the
        // other parts as well
        llvm::Value* operator()(const expression& x) const
        {
            llvm::Value* state = boost::apply_visitor(*this, x.first);

            for (const operation& op : x.rest)
            {
                state = (*this)(op, state);
            }

            return state;
        }

        // For an assignment, create the variable
        llvm::Value* operator()(const assignment& x) const
        {
            // Evaluate what is on the right side of the assignment operator
            llvm::Value* expression = (*this)(x.expression_);

            // We'll be adding the allocations to the main function
            llvm::Function* func = TheModule->getFunction(MainName);

            assert(func);

            // Create a variable and save the result to it
            llvm::AllocaInst* alloca = CreateEntryBlockAlloca(func, x.variable);
            Builder.CreateStore(expression, alloca);
            NamedValues[x.variable] = alloca;

            // Also return this expression so we don't get a failed-to-compile
            // error
            return expression;
        }

        // For the whole thing, return whatever was the last value
        llvm::Value* operator()(const program& x) const
        {
            llvm::Value* lastValue = nullptr;

            for (const program_line& line : x)
                lastValue = boost::apply_visitor(*this, line);

            return lastValue;
        }
    };
}}

namespace client
{
    // Allow accessing via shorter namespaces
    namespace qi = boost::spirit::qi;
    namespace ascii = boost::spirit::ascii;

    // The grammar
    template <typename Iterator>
    struct calculator
        : qi::grammar<
            // For example, iterating over a std::string or std::vector<char>
            Iterator,
            // What we'll be parsing into, the result
            ast::program(),
            // What we're skipping. In this case we probably don't care about
            // whitespace.
            ascii::space_type
        >
    {
        calculator() : calculator::base_type(program)
        {
            // This is because we did BOOST_SPIRIT_NO_PREDEFINED_TERMINALS
            qi::float_type float_;
            qi::char_type char_;
            qi::lexeme_type lexeme_;
            qi::alnum_type alnum_;
            qi::lit_type lit_;

            // Our BNF grammar
            //
            // A program consists of one or more line with a semicolon separating
            program = (program_line % ';') >> -lit_(";");

            // The program line is either an assignment or an expression
            program_line = assignment | expression;

            // Assignment
            assignment = variable >> lit_("=") >> expression;

            // Handle order of operations, do the addition/subtraction last
            expression = term >> *(char_('+') >> term | char_('-') >> term);

            // Do the multiplication/division before add/subtract
            term = factor >> *(char_('*') >> factor | char_('/') >> factor);

            // Do what's inside parenthesis before mult/div
            factor = '(' >> expression >> ')' | float_ | variable;

            // Variables are alphanumeric
            //
            // We use a lexeme here to make sure it doesn't ignore spaces, i.e.
            // we want to make sure the variable name is only alphanumeric, not
            // alphanumeric separated by spaces
            variable %= lexeme_[+alnum_];

            // Debugging
            BOOST_SPIRIT_DEBUG_NODE(program);
            BOOST_SPIRIT_DEBUG_NODE(program_line);
            BOOST_SPIRIT_DEBUG_NODE(assignment);
            BOOST_SPIRIT_DEBUG_NODE(expression);
            BOOST_SPIRIT_DEBUG_NODE(term);
            BOOST_SPIRIT_DEBUG_NODE(factor);
            BOOST_SPIRIT_DEBUG_NODE(variable);
        }

        // Specify the iterator, result, and skip types for each
        //
        // program is a program, program_line is either an assignment or an
        // expression, and assignment is an assignment.
        //
        // expression and term result type is expression() because they have
        // the Kleene star, having one term first with a variable number of
        // operator followed by more terms aftewards.
        //
        // factor doesn't. It's either a subexpression or an integer, which
        // corresponds with our boost::variant that can be one of several
        // types.
        //
        // The last is just a variable name, which is a string. Actually, it's
        // a vector of chars that can be cast to a string.
        qi::rule<Iterator, ast::program(), ascii::space_type> program;
        qi::rule<Iterator, ast::program_line(), ascii::space_type> program_line;
        qi::rule<Iterator, ast::assignment(), ascii::space_type> assignment;
        qi::rule<Iterator, ast::expression(), ascii::space_type> expression;
        qi::rule<Iterator, ast::expression(), ascii::space_type> term;
        qi::rule<Iterator, ast::operand(), ascii::space_type> factor;
        qi::rule<Iterator, std::string(), ascii::space_type> variable;
    };
}

// We need to create the prototype and the entry point before compiling the
// code since during an assignment it adds allocations to this entry point.
llvm::Function* createMainPrototype()
{
	// Make the function type: double()
	std::vector<llvm::Type*> noArguments;
	llvm::FunctionType* functionType = llvm::FunctionType::get(
		llvm::Type::getDoubleTy(llvm::getGlobalContext()), noArguments, false);
	llvm::Function* func = llvm::Function::Create(
		functionType, llvm::Function::ExternalLinkage, MainName, TheModule.get());

	// Create a new basic block to start insertion into
	llvm::BasicBlock* basicBlock = llvm::BasicBlock::Create(
		llvm::getGlobalContext(), "entry", func);
	Builder.SetInsertPoint(basicBlock);

    return func;
}

// Take some code, e.g. assignment, and wrap it in a main function so that
// it'll actually be able to do something
llvm::Function* createMainFunction(llvm::Value* body,
        llvm::Function* func = nullptr)
{
    // If we don't get the function directly, try to find it
    if (!func)
    {
        // See if we've already created the prototype
        func = TheModule->getFunction(MainName);

        if (!func)
            func = createMainPrototype();

        // For some reason couldn't generate the prototype
        if (!func)
            return nullptr;
    }

	// Create a new basic block to start insertion into
	llvm::BasicBlock* basicBlock = llvm::BasicBlock::Create(
		llvm::getGlobalContext(), "entry", func);
	Builder.SetInsertPoint(basicBlock);

	// Create body through parsing the actual code
	llvm::Value* returnValue = body;

    if (returnValue)
    {
        // Finish off the function.
        Builder.CreateRet(returnValue);

        // Validate the generated code, checking for consistency.
        llvm::verifyFunction(*func);

        return func;
    }

    // Error reading body, remove function
    func->eraseFromParent();
    return nullptr;
}

int main()
{
    std::cout << "WwuLang Compiler" << std::endl;

    // Typedefs to simplify our definitions. Our calculator parser will be
    // iterating over a string.
    typedef std::string::const_iterator iterator_type;
    typedef client::calculator<iterator_type> calculator;

    // Skip spaces
    boost::spirit::ascii::space_type space;

    // Parser
    calculator calc;

    while (true)
    {
        // Recreate this every time we compile something so we don't have
        // multiple entry points, old code, etc.
        TheModule = llvm::make_unique<llvm::Module>(
                "WwuLang JIT Compiler", llvm::getGlobalContext());
        NamedValues.clear();

        // Interactively get user input to parse
        std::cout << "> ";
        std::string str;
        std::getline(std::cin, str);

        // Nice way to exit
        if (str.empty() || str == "q" || str == "Q")
            break;

        // To parse, Spirit requires that we have iterators, so get the
        // iterators for the string we just read
        std::string::const_iterator iter = str.begin();
        std::string::const_iterator end = str.end();

        // Must create the prototype before parsing since when making
        // assignments we'll be initializing memory at the beginning of this
        // main function
        llvm::Function* mainFunction = createMainPrototype();

        // Actually parse the input
        client::ast::program ast;
        client::ast::printer ast_print;
        client::ast::compiler ast_compile;
        bool r = phrase_parse(iter, end, calc, space, ast);

        // If the iterator matches the end iterator of the string,
        // we've parsed the entire string
        if (r && iter == end)
        {
            // AST
            std::cout << "AST: ";
            ast_print(ast);
            std::cout << std::endl;

            // LLVM IR text assembly output
            std::cout << "Compiled: " << std::endl;
            llvm::Value* compiled = ast_compile(ast);
            llvm::Function* program = createMainFunction(compiled, mainFunction);

            if (program)
                program->dump();
            else
                std::cout << "Error: failed to compile" << std::endl;
        }
        // If not, then we stopped early because of some error
        else
        {
            std::string rest(iter, end);
            std::cout << "Parsing failed, stopped at: \""
                << rest << "\"" << std::endl;
        }
    }

    return 0;
}
