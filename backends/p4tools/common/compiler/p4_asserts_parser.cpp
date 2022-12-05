#include "backends/p4tools/common/compiler/p4_asserts_parser.h"

#include <stdint.h>

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include <boost/format.hpp>
#include <boost/optional/optional.hpp>

#include "backends/p4tools/common/core/z3_solver.h"
#include "backends/p4tools/common/lib/util.h"
#include "ir/id.h"
#include "ir/indexed_vector.h"
#include "ir/ir-inline.h"
#include "ir/ir.h"
#include "ir/irutils.h"
#include "lib/big_int_util.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/log.h"
#include "lib/ordered_map.h"
#include "lib/safe_vector.h"
#include "p4tools/common/lib/formulae.h"

namespace P4Tools {

namespace AssertsParser {

static std::vector<std::string> NAMES {
    "Priority",      "Text",        "LineStatementClose", "Number",        "Comment", 
    "StringLiteral", "LeftParen",   "RightParen",         "LeftSParent",   "RightSParent",
    "Dot",           "FieldAccess", "LNot",               "Complement",    "Mul",
    "Percent",       "Slash",       "Minus",              "SaturationSub", "Plus",
    "SaturationAdd", "LessEqual",   "Shl",                "LessThan",      "GreaterEqual",
    "Shr",           "GreaterThan", "NotEqual",           "Equal",         "BAnd",
    "Xor",           "BOr",         "Conjunction",        "Disjunction",   "Implication",
    "Colon",         "Question",    "Semicolon",          "Comma",         "Unknown",
    "EndString",     "End"
};

AssertsParser::AssertsParser(std::vector<std::vector<const IR::Expression*>>& output)
    : restrictionsVec(output) {
    setName("Restrictions");
}

/// The function to get kind from token
Token::Kind Token::kind() const noexcept { return m_kind; }

/// The function to replace the token kind with another kind
void Token::kind(Token::Kind kind) noexcept { m_kind = kind; }

/// Kind comparison function. Allows you to compare token kind with the specified kind and return
/// bool values, replacement for ==
bool Token::is(Token::Kind kind) const noexcept { return m_kind == kind; }

/// Kind comparison function. Allows you to compare token kind with the specified kind and return
/// bool values, replacement for !=
bool Token::is_not(Token::Kind kind) const noexcept { return m_kind != kind; }

/// Functions for multiple comparison kind
bool Token::is_one_of(Token::Kind k1, Token::Kind k2) const noexcept { return is(k1) || is(k2); }

template <typename... Ts>
bool Token::is_one_of(Token::Kind k1, Token::Kind k2, Ts... ks) const noexcept {
    return is(k1) || is_one_of(k2, ks...);
}

/// The function to get lexeme from token
std::string_view Token::lexeme() const noexcept { return m_lexeme; }

/// The function to replace the token lexeme with another lexeme
void Token::lexeme(std::string_view lexeme) noexcept { m_lexeme = lexeme; }

/// Function to get the current character
char Lexer::peek() const noexcept { return *m_beg; }

/// The function advances the iterator one index back and returns the character corresponding to the
/// position of the iterator
char Lexer::prev() noexcept { return *m_beg--; }

/// The function advances the iterator one index forward and returns the character corresponding to
/// the position of the iterator
char Lexer::get() noexcept { return *m_beg++; }

bool isSpace(char c) noexcept {
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
            return true;
        default:
            return false;
    }
}

std::ostream& operator<<(std::ostream& os, const Token::Kind& kind) {
    return os << NAMES.at(static_cast<size_t>(kind));
}

/// The function combines IR expressions separated by the "or" ,"and" operators
/// and also by the implication which is indicated by "(tmp" ,inside the algorithm,
/// for the convenience of determining the order of expressions. At the output, we get one
/// expression. For example, at the input we have a vector of expressions: [IR::Expression, IR::LOr
/// with name "(tmp", IR::Expression, IR::LAnd, IR::Expression] The result will be an IR::Expression
/// equal to !IR::Expression || (IR::Expression && IR::Expression)
const IR::Expression* makeSingleExpr(std::vector<const IR::Expression*> input) {
    const IR::Expression* expr = nullptr;
    for (uint64_t idx = 0; idx < input.size(); idx++) {
        if (input[idx]->is<IR::LOr>()) {
            if (idx + 1 == input.size()) {
                break;
            }
            if (expr == nullptr) {
                expr = new IR::LOr(input[idx - 1], input[idx + 1]);
            } else {
                expr = new IR::LOr(expr, input[idx + 1]);
            }
        }
        if (input[idx]->is<IR::LAnd>()) {
            if (idx + 1 == input.size()) {
                break;
            }
            if (expr == nullptr) {
                expr = new IR::LAnd(input[idx - 1], input[idx + 1]);
            } else {
                expr = new IR::LAnd(expr, input[idx + 1]);
            }
        }
    }

    return expr;
}

/// Determines the token type according to the table key and generates a zombie constant for it.
const IR::Expression* makeConstant(Token input, const IR::Vector<IR::KeyElement>& keyElements,
                                   const IR::Type* leftType) {
    const IR::Type_Base* type = nullptr;
    const IR::Expression* result = nullptr;
    auto inputStr = input.lexeme();
    if (input.is(Token::Kind::Text)) {
        for (const auto* key : keyElements) {
            cstring keyName;
            if (const auto* annotation = key->getAnnotation(IR::Annotation::nameAnnotation)) {
                keyName = annotation->getName();
            }
            BUG_CHECK(keyName.size() > 0, "Key does not have a name annotation.");
            auto annoSize = keyName.size();
            auto tokenLength = inputStr.length();
            if (inputStr.find(keyName, tokenLength - annoSize) == std::string::npos) {
                continue;
            }
            const auto* keyType = key->expression->type;
            if (const auto* bit = keyType->to<IR::Type_Bits>()) {
                type = bit;
            } else if (const auto* varbit = keyType->to<IR::Extracted_Varbits>()) {
                type = varbit;
            } else if (keyType->is<IR::Type_Boolean>()) {
                type = IR::Type_Bits::get(1);
            } else {
                BUG("Unexpected key type %s.", keyType->node_type_name());
            }
            return Utils::getZombieConst(type, 0, std::string(inputStr));
        }
    }
    if (input.is(Token::Kind::Number)) {
        if (leftType == nullptr) {
            return IR::getConstant(IR::Type_Bits::get(32), static_cast<big_int>(inputStr));
        }
        return IR::getConstant(leftType, static_cast<big_int>(inputStr));
    }
    // TODO: Is this the right solution for priorities?
    if (input.is(Token::Kind::Priority)) {
        return Utils::getZombieConst(IR::Type_Bits::get(32), 0, std::string(inputStr));
    }
    BUG_CHECK(result != nullptr,
              "Could not match restriction key label %s was not found in key list.",
              std::string(inputStr));
    return nullptr;
}

/// Determines the right side of the expression starting from the original position and returns a
/// slice of tokens related to the right side and the index of the end of the right side.
/// Returning the end index is necessary so that after moving from the end of the right side
std::pair<std::vector<Token>, size_t> findRightPart(std::vector<Token> tokens, size_t index) {
    size_t idx = index + 1;
    size_t endIdx = 0;
    bool flag = true;
    while (flag) {
        if (idx == tokens.size() ||
            tokens[idx].is_one_of(Token::Kind::Conjunction, Token::Kind::Disjunction,
                                  Token::Kind::RightParen)) {
            endIdx = idx;
            flag = false;
        }
        idx++;
    }

    std::vector<Token> rightTokens;
    for (size_t j = index + 1; j < endIdx; j++) {
        rightTokens.push_back(tokens[j]);
    }
    return std::make_pair(rightTokens, idx);
}

/// Chose a binary expression that correlates to the token kind.
const IR::Expression* pickBinaryExpr(const Token& token, const IR::Expression* leftL,
                                     const IR::Expression* rightL) {
    if (token.is(Token::Kind::Minus)) {
        return new IR::Sub(leftL, rightL);
    }
    if (token.is(Token::Kind::Plus)) {
        return new IR::Add(leftL, rightL);
    }
    if (token.is(Token::Kind::Equal)) {
        return new IR::Equ(leftL, rightL);
    }
    if (token.is(Token::Kind::NotEqual)) {
        return new IR::Neq(leftL, rightL);
    }
    if (token.is(Token::Kind::GreaterThan)) {
        return new IR::Grt(leftL, rightL);
    }
    if (token.is(Token::Kind::GreaterEqual)) {
        return new IR::Geq(leftL, rightL);
    }
    if (token.is(Token::Kind::LessThan)) {
        return new IR::Lss(leftL, rightL);
    }
    if (token.is(Token::Kind::LessEqual)) {
        return new IR::Leq(leftL, rightL);
    }
    if (token.is(Token::Kind::Slash)) {
        return new IR::Div(leftL, rightL);
    }
    if (token.is(Token::Kind::Percent)) {
        return new IR::Mod(leftL, rightL);
    }
    if (token.is(Token::Kind::Shr)) {
        return new IR::Shr(leftL, rightL);
    }
    if (token.is(Token::Kind::Shl)) {
        return new IR::Shl(leftL, rightL);
    }
    if (token.is(Token::Kind::Mul)) {
        return new IR::Mul(leftL, rightL);
    }
    if (token.is(Token::Kind::NotEqual)) {
        return new IR::Neq(leftL, rightL);
    }
    BUG("Unsupported binary expression.");
}

/// Converts a vector of tokens into a single IR:Expression
/// For example, at the input we have a vector of tokens:
/// [key1(Text), ->(Implication), key2(Text), &&(Conjunction), key3(Text)] The result will be an
/// IR::Expression equal to !IR::Expression || (IR::Expression && IR::Expression)
const IR::Expression* getIR(std::vector<Token> tokens,
                            const IR::Vector<IR::KeyElement>& keyElements) {
    std::vector<const IR::Expression*> exprVec;

    for (size_t idx = 0; idx < tokens.size(); idx++) {
        auto token = tokens.at(idx);
        if (token.is_one_of(
                Token::Kind::Minus, Token::Kind::Plus, Token::Kind::Equal, Token::Kind::NotEqual,
                Token::Kind::GreaterThan, Token::Kind::GreaterEqual, Token::Kind::LessThan,
                Token::Kind::LessEqual, Token::Kind::Slash, Token::Kind::Percent, Token::Kind::Shr,
                Token::Kind::Shl, Token::Kind::Mul, Token::Kind::NotEqual)) {
            const IR::Expression* leftL = nullptr;
            const IR::Expression* rightL = nullptr;
            leftL = makeConstant(tokens[idx - 1], keyElements, nullptr);
            if (tokens[idx + 1].is_one_of(Token::Kind::Text, Token::Kind::Number)) {
                rightL = makeConstant(tokens[idx + 1], keyElements, leftL->type);
                if (const auto* constant = leftL->to<IR::Constant>()) {
                    auto* clone = constant->clone();
                    clone->type = rightL->type;
                    leftL = clone;
                }
            } else {
                auto rightPart = findRightPart(tokens, idx);
                rightL = getIR(rightPart.first, keyElements);
                idx = rightPart.second;
            }

            if (idx - 2 > 0 && tokens[idx - 2].is(Token::Kind::LNot)) {
                leftL = new IR::LNot(leftL);
            }
            exprVec.push_back(pickBinaryExpr(token, leftL, rightL));
        } else if (token.is(Token::Kind::LNot)) {
            if (!tokens[idx + 1].is_one_of(Token::Kind::Text, Token::Kind::Number)) {
                auto rightPart = findRightPart(tokens, idx);
                const IR::Expression* exprLNot = getIR(rightPart.first, keyElements);
                idx = rightPart.second;
                exprVec.push_back(new IR::LNot(exprLNot));
            }
        } else if (token.is_one_of(Token::Kind::Disjunction, Token::Kind::Implication)) {
            if (token.is(Token::Kind::Implication)) {
                const auto* tmp = exprVec[exprVec.size() - 1];
                exprVec.pop_back();
                exprVec.push_back(new IR::LNot(tmp));
            }
            const IR::Expression* expr1 = new IR::PathExpression(new IR::Path("tmp"));
            const IR::Expression* expr2 = new IR::PathExpression(new IR::Path("tmp"));
            exprVec.push_back(new IR::LOr(expr1, expr2));
        } else if (token.is(Token::Kind::Conjunction)) {
            const IR::Expression* expr1 = new IR::PathExpression(new IR::Path("tmp"));
            const IR::Expression* expr2 = new IR::PathExpression(new IR::Path("tmp"));
            exprVec.push_back(new IR::LAnd(expr1, expr2));
        }
    }

    if (exprVec.size() == 1) {
        return exprVec[0];
    }

    return makeSingleExpr(exprVec);
}
/// Combines successive tokens into variable names.
/// Returns a vector with tokens combined into names.
/// For example, at the input we have a vector of tokens:
/// [a(Text),c(Text),b(text), +(Plus), 1(Number),2(number)]
/// The result will be [acb(Text), +(Plus), 1(Number),2(number)]
std::vector<Token> combineTokensToNames(const std::vector<Token>& inputVector) {
    std::vector<Token> result;
    Token prevToken = Token(Token::Kind::Unknown, " ", 1);
    cstring txt = "";
    for (const auto& input : inputVector) {
        if (prevToken.is(Token::Kind::Text) && input.is(Token::Kind::Number)) {
            txt += std::string(input.lexeme());
            continue;
        } 
        if (input.is(Token::Kind::Text)) {
            auto strtmp = std::string(input.lexeme());
            if (strtmp == "." && prevToken.is(Token::Kind::Number)) {
                ::error(
                    "Syntax error, unexpected INTEGER. P4 does not support floating point values. "
                    "Exiting");
            }
            txt += strtmp;
            if (prevToken.is(Token::Kind::Number)) {
                txt = std::string(prevToken.lexeme()) + txt;
                result.pop_back();
            }
        } else {
            if (txt.size() > 0) {
                result.emplace_back(Token::Kind::Text, txt, txt.size());
                txt = "";
            }
            result.push_back(input);
        }

        prevToken = input;
    }
    return result;
}

/// Combines successive tokens into numbers. For converting boolean or Hex, Octal values to
/// numbers we must first use combineTokensToNames. Returns a vector with tokens combined into
/// numbers. For example, at the input we have a vector of tokens: [a(Text),c(Text),b(text),
/// +(Plus), 1(Number),2(number)] The result will be [a(Text),c(Text),b(text), +(Plus), 12(number)]
/// Other examples :
/// [acb(text), ||(Disjunction), true(text)] -> [acb(text), ||(Disjunction), 1(number)] - This
/// example is possible only after executing combineTokensToNames, since to convert bool values,
/// they must first be collected from text tokens in the combineTokensToNames function
/// [2(Number), 5(number), 5(number), ==(Equal), 0xff(Text)] -> [255(number), ==(Equal),
/// 0xff(Number)] - This example is possible only after executing combineTokensToNames
std::vector<Token> combineTokensToNumbers(std::vector<Token> input) {
    cstring numb = "";
    std::vector<Token> result;
    for (uint64_t i = 0; i < input.size(); i++) {
        if (input[i].is(Token::Kind::Text)) {
            auto str = std::string(input[i].lexeme());

            if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) {
                cstring cstr = str;
                result.emplace_back(Token::Kind::Number, cstr, cstr.size());
                continue;
            }

            if (str == "true") {
                result.emplace_back(Token::Kind::Number, "1", 1);
                continue;
            }

            if (str == "false") {
                result.emplace_back(Token::Kind::Number, "0", 1);
                continue;
            }
        }
        if (input[i].is(Token::Kind::Number)) {
            numb += std::string(input[i].lexeme());
            if (i + 1 == input.size()) {
                result.emplace_back(Token::Kind::Number, numb, numb.size());
                numb = "";
                continue;
            }
        } else {
            if (numb.size() > 0) {
                result.emplace_back(Token::Kind::Number, numb, numb.size());
                numb = "";
            }
            result.push_back(input[i]);
        }
    }
    return result;
}
/// Convert access tokens or keys into table keys. For converting access tokens or keys to
/// table keys we must first use combineTokensToNames. Returns a vector with tokens converted into
/// table keys.
/// For example, at the input we have a vector of tokens:
/// [key::mask(Text), ==(Equal) , 0(Number)] The result will be [tableName_mask_key(Text), ==(Equal)
/// , 0(Number)] Other examples : [key::prefix_length(Text), ==(Equal) , 0(Number)] ->
/// [tableName_lpm_prefix_key(Text), ==(Equal) , 0(Number)] [key::priority(Text), ==(Equal) ,
/// 0(Number)] -> [tableName_priority_key(Text), ==(Equal) , 0(Number)] [Name01(Text), ==(Equal) ,
/// 0(Number)] -> [tableName_key_Name01(Text), ==(Equal) , 0(Number)]
std::vector<Token> combineTokensToTableKeys(std::vector<Token> input, cstring tableName) {
    std::vector<Token> result;
    for (uint64_t idx = 0; idx < input.size(); idx++) {
        if (!input[idx].is(Token::Kind::Text)) {
            result.push_back(input[idx]);
            continue;
        }
        auto str = std::string(input[idx].lexeme());

        auto substr = str.substr(0, str.find("::mask"));
        if (substr != str) {
            cstring cstr = tableName + "_mask_" + substr;
            result.emplace_back(Token::Kind::Text, cstr, cstr.size());
            continue;
        }
        substr = str.substr(0, str.find("::prefix_length"));
        if (substr != str) {
            cstring cstr = tableName + "_lpm_prefix_" + substr;
            result.emplace_back(Token::Kind::Text, cstr, cstr.size());
            continue;
        }

        substr = str.substr(0, str.find("::priority"));
        if (substr != str) {
            cstring cstr = tableName + "_priority";
            result.emplace_back(Token::Kind::Priority, cstr, cstr.size());
            continue;
        }

        if (str.find("isValid") != std::string::npos) {
            cstring cstr = tableName + "_key_" + str;
            result.emplace_back(Token::Kind::Text, cstr, cstr.size());
            idx += 2;
            continue;
        }

        cstring cstr = tableName + "_key_" + str;
        result.emplace_back(Token::Kind::Text, cstr, cstr.size());
    }
    return result;
}

