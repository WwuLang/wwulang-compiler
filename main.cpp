/*
 * Garrett and Cody Wilson
 * CPTR 350 - Computer Architecture
 *
 * WwuLang Compiler
 *
 * Based on:
 * http://boost.org/doc/libs/1_60_0/libs/spirit/example/qi/compiler_tutorial/calc4.cpp
 */

// This will make it build faster (supposedly). We just have to specify
// what type the elements of our grammar are.
#define BOOST_SPIRIT_NO_PREDEFINED_TERMINALS

#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/foreach.hpp>

#include <string>
#include <vector>
#include <iostream>

// The Abstract Syntax Tree (AST)
namespace client { namespace ast {
    // This part may be nothing
    struct nil {};

    // This part may be an entire expression
    struct expression;

    // Each part of the tree may be any one of these types.
    //
    // Note: we'll postfix a name with an underscore when the name is
    // reserved or we're declaring an instance of a class with the same
    // name.
    typedef boost::variant<
        nil,
        unsigned int,
        boost::recursive_wrapper<expression>
    > operand;

    // This part may consist of some operator (e.g. +) and then some other part
    // of the expression, which may consist of another number (using the
    // unsigned int variant), nothing if this is the end (using nil), etc.
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
        void operator()(unsigned int n) const
        {
            std::cout << n;
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
            ast::expression(),
            // What we're skipping. In this case we probably don't care about
            // whitespace.
            ascii::space_type
        >
    {
        calculator() : calculator::base_type(expression)
        {
            // This is because we did BOOST_SPIRIT_NO_PREDEFINED_TERMINALS
            qi::uint_type uint_;
            qi::char_type char_;

            // Our BNF grammar
            //
            // Handle order of operations, do the addition/subtraction last
            expression = term >> *(char_('+') >> term | char_('-') >> term);

            // Do the multiplication/division before add/subtract
            term = factor >> *(char_('*') >> factor | char_('/') >> factor);

            // Do what's inside parenthesis before mult/div
            factor = '(' >> expression >> ')' | uint_;
        }

        // Specify the iterator, result, and skip types for each
        //
        // The first two result type is expression() because they have the
        // Kleene star, having one term first with a variable number of
        // operator followed by more terms aftewards. The last one doesn't.
        // It's either a subexpression or an integer, which corresponds with
        // our boost::variant that can be one of several types.
        qi::rule<Iterator, ast::expression(), ascii::space_type> expression;
        qi::rule<Iterator, ast::expression(), ascii::space_type> term;
        qi::rule<Iterator, ast::operand(), ascii::space_type> factor;
    };
}

int main()
{
    std::cout << "WwuLang Compiler" << std::endl;

    // Our parser, which will be iterating over a string
    typedef std::string::const_iterator iterator_type;
    typedef client::calculator<iterator_type> calculator;

    // Skip spaces
    boost::spirit::ascii::space_type space;

    // Parser
    calculator calc;

    while (true)
    {
        // Interactively get user input to parse
        std::cout << "> ";
        std::string str;
        std::getline(std::cin, str);

        // Nice way to exit
        if (str.empty() || str[0] == 'q' || str[0] == 'Q')
            break;

        // To parse, Spirit requires that we have iterators, so get the
        // iterators for the string we just read
        std::string::const_iterator iter = str.begin();
        std::string::const_iterator end = str.end();

        // Actually parse the input
        client::ast::expression ast;
        client::ast::printer ast_print;
        bool r = phrase_parse(iter, end, calc, space, ast);

        // If the iterator matches the end iterator of the string,
        // we've parsed the entire string
        if (r && iter == end)
        {
            std::cout << "Parsing succeeded" << std::endl;
            std::cout << "AST: ";
            ast_print(ast);
            std::cout << std::endl;
        }
        // If not, then we stopped early because of some error
        else
        {
            std::string rest(iter, end);
            std::cout << "Parsing failed" << std::endl;
            std::cout << "stopped at: \" " << rest << "\"" << std::endl;
        }
    }

    return 0;
}
