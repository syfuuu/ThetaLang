#include <vector>
#include <deque>
#include <string>
#include <map>
#include <iostream>
#include <memory>
#include <filesystem>
#include "../lexer/token.hpp"
#include "../util/exceptions.hpp"
#include "ast/assignment_node.hpp"
#include "ast/unary_operation_node.hpp"
#include "ast/binary_operation_node.hpp"
#include "ast/literal_node.hpp"
#include "ast/identifier_node.hpp"
#include "ast/ast_node.hpp"
#include "ast/type_declaration_node.hpp"
#include "ast/list_definition_node.hpp"
#include "ast/capsule_node.hpp"
#include "ast/function_declaration_node.hpp"
#include "ast/keyed_access_node.hpp"
#include "ast/source_node.hpp"
#include "ast/link_node.hpp"
#include "ast/symbol_node.hpp"
#include "ast/dict_definition_node.hpp"
#include "ast/block_node.hpp"
#include "ast/tuple_node.hpp"
#include "../compiler/compiler.hpp"
#include "../util/tokens.hpp"

using namespace std;

class ThetaParser {
    public:
        shared_ptr<ASTNode> parse(deque<Token> &tokens, string &src, string file, shared_ptr<map<string, string>> filesByCapsuleName) {
            source = src;
            fileName = file;
            remainingTokens = &tokens;
            filesByCapsule = filesByCapsuleName;

            shared_ptr<ASTNode> parsedSource = parseSource();

            // Throw parse errors for any remaining tokens after we've finished our parser run
            for (int i = 0; i < tokens.size(); i++) {
                ThetaCompiler::getInstance().addException(
                    ThetaCompilationError(
                        "ParseError",
                        "Unparsed token " + tokens[i].getLexeme(),
                        tokens[i],
                        source,
                        fileName
                    )
                );
            }

            return parsedSource;
        }

    private:
        string source;
        string fileName;
        deque<Token> *remainingTokens;

        shared_ptr<map<string, string>> filesByCapsule;
        Token currentToken;

        shared_ptr<ASTNode> parseSource() {
            vector<shared_ptr<ASTNode>> links;

            while (match(Token::Types::KEYWORD, Symbols::LINK)) {
                links.push_back(link());
            }

            shared_ptr<SourceNode> sourceNode = make_shared<SourceNode>();
            sourceNode->setLinks(links);
            sourceNode->setValue(capsule());

            return sourceNode;
        }

        shared_ptr<ASTNode> link() {
            match(Token::Types::IDENTIFIER);
            shared_ptr<LinkNode> linkNode = ThetaCompiler::getInstance().getIfExistsParsedLinkAST(currentToken.getLexeme());

            if (linkNode) return linkNode;

            linkNode = make_shared<LinkNode>(currentToken.getLexeme());

            auto fileContainingLinkedCapsule = filesByCapsule->find(currentToken.getLexeme());

            if (fileContainingLinkedCapsule == filesByCapsule->end()) {
                ThetaCompiler::getInstance().addException(
                    ThetaCompilationError(
                        "LinkageError",
                        "Could not find capsule " + currentToken.getLexeme() + " referenced",
                        currentToken,
                        source,
                        fileName
                    )
                );
            } else {
                shared_ptr<ASTNode> linkedAST = ThetaCompiler::getInstance().buildAST(fileContainingLinkedCapsule->second);

                linkNode->setValue(linkedAST);
            }

            ThetaCompiler::getInstance().addParsedLinkAST(currentToken.getLexeme(), linkNode);

            return linkNode;
        }

        shared_ptr<ASTNode> capsule() {
            if (match(Token::Types::KEYWORD, Symbols::CAPSULE)) {
                match(Token::Types::IDENTIFIER);

                shared_ptr<ASTNode> capsule = make_shared<CapsuleNode>(currentToken.getLexeme());
                capsule->setValue(block());

                return capsule;
            }

            return assignment();
        }

        shared_ptr<ASTNode> assignment() {
            shared_ptr<ASTNode> expr = expression();

            if (match(Token::Types::ASSIGNMENT)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<AssignmentNode>();
                expr->setLeft(left);
                expr->setRight(function_declaration());
            }

            return expr;
        }

        shared_ptr<ASTNode> block() {
            if (match(Token::Types::BRACE_OPEN)) {
                vector<shared_ptr<ASTNode>> blockExpr;

                while (!match(Token::Types::BRACE_CLOSE)) {
                    blockExpr.push_back(assignment());
                }

                shared_ptr<BlockNode> block = make_shared<BlockNode>();
                block->setBlockExpressions(blockExpr);

                return block;
            }

            return function_declaration();
        }