/// Removes comment lines from the input array.
/// For example, at the input we have a vector of tokens:
/// [//(Comment), CommentText(Text) , (EndString) , 28(Number), .....] -> [28(Number), .....]
std::vector<Token> removeComments(const std::vector<Token>& input) {
    std::vector<Token> result;
    bool flag = true;
    for (const auto& i : input) {
        if (i.is(Token::Kind::Comment)) {
            flag = false;
            continue;
        }
        if (i.is(Token::Kind::EndString)) {
            flag = true;
            continue;
        }
        if (flag) {
            result.push_back(i);
        }
    }
    return result;
}

/// A function that calls the beginning of the transformation of restrictions from a string into an
/// IR::Expression. Internally calls all other necessary functions, for example combineTokensToNames
/// and the like, to eventually get an IR expression that meets the string constraint
std::vector<const IR::Expression*> AssertsParser::genIRStructs(
    cstring tableName, cstring restrictionString, const IR::Vector<IR::KeyElement>& keyElements) {
    Lexer lex(restrictionString);
    std::vector<Token> tmp;
    for (auto token = lex.next(); !token.is_one_of(Token::Kind::End, Token::Kind::Unknown);
         token = lex.next()) {
        tmp.push_back(token);
    }

    std::vector<const IR::Expression*> result;

    tmp = combineTokensToNames(tmp);
    
    tmp = combineTokensToNumbers(tmp);
    if (tableName.size() == 0) {
        for (uint64_t i = 0; i < tmp.size(); i++) {
            std::cout << tmp[i].lexeme() << std::endl;
        }
        const auto* expr = getIR(tmp, keyElements);
        std::cout << expr << std::endl;
        result.push_back(expr);
        return result;
    } else {
        tmp = combineTokensToTableKeys(tmp, tableName);
    }
    tmp = removeComments(tmp);
    std::vector<Token> tokens;
    for (uint64_t i = 0; i < tmp.size(); i++) {
        if (tmp[i].is(Token::Kind::Semicolon)) {
            const auto* expr = getIR(tokens, keyElements);
            result.push_back(expr);
            tokens.clear();
        } else if (i == tmp.size() - 1) {
            tokens.push_back(tmp[i]);
            const auto* expr = getIR(tokens, keyElements);
            result.push_back(expr);
            tokens.clear();
        } else {
            tokens.push_back(tmp[i]);
        }
    }

    return result;
}

