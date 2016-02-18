#define BOOST_VARIANT_MINIMIZE_SIZE

#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/lex_lexertl.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_statement.hpp>
#include <boost/spirit/include/phoenix_container.hpp>

#include <iostream>
#include <string>

using namespace boost::spirit;
using namespace boost::spirit::ascii;

// Token definition
enum tokenids
{
    IDANY = lex::min_token_id + 10
};

template <typename Lexer>
struct tokens : lex::lexer<Lexer>
{
    tokens()
    {
        this->self.add_pattern
            ("VARIABLE", "[^ \t\n0-9]+")
            ("NUMBER", "[-+]?([0-9]*[.])?[0-9]+")
            ("OP", "[+-*/]");

        variable = "{VARIABLE}";
        number = "{NUMBER}";
        op = "{OP}";

        this->self.add
            (variable)
            (number)
            (op)
            //('\n')
            //(".", IDANY);
        ;
    }

    lex::token_def<std::string> variable, number, op;
};

// Grammar definition
template <typename Iterator>
struct grammar : qi::grammar<Iterator, ascii::space_type>
{
    template <typename TokenDef>
    grammar(TokenDef const& tok)
      : grammar::base_type(statement)
    {
        using boost::phoenix::ref;
        using boost::phoenix::size;

        statement = expr >> tok.op >> statement | expr;
        expr = tok.number | expr >> tok.op >> statement | '(' >> expr >> ')';
    }

    qi::rule<Iterator, ascii::space_type> statement, expr;
};

int main(int argc, char* argv[])
{
	typedef lex::lexertl::token<char const*,
            boost::mpl::vector<std::string> > token_type;
	typedef lex::lexertl::lexer<token_type> lexer_type;
	typedef tokens<lexer_type>::iterator_type iterator_type;

	// Create lexer and grammar
    tokens<lexer_type> lex;
    grammar<iterator_type> g(lex);
    //boost::spirit::ascii::space_type space; // skip spaces

    // Input
    std::string str = "input string to parse";
    char const* first = str.c_str();
    char const* last = &first[str.size()];

	bool r = lex::tokenize_and_phrase_parse(first, last, lex, g, space);

    if (r)
    {
        std::cout << "Success" << std::endl;
    }
    else
    {
        std::string rest(first, last);
        std::cerr << "Parsing failed\n" << "stopped at: \""
                  << rest << "\"" << std::endl;
    }

    return 0;
}

/*int main(int argc, char* argv[])
{
	typedef lex::lexertl::token<
            boost::mpl::vector<std::string>,
            boost::mpl::vector<std::string> > token_type;
	typedef lex::lexertl::lexer<token_type> lexer_type;
	typedef tokens<lexer_type>::iterator_type iterator_type;

	// Create lexer and grammar
    tokens<lexer_type> lex;
    grammar<iterator_type> g(lex);
    boost::spirit::ascii::space_type space; // skip spaces

    // Interactively get input
    std::cout << "Expression parser...\n\n";
    std::cout << "Type an expression...or [q or Q] to quit\n\n";

    std::string str;
    while (std::getline(std::cin, str))
    {
        if (str.empty() || str[0] == 'q' || str[0] == 'Q')
            break;

        std::string::const_iterator iter = str.begin();
        std::string::const_iterator end = str.end();
        bool r = lex::tokenize_and_phrase_parse(iter, end, lex, g, space);

        if (r && iter == end)
        {
            std::cout << "-------------------------\n";
            std::cout << "Parsing succeeded\n";
            std::cout << "-------------------------\n";
        }
        else
        {
            std::string rest(iter, end);
            std::cout << "-------------------------\n";
            std::cout << "Parsing failed\n";
            std::cout << "stopped at: \" " << rest << "\"\n";
            std::cout << "-------------------------\n";
        }
    }

    return 0;
}*/