        shared_ptr<ASTNode> function_declaration() {
            shared_ptr<ASTNode> expr = assignment();

            if (check(Token::Types::COMMA) || match(Token::Types::FUNC_DECLARATION) || match(Token::Types::PAREN_CLOSE)) {
                vector<shared_ptr<ASTNode>> params;
                shared_ptr<FunctionDeclarationNode> func_def = make_shared<FunctionDeclarationNode>();

                if (expr) params.push_back(expr);

                while(match(Token::Types::COMMA)) {
                    params.push_back(assignment());
                }

                match(Token::Types::PAREN_CLOSE);
                match(Token::Types::FUNC_DECLARATION);

                func_def->setParameters(params);
                func_def->setDefinition(block());

                expr = func_def;
            }

            return expr;
        }

        shared_ptr<ASTNode> expression() {
            return boolean_comparison();
        }

        shared_ptr<ASTNode> boolean_comparison() {
            shared_ptr<ASTNode> expr = equality();

            while (match(Token::Types::OPERATOR, Symbols::OR) || match(Token::Types::OPERATOR, Symbols::AND)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(expression());
            }

            return expr;
        }

        shared_ptr<ASTNode> equality() {
            shared_ptr<ASTNode> expr = comparison();

            while (match(Token::Types::OPERATOR, Symbols::EQUALITY) || match(Token::Types::OPERATOR, Symbols::INEQUALITY)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(comparison());
            }

            return expr;
        }

        shared_ptr<ASTNode> comparison() {
            shared_ptr<ASTNode> expr = term();

            while (
                match(Token::Types::OPERATOR, Symbols::GT) ||
                match(Token::Types::OPERATOR, Symbols::GTEQ) ||
                match(Token::Types::OPERATOR, Symbols::LT) ||
                match(Token::Types::OPERATOR, Symbols::LTEQ)
            ) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(term());
            }