const IR::Type* Parser::getDefinedType(cstring txt, const IR::Type* prevType) {
    if (prevType == nullptr) {
        // Find struct inside a program.
        auto* decl = program->getDeclsByName(txt)->single();
        return decl->to<IR::Type>();
    } else if (const auto* structType = prevType->to<IR::Type_StructLike>()) {
        // Find field in a struct.
        const auto* field = structType->getField(txt);
        BUG_CHECK(field != nullptr, "Can't find field %1% in struct %2%", txt, prevType);
        return field->type;
    }
    BUG("Can't find't type %1%", prevType);
}

const IR::Expression* Parser::createBinaryOp(Token::Kind kind, const IR::Expression* left,
                                             const IR::Expression* right) {
    switch (kind) {
        case Token::Kind::LNot:
            return IR::LNot(left, right);
        case Token::Kind::Complement:
            return IR::Cmpl(left, right);
        case Token::Kind::Mul:
            return IR::Mul(left, right);
        case Token::Kind::Percent:
            return IR::Mod(left, right);
        case Token::Kind::Slash:
            return IR::Div(left, right);
        case Token::Kind::Minus:
            return IR::Sub(left, right);
        case Token::Kind::SaturationSub:
            return IR::SubSat(left, right);
        case Token::Kind::Plus:
            return IR::Add(left, right);
        case Token::Kind::SaturationAdd:
            return IR::AddSat(left, right);
        case Token::Kind::LessEqual:
            return IR::Leq(left, right);
        case Token::Kind::Shl:
            return IR::Shl(left, right);
        case Token::Kind::LessThan:
            return IR::Lss(left, right);
        case Token::Kind::GreaterEqual:
            return IR::Geq(left, right);
        case Token::Kind::Shr:
            return IR::Shr(left, right);
        case Token::Kind::GreaterThan:
            return IR::Grt(left, right);
        case Token::Kind::NotEqual:
            return IR::Neq(left, right);
        case Token::Kind::Equal:
            return IR::Equ(left, right);
        case Token::Kind::BAnd:
            return IR::BAnd(left, right);
        case Token::Kind::Xor:
            return IR::BXor(left, right);
        case Token::Kind::BOr:
            return IR::BOr(left, right);
        case Token::Kind::Conjunction:
            return IR::LOr(left, right);
        case Token::Kind::Disjunction:
            return IR::LAnd(left, right);
        case Token::Kind::Implication:
            return IR::LOr(new IR::LNot(left), right);
    }
    BUG("Unimplemented kind %1%", kind);
}

const IR::Type* Parser::getDefinedType(cstring txt, const IR::Type* prevType) {
    if (prevType == nullptr) {
        // Find struct inside a program.
        const auto* decl = program->getDeclsByName(txt)->single();
        BUG_CHECK(decl != nullptr, "Can't find type for %1%", txt);
        if (const auto* declVar = decl->to<IR::Declaration_Variable>()) {
            return declVar->type;
        }
        if (const auto* declConst = decl->to<IR::Declaration_Constant>()) {
            return declConst->type;
        }
        if (const auto* declInst = decl->to<IR::Declaration_Instance>()) {
            return declInst->type;
        }
        BUG("Can't find declaration for %1%", txt);
    } else if (const auto* structType = prevType->to<IR::Type_StructLike>()) {
        // Find field in a struct.
        const auto* field = structType->getField(txt);
        BUG_CHECK(field != nullptr, "Can't find field %1% in struct %2%", txt, prevType);
        return field->type;
    }
    BUG("Can't find't type %1%", prevType);
}

const IR::Expression* Parser::createConstantOp() {
    LOG1("createConstantOp : " << tokens[index].lexeme());
    if (tokens[index].is(Token::Kind::Text)) {
        cstring txt;
        do {
            txt += tokens[index].lexeme().data();
            index++;
        } while (tokens[index].is(Token::Kind::Text));
        if (txt == "true") {
            return new IR::BoolLiteral(true);
        } else if (txt == "false") {
            return new IR::BoolLiteral(false);
        } else {
            // Used for representation of a member part.
            const auto* type = getDefinedType(txt, nullptr);
            if (type == nulltr) {
                return new IR::NamedExpression("FieldName", new IR::StringLiteral(txt));
            }
            return new IR::PathExpression(type, new IR::Path(txt));
        }
    }
    if (tokens[index].is(Token::Kind::Number)) {
        std::string txt;
        bool isHex = false;
        do {
            txt += std::data(tokens[index].lexeme());
            index++;
            if (tokens[index].is(Token::Kind::Text)) {
                if (tokens[index].lexeme() == "x" && index + 1 < tokens.size() &&
                    tokens[index + 1].is(Token::Kind::Number)) {
                    txt.erase(0, 1);
                    isHex = true;
                    index++;
                    continue;
                }
                break;
            }
        } while (tokens[index].is(Token::Kind::Number));
        const IR::Expression* expression = nullptr;
        if (isHex) {
            std::istringstream converter { txt };
            uint64_t value = 0;
            converter >> std::hex >> value;
            expression = new IR::Constant(value);
        } else {
            expression = new IR::Constant(big_int(txt));
        }
        return expression;
    }
    BUG("Unimplemented literal %1%", tokens[index].lexeme());
}