            return expr;
        }

        shared_ptr<ASTNode> term() {
            shared_ptr<ASTNode> expr = factor();

            while (match(Token::Types::OPERATOR, Symbols::MINUS) || match(Token::Types::OPERATOR, Symbols::PLUS)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(factor());
            }

            return expr;
        }

        shared_ptr<ASTNode> factor() {
            shared_ptr<ASTNode> expr = exponent();

            while (match(Token::Types::OPERATOR, Symbols::DIVISION) || match(Token::Types::OPERATOR, Symbols::TIMES)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(exponent());
            }

            return expr;
        }

        shared_ptr<ASTNode> exponent() {
            shared_ptr<ASTNode> expr = unary();

            while (match(Token::Types::OPERATOR, Symbols::EXPOONENT)) {
                shared_ptr<ASTNode> left = expr;

                expr = make_shared<BinaryOperationNode>(currentToken.getLexeme());
                expr->setLeft(left);
                expr->setRight(unary());
            }

            return expr;
        }

        shared_ptr<ASTNode> unary() {
            if (match(Token::Types::OPERATOR, Symbols::NOT) || match(Token::Types::OPERATOR, Symbols::MINUS)) {
                shared_ptr<ASTNode> un = make_shared<UnaryOperationNode>(currentToken.getLexeme());
                un->setValue(unary());

                return un;
            }

            return primary();
        }

        shared_ptr<ASTNode> primary() {
            if (match(Token::Types::BOOLEAN) || match(Token::Types::NUMBER) || match(Token::Types::STRING)) {
                return make_shared<LiteralNode>(currentToken.getType(), currentToken.getLexeme());
            }

            if (match(Token::Types::IDENTIFIER)) {
                return identifier();
            }

            if (match(Token::Types::COLON)) {
                return symbol();
            }

            if (match(Token::Types::BRACKET_OPEN)) {
                return list();
            }

            if (match(Token::Types::BRACE_OPEN)) {
                return dict();
            }

            if (match(Token::Types::PAREN_OPEN)) {
                shared_ptr<ASTNode> expr = expression();

                match(Token::Types::PAREN_CLOSE);

                return expr;
            }

            return nullptr;
        }

        shared_ptr<ASTNode> dict() {
            pair<string, shared_ptr<ASTNode>> p = kvPair();
            shared_ptr<ASTNode> expr = p.second;

            if (p.first == "kv" && expr && expr->getNodeType() == "Tuple") {
               vector<shared_ptr<ASTNode>> el;
                el.push_back(expr);

                while (match(Token::Types::COMMA)) {
                    el.push_back(kvPair().second);
                }

                expr = make_shared<DictDefinitionNode>();
                dynamic_pointer_cast<DictDefinitionNode>(expr)->setElements(el);

                match(Token::Types::BRACE_CLOSE);
            }

            return expr;
        }

        pair<string, shared_ptr<ASTNode>> kvPair() {
            // Because both flows of this function return a tuple, we need a type flag to indicate whether
            // we generated the tuple with the intention of it being a kvPair or not. Otherwise it would
            // be ambiguous and we would accidentally convert dicts with a single key-value pair into a tuple
            string type = "tuple";
            shared_ptr<ASTNode> expr = tuple();

            if (match(Token::Types::COLON)) {
                type = "kv";
                shared_ptr<ASTNode> left = expr;

                if (left->getNodeType() == "Identifier") {
                    left = make_shared<SymbolNode>(dynamic_pointer_cast<IdentifierNode>(left)->getIdentifier());
                }

                expr = make_shared<TupleNode>();
                expr->setLeft(left);
                expr->setRight(expression());
            }

            return make_pair(type, expr);
        }

        shared_ptr<ASTNode> tuple() {
            shared_ptr<ASTNode> expr;

            try {
                expr = expression();
            } catch (ParseError e) {
                if (e.getErrorParseType() == "symbol") remainingTokens->pop_front();
            }

            if (match(Token::Types::COMMA)) {
                shared_ptr<ASTNode> first = expr;

                expr = make_shared<TupleNode>();
                expr->setLeft(first);

                try {
                    expr->setRight(expression());
                } catch (ParseError e) {
                    if (e.getErrorParseType() == "symbol") remainingTokens->pop_front();
                }


                if (!match(Token::Types::BRACE_CLOSE)) {
                    ThetaCompiler::getInstance().addException(
                        ThetaCompilationError(
                            "SyntaxError",
                            "Expected closing brace after tuple definition",
                            remainingTokens->front(),
                            source,
                            fileName
                        )
                    );
                }
            }

            return expr;
        }

        shared_ptr<ASTNode> list() {
            shared_ptr<ListDefinitionNode> listNode = make_shared<ListDefinitionNode>();
            vector<shared_ptr<ASTNode>> el;

            if (!match(Token::Types::BRACKET_CLOSE)) {
                el.push_back(expression());

                while(match(Token::Types::COMMA)) {
                    el.push_back(expression());
                }

                listNode->setElements(el);

                match(Token::Types::BRACKET_CLOSE);
            }

            return listNode;
        }

        shared_ptr<ASTNode> identifier() {
            validateIdentifier(currentToken);

            shared_ptr<ASTNode> ident = make_shared<IdentifierNode>(currentToken.getLexeme());

            if (match(Token::Types::OPERATOR, Symbols::LT)) {
                ident->setValue(type());

                match(Token::Types::OPERATOR, Symbols::GT);
            }

            return ident;
        }

        shared_ptr<ASTNode> type() {
            match(Token::Types::IDENTIFIER);

            shared_ptr<ASTNode> typ = make_shared<TypeDeclarationNode>(currentToken.getLexeme());

            if (match(Token::Types::OPERATOR, Symbols::LT)) {
                typ->setValue(type());

                match(Token::Types::OPERATOR, Symbols::GT);
            }

            return typ;
        }

        shared_ptr<ASTNode> symbol() {
            if (match(Token::Types::IDENTIFIER) || match(Token::Types::NUMBER)) {
                if (currentToken.getType() == Token::Types::IDENTIFIER) validateIdentifier(currentToken);

                return make_shared<SymbolNode>(currentToken.getLexeme());
            }

            ThetaCompiler::getInstance().addException(
                ThetaCompilationError(
                    "SyntaxError",
                    "Expected identifier as part of symbol declaration",
                    remainingTokens->front(),
                    source,
                    fileName
                )
            );

            // TODO: Throw an exception here that gets caught higher up to re-synchronize the parser, so we dont show
            // the user transient errors

            throw ParseError("symbol");

            return nullptr;
        }

        bool match(Token::Types type, string lexeme = "") {
            if (check(type, lexeme)) {
                currentToken = remainingTokens->front();
                remainingTokens->pop_front();
                return true;
            }

            return false;
        }

        bool check(Token::Types type, string lexeme = "") {
            return remainingTokens->size() != 0 &&
                remainingTokens->front().getType() == type &&
                (lexeme != "" ? remainingTokens->front().getLexeme() == lexeme : true);
        }

        /**
         * @brief Validates an identifier token to ensure it conforms to language syntax rules.
         *
         * This method checks each character in the identifier's text to ensure it does not
         * contain disallowed characters or start with a digit. If an invalid character or
         * format is detected, a SyntaxError exception is thrown with details about the error.
         *
         * @param token The token representing the identifier to be validated.
         * @throws SyntaxError If the identifier contains disallowed characters or starts with a digit.
         */
        void validateIdentifier(Token token) {
            string disallowedIdentifierChars = "!@#$%^&*()-=+/<>{}[]|?,`~";

            for (int i = 0; i < token.getLexeme().length(); i++) {
                char identChar = tolower(token.getLexeme()[i]);

                bool isDisallowedChar = find(disallowedIdentifierChars.begin(), disallowedIdentifierChars.end(), identChar) != disallowedIdentifierChars.end();
                bool isStartsWithDigit = i == 0 && isdigit(identChar);

                if (isStartsWithDigit || isDisallowedChar) {
                    ThetaCompiler::getInstance().addException(
                        ThetaCompilationError(
                            "SyntaxError",
                            "Invalid identifier \"" + token.getLexeme() + "\"",
                            token,
                            source,
                            fileName
                        )
                    );
                }
            }
        }
};