const IR::Expression* Parser::createSliceOrArrayOp(const IR::Expression* base) {
    LOG1("createSliceOp : " << tokens[index].lexeme());
    BUG_CHECK(tokens[index].is(Token::Kind::LeftSParent), "Expected '['");
    const auto slice = createFunctionCallOrConstantOp();
    BUG_CHECK(tokens[index].is(Token::Kind::RightSParent), "Expected ']'");
    index++;
    const auto* namedExpr = slice->to<IR::NamedExpression>();
    BUG_CHECK(namedExpr != nullptr, "Unexpected expression %1%", namedExpr);
    BUG_CHECK(namedExpr->name == "Colon", "Expected colon expression %1%", namedExpr);
    const auto* listExpr = namedExpr->expression->to<IR::ListExpression>();
    BUG_CHECK(listExpr->size() < 2 && listExpr->size() != 0, "Expected one or two arguments", listExpr);
    if (listExpr->size() == 1) {
        // Create an array.
        return new IR::ArrayIndex(base, listExpr->at(0));
    }
    // Create a slice.
    return new IR::Slice(base, listExpr->at(0), listExpr->at(1));
}

const IR::Expression* Parser::createFunctionCallOrConstantOp() {
    LOG1("createFunctionCallOrConstantOp : " << tokens[index].lexeme());
    if (tokens[index].is(Token::Kind::Minus)) {
        index++;
        return new IR::Mul(new IR::Constant(-1), createFunctionCallOrConstantIR());
    }
    if (tokens[index].is(Token::Kind::Plus)) {
        index++;
        return createFunctionCallOrConstantIR();
    }
    if (tokens[index].is(Token::Kind::StringLiteral)) {
        index++;
        return IR::StringLiteral(tokens[index].lexeme());
    }
    if (tokens[index].is(Token::Kind::LeftSParent)) {
        index++;
        const auto* res = createPunctuationMarks();
        BUG_CHECK(tokens[index].is(Token::Kind::RightSParen), "Can't find coresponded ']'");
        index++;
        return res;
    }
    const auto* mainArgument = createConstantOp();
    if (tokens[index].is(Token::Kind::Dot) || tokens[index].is(Token::Kind::FieldAccess)) {
        const IR::Type* prevType = nullptr;
        do {
            index++;
            auto result = createConstantOp();
            prevType = getDefinedType(result->to<IR::StringLiteral>()->value, mainArgument->type);
            mainArgument =
                new IR::Member(prevType, mainArgument, result->to<IR::NamedExpression>()->expression->to<IR::StringLiteral>()->value);
        } while (tokens[index].is(Token::Kind::Dot) || tokens[index].is(Token::Kind::FieldAccess));
        if (index >= tokens.size()) {
            return mainArgument;
        }
        if (tokens[index].is(Token::Kind::LeftSParent)) {
            return createSliceOrArrayOp(mainArgument);
        }
        return mainArgument;
    }
    if (tokens[index].is(Token::Kind::LeftSParent) && mainArgument->is<IR::PathExpression>()) {
        return createSliceOrArrayOp(mainArgument);
    }
    return mainArgument;
}

const IR::Expression* Parser::createArithmeticIR() {
    LOG1("createEqCompareAndShiftOp : " << tokens[index].lexeme());
    if (curToken.is(Token::Kind::LNot)) {
        index++;
        return createIR(curToken.kind(), createArithmeticIR(), nullptr);
    }
    if (curToken.is(Token::Kind::Complement)) {
        index++;
        return createIR(curToken.kind(), createArithmeticIR(), nullptr);
    }
    const auto* mainArgument = createFunctionCallOrConstantIR();
    if (index >= tokens.size()) {
        return mainArgument;
    }
    auto curToken = tokens[index];
    index++;
    if (curToken.is(Token::Kind::Mul)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::Percent)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::Slash)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::Minus)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::SaturationSub)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::Plus)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    if (curToken.is(Token::Kind::SaturationAdd)) {
        return createIR(curToken.kind(), mainArgument, createArithmeticIR());
    }
    index--;
    return mainArgument;
}

const IR::Expression* Parser::createEqCompareAndShiftOp() {
    LOG1("createEqCompareAndShiftOp : " << tokens[index].lexeme());
    const auto* mainArgument = createArithmeticIR();
    if (index >= tokens.size()) {
        return mainArgument;
    }
    auto curToken = tokens[index];
    index++;
    if (curToken.is(Token::Kind::LessEqual)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::Shl)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::LessThan)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::GreaterEqual)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::Shr)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::GreaterThan)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::NotEqual)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    if (curToken.is(Token::Kind::Equal)) {
        return createIR(curToken.kind(), mainArgument, createEqCompareAndShiftOp());
    }
    index--;
    return mainArgument;
}

const IR::Expression* Parser::createBinaryOp() {
    LOG1("createBinaryOp : " << tokens[index].lexeme());
    const auto* mainArgument = createEqCompareAndShiftOp();
    if (index >= tokens.size()) {
        return mainArgument;
    }
    auto curToken = tokens[index];
    index++;
    if (curToken.is(Token::Kind::BAnd)) {
        return createIR(curToken.kind(), mainArgument, createBinaryOp());
    }
    if (curToken.is(Token::Kind::Xor)) {
        return createIR(curToken.kind(), mainArgument, createBinaryOp());
    }
    if (curToken.is(Token::Kind::Bor)) {
        return createIR(curToken.kind(), mainArgument, createBinaryOp());
    }
    index--;
    return mainArgument;
}

const IR::Expression* Parser::createLogicalOp() {
    LOG1("createLogicalOp : " << tokens[index].lexeme());
    BUG_CHECK(index < tokens.size(), "Invalid index of a token in createLogicalOp");
    const auto* mainArgument = createBinaryOp();
    if (index >= tokens.size()) {
        return mainArgument;
    }
    auto curToken = tokens[index];
    index++;
    if (curToken.is(Token::Kind::Conjunction)) {
        return createIR(curToken.kind(), mainArgument, createLogicalOp());
    }
    if (curToken.is(Token::Kind::Disjunction)) {
        return createIR(curToken.kind(), mainArgument, createLogicalOp());
    }
    if (curToken.is(Token::Kind::Implication)) {
        return createIR(curToken.kind(), mainArgument, createLogicalOp());
    }
    index--;
    return mainArgument;
}

const IR::Expression* Parser::createListExpressions(const IR::Expression* first, const char* name, Token::Kind kind) {
    std::vector<const IR::Expression*> components;
    components.push_back(first);
    do {
        index++;
        result.push_back(createPunctuationMarks());
    } while (tokens[index].kind() == kind);
    return new IR::NamedExpression(name, new IR::ListExpression(components));
}

const IR::Expression* Parser::createPunctuationMarks() {
    LOG1("createPunctuationMarks : " << tokens[index].lexeme());
    const auto* mainArgument = createLogicalOp();
    if (index >= tokens.size()) {
        return mainArgument;
    }
    if (tokens[index].is(Token::Kind::Colon)) {
        return createListExpressions(mainArgument, NAMES[tokens[index].kind()], tokens[index].kind());
    }
    if (tokens[index].is(Token::Kind::Question)) {
        index++;
        const auto* right = createPunctuationMarks();
        const auto* namedExpr = right->to<IR::NamedExpression>();
        BUG_CHECK(namesExpr->name == "Colon", "Undefined named expression %1%", right);
        const auto* listExpr = namedExpr->expression->to<IR::ListExpression>();
        BUG_CHECK(listExpr->size() == 2,
                  "Invalid size for arguments of ? operator %1%", right);
        return new IR::Mux(mainArgument, listExpr->components[0], listExpr->components[0]);
    }
    if (tokens[index].is(Token::Kind::Semicolon)) {
        return createListExpressions(mainArgument, NAMES[tokens[index].kind()], tokens[index].kind());
    }
    if (tokens[index].is(Token::Kind::Comma)) {
        return createListExpressions(mainArgument, NAMES[tokens[index].kind()], tokens[index].kind());
    }
    return mainArgument;
}

const IR::Expression* Parser::getIR() {
    index = 0;
    LOG1("getIR : " << tokens[index].lexeme());
    BUG_CHECK(index < tokens.size(), "Invalid size of the tokens vector");
    const auto* result = createPunctuationMarks;
    BUG_CHECK(index < tokens.size(), "Can't translate string into IR");
    return result;
}

const IR::Expression* Parser::getIR(const char* str, const IR::P4Program* program,
                                    TokensSet skippedTokens) {
    Lexer lex(str);
    std::vector<Token> tmp;
    for (auto token = lex.next(); !token.is_one_of(Token::Kind::End, Token::Kind::Unknown);
         token = lex.next()) {
        if (skippedTokens.count(token.kind()) == 0) {
            tmp.push_back(token);
        }
    }
    Parser parser(program, tmp);
    return parser.getIR();
}

Parser::Parser(const IR::P4Program* program, std::vector<Token> &tokens)
    : program(program), tokens(tokens) {}

const IR::Node* AssertsParser::postorder(IR::P4Table* node) {
    const auto* annotation = node->getAnnotation("entry_restriction");
    const auto* key = node->getKey();
    if (annotation == nullptr || key == nullptr) {
        return node;
    }

    for (const auto* restrStr : annotation->body) {
        auto restrictions =
            genIRStructs(node->controlPlaneName(), restrStr->text, key->keyElements);
        /// Using Z3Solver, we check the feasibility of restrictions, if they are not
        /// feasible, we delete keys and entries from the table to execute
        /// default_action
        Z3Solver solver;
        if (solver.checkSat(restrictions) == true) {
            restrictionsVec.push_back(restrictions);
            continue;
        }
        auto* cloneTable = node->clone();
        auto* cloneProperties = node->properties->clone();
        IR::IndexedVector<IR::Property> properties;
        for (const auto* property : cloneProperties->properties) {
            if (property->name.name != "key" || property->name.name != "entries") {
                properties.push_back(property);
            }
        }
        cloneProperties->properties = properties;
        cloneTable->properties = cloneProperties;
        return cloneTable;
    }
    return node;
}

Token Lexer::atom(Token::Kind kind) noexcept { return {kind, m_beg++, 1}; }

Token Lexer::next() noexcept {
    while (isSpace(peek())) {
        get();
    }
    std::string txt;
    switch (peek()) {
        case '\0':
            return {Token::Kind::End, m_beg, 1};
        case '\n':
            get();
            return {Token::Kind::EndString, m_beg, 1};
        default:
            return atom(Token::Kind::Text);
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return atom(Token::Kind::Number);
        case '\"':
            txt = "";
            do (
                txt += peek();
            ) while (get() != '\"');
            return {Token::Kind::StringLiteral, txt.c_str(), 2};
        case '(':
            return atom(Token::Kind::LeftParen);
        case ')':
            return atom(Token::Kind::RightParen);
        case '=':
            get();
            if (get() == '=') {
                get();
                return {Token::Kind::Equal, "==", 2};
            }
            prev();
            return atom(Token::Kind::Unknown);
        case '!':
            get();
            if (get() == '=') {
                get();
                return {Token::Kind::NotEqual, "!=", 2};
            }
            prev();
            prev();
            return atom(Token::Kind::LNot);
        case '-':
            get();
            if (get() == '>') {
                get();
                return {Token::Kind::Implication, "->", 2};
            }
            prev();
            return atom(Token::Kind::Minus);
        case '<':
            get();
            if (get() == '=') {
                get();
                return {Token::Kind::LessEqual, "<=", 2};
            }
            prev();
            if (get() == '<') {
                return {Token::Kind::Shl, "<<", 2};
            }
            prev();
            return atom(Token::Kind::LessThan);
        case '>':
            get();
            if (get() == '=') {
                get();
                return {Token::Kind::GreaterEqual, ">=", 2};
            }
            prev();
            if (get() == '>') {
                return {Token::Kind::Shr, ">>", 2};
            }
            prev();
            return atom(Token::Kind::GreaterThan);
        case ';':
            return atom(Token::Kind::Semicolon);
        case ',':
            return atom(Token::Kind::Comma);
        case '.':
            return atom(Token::Kind::Dot);
        case ':':
            get();
            if (get() == ':') {
                get();
                return atom(Token::Kind::FieldAccess);
            }
            return atom(Token::Kind::Colon);
        case '&':
            get();
            if (get() == '&') {
                get();
                return {Token::Kind::Conjunction, "&&", 2};
            }
            prev();
            return atom(Token::Kind::Text);
        case '|':
            get();
            if (get() == '|') {
                get();
                return {Token::Kind::Disjunction, "||", 2};
            }
            prev();
            bool isAdd = false;
            if ((isAdd = (peek() == '+')) || peek() == '-') {
                get();
                if (peek() == '|') {
                    get();
                    if (isAdd) {
                        return atom(Token::Kind::SaturationAdd);
                    } else {
                        return atom(Token::Kind::SaturationSub);
                    }
                }
                prev();
            }
            return atom(Token::Kind::Text);
        case '+':
            return atom(Token::Kind::Plus);
        case '/':
            get();
            if (get() == '/') {
                get();
                return {Token::Kind::Comment, "//", 2};
            }
            prev();
            return atom(Token::Kind::Slash);
        case '%':
            return atom(Token::Kind::Percent);
        case '*':
            return atom(Token::Kind::Mul);
    }
}
}  // namespace AssertsParser

}  // namespace P4Tools
