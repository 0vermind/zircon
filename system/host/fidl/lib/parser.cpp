// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

namespace fidl {

#define CASE_TOKEN(K) \
    case Token::KindAndSubkind(K, Token::Subkind::kNone).combined()

#define CASE_IDENTIFIER(K) \
    case Token::KindAndSubkind(Token::Kind::kIdentifier, K).combined()

#define TOKEN_PRIMITIVE_TYPE_CASES \
    CASE_IDENTIFIER(Token::Subkind::kBool):        \
    CASE_IDENTIFIER(Token::Subkind::kInt8):        \
    CASE_IDENTIFIER(Token::Subkind::kInt16):       \
    CASE_IDENTIFIER(Token::Subkind::kInt32):       \
    CASE_IDENTIFIER(Token::Subkind::kInt64):       \
    CASE_IDENTIFIER(Token::Subkind::kUint8):       \
    CASE_IDENTIFIER(Token::Subkind::kUint16):      \
    CASE_IDENTIFIER(Token::Subkind::kUint32):      \
    CASE_IDENTIFIER(Token::Subkind::kUint64):      \
    CASE_IDENTIFIER(Token::Subkind::kFloat32):     \
    CASE_IDENTIFIER(Token::Subkind::kFloat64)

#define TOKEN_TYPE_CASES                        \
    TOKEN_PRIMITIVE_TYPE_CASES:                 \
    CASE_IDENTIFIER(Token::Subkind::kNone):     \
    CASE_IDENTIFIER(Token::Subkind::kArray):    \
    CASE_IDENTIFIER(Token::Subkind::kVector):   \
    CASE_IDENTIFIER(Token::Subkind::kString):   \
    CASE_IDENTIFIER(Token::Subkind::kHandle):   \
    CASE_IDENTIFIER(Token::Subkind::kRequest)

#define TOKEN_ATTR_CASES           \
    case Token::Kind::kDocComment: \
    case Token::Kind::kLeftSquare

#define TOKEN_LITERAL_CASES                   \
    CASE_IDENTIFIER(Token::Subkind::kTrue):   \
    CASE_IDENTIFIER(Token::Subkind::kFalse):  \
    CASE_TOKEN(Token::Kind::kNumericLiteral): \
    CASE_TOKEN(Token::Kind::kStringLiteral)

namespace {
enum {
    More,
    Done,
};
} // namespace

Parser::Parser(Lexer* lexer, ErrorReporter* error_reporter)
    : lexer_(lexer), error_reporter_(error_reporter), latest_discarded_end_() {
    handle_subtype_table_ = {
        {"process", types::HandleSubtype::kProcess},
        {"thread", types::HandleSubtype::kThread},
        {"vmo", types::HandleSubtype::kVmo},
        {"channel", types::HandleSubtype::kChannel},
        {"event", types::HandleSubtype::kEvent},
        {"port", types::HandleSubtype::kPort},
        {"interrupt", types::HandleSubtype::kInterrupt},
        {"log", types::HandleSubtype::kLog},
        {"socket", types::HandleSubtype::kSocket},
        {"resource", types::HandleSubtype::kResource},
        {"eventpair", types::HandleSubtype::kEventpair},
        {"job", types::HandleSubtype::kJob},
        {"vmar", types::HandleSubtype::kVmar},
        {"fifo", types::HandleSubtype::kFifo},
        {"guest", types::HandleSubtype::kGuest},
        {"timer", types::HandleSubtype::kTimer},
    };

    last_token_ = Lex();
}

bool Parser::LookupHandleSubtype(const raw::Identifier* identifier,
                                 types::HandleSubtype* subtype_out) {
    auto lookup = handle_subtype_table_.find(identifier->location().data());
    if (lookup == handle_subtype_table_.end()) {
        return false;
    }
    *subtype_out = lookup->second;
    return true;
}

decltype(nullptr) Parser::Fail() {
    return Fail("found unexpected token");
}

decltype(nullptr) Parser::Fail(StringView message) {
    if (ok_) {
        error_reporter_->ReportError(last_token_, std::move(message));
        ok_ = false;
    }
    return nullptr;
}

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier(bool is_discarded) {
    Token identifier = ConsumeToken(OfKind(Token::Kind::kIdentifier), is_discarded);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Identifier>(identifier, identifier);
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<raw::Identifier>> components;

    components.emplace_back(ParseIdentifier());
    if (!Ok())
        return Fail();
    Token first_token = components[0]->start_;

    auto parse_component = [&components, this]() {
        switch (Peek().combined()) {
        default:
            return Done;

        CASE_TOKEN(Token::Kind::kDot):
            ConsumeToken(OfKind(Token::Kind::kDot), true);
            if (Ok()) {
                components.emplace_back(ParseIdentifier());
            }
            return More;
        }
    };

    while (parse_component() == More) {
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::CompoundIdentifier>(first_token, MarkLastUseful(), std::move(components));
}

std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
    Token string_literal = ConsumeToken(OfKind(Token::Kind::kStringLiteral));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(string_literal);
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::NumericLiteral>(numeric_literal);
}

std::unique_ptr<raw::Ordinal> Parser::ParseOrdinal() {
    auto numeric_literal = ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
    if (!Ok())
        return Fail();
    auto colon = ConsumeToken(OfKind(Token::Kind::kColon));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Ordinal>(numeric_literal, colon);
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
    Token token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTrue));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TrueLiteral>(token);
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
    Token token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kFalse));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::FalseLiteral>(token);
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
    switch (Peek().combined()) {
    CASE_TOKEN(Token::Kind::kStringLiteral):
        return ParseStringLiteral();

    CASE_TOKEN(Token::Kind::kNumericLiteral):
        return ParseNumericLiteral();

    CASE_IDENTIFIER(Token::Subkind::kTrue):
        return ParseTrueLiteral();

    CASE_IDENTIFIER(Token::Subkind::kFalse):
        return ParseFalseLiteral();

    default:
        return Fail();
    }
}

std::unique_ptr<raw::Attribute> Parser::ParseAttribute() {
    auto name = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::StringLiteral> value;
    if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        value = ParseStringLiteral();
        if (!Ok())
            return Fail();
    }

    std::string str_name("");
    std::string str_value("");
    if (name)
        str_name = std::string(name->location().data().data(), name->location().data().size());
    if (value) {
        auto data = value->location().data();
        if (data.size() >= 2 && data[0] == '"' && data[data.size() - 1] == '"') {
            str_value = std::string(value->location().data().data() + 1, value->location().data().size() - 2);
        }
    }
    return std::make_unique<raw::Attribute>(name->start_, MarkLastUseful(), str_name, str_value);
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList(std::unique_ptr<raw::Attribute>&& doc_comment) {
    Token start;
    auto attributes = std::make_unique<raw::Attributes>();
    if (doc_comment) {
        start = doc_comment->start_;
        attributes->Insert(std::move(doc_comment));
        ConsumeToken(OfKind(Token::Kind::kLeftSquare), true);
    } else {
        start = ConsumeToken(OfKind(Token::Kind::kLeftSquare));
    }
    if (!Ok())
        return Fail();
    for (;;) {
        auto attribute = ParseAttribute();
        if (!Ok())
            return Fail();
        auto attribute_name = attribute->name;
        if (!attributes->Insert(std::move(attribute))) {
            std::string message("Duplicate attribute with name '");
            message += attribute_name;
            message += "'";
            return Fail(message);
        }
        if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
            break;
    }
    ConsumeToken(OfKind(Token::Kind::kRightSquare), true);
    if (!Ok())
        return Fail();
    auto attribute_list = std::make_unique<raw::AttributeList>(start, MarkLastUseful(), std::move(attributes));
    return attribute_list;
}

std::unique_ptr<raw::Attribute> Parser::ParseDocComment() {
    std::string str_value("");
    Token start;
    Token end;

    Token doc_line;
    while (Peek().kind() == Token::Kind::kDocComment) {
        // Most of the tokens are discarded, except the first and last, which we
        // retroactively mark useful.
        doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment), true);
        if (start.kind() == Token::Kind::kNotAToken) {
            start = MarkLastUseful();
        }
        str_value += std::string(doc_line.location().data().data() + 3, doc_line.location().data().size() - 2);
        assert(Ok());
    }
    end = MarkLastUseful();
    return std::make_unique<raw::Attribute>(start, end, "Doc", str_value);
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList() {
    std::unique_ptr<raw::Attribute> doc_comment;
    // Doc comments must appear above attributes
    if (Peek().kind() == Token::Kind::kDocComment) {
        doc_comment = ParseDocComment();
    }
    if (Peek().kind() == Token::Kind::kLeftSquare) {
        return ParseAttributeList(std::move(doc_comment));
    }
    // no generic attributes, start the attribute list
    if (doc_comment) {
        auto attributes = std::make_unique<raw::Attributes>();
        Token start = doc_comment->start_;
        Token end = doc_comment->end_;
        attributes->Insert(std::move(doc_comment));
        return std::make_unique<raw::AttributeList>(start, end, std::move(attributes));
    }
    return nullptr;
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
    switch (Peek().combined()) {
    CASE_TOKEN(Token::Kind::kIdentifier): {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::IdentifierConstant>(std::move(identifier));
    }

    TOKEN_LITERAL_CASES: {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::LiteralConstant>(std::move(literal));
    }

    default:
        return Fail();
    }
}

std::unique_ptr<raw::Using> Parser::ParseUsing() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUsing));
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> maybe_alias;
    std::unique_ptr<raw::PrimitiveType> maybe_primitive;

    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kAs))) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    } else if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        if (!Ok() || using_path->components.size() != 1u)
            return Fail();
        maybe_primitive = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::Using>(start, MarkLastUseful(), std::move(using_path), std::move(maybe_alias), std::move(maybe_primitive));
}

std::unique_ptr<raw::ArrayType> Parser::ParseArrayType() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kArray));
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftAngle), true);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kRightAngle), true);
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kColon), true);
    if (!Ok())
        return Fail();
    auto element_count = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ArrayType>(start, MarkLastUseful(), std::move(element_type), std::move(element_count));
}

std::unique_ptr<raw::VectorType> Parser::ParseVectorType() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kVector));
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftAngle), true);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kRightAngle), true);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::VectorType>(start, MarkLastUseful(), std::move(element_type),
                                             std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::StringType> Parser::ParseStringType() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kString));
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::StringType>(start, MarkLastUseful(), std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::HandleType> Parser::ParseHandleType() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kHandle));
    if (!Ok())
        return Fail();

    auto subtype = types::HandleSubtype::kHandle;
    if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
        if (!Ok())
            return Fail();
        auto identifier = ParseIdentifier(true);
        if (!Ok())
            return Fail();
        if (!LookupHandleSubtype(identifier.get(), &subtype))
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kRightAngle), true);
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::HandleType>(start, MarkLastUseful(), subtype, nullability);
}

std::unique_ptr<raw::PrimitiveType> Parser::ParsePrimitiveType() {
    types::PrimitiveSubtype subtype;

    switch (Peek().combined()) {
    CASE_IDENTIFIER(Token::Subkind::kBool):
        subtype = types::PrimitiveSubtype::kBool;
        break;
    CASE_IDENTIFIER(Token::Subkind::kInt8):
        subtype = types::PrimitiveSubtype::kInt8;
        break;
    CASE_IDENTIFIER(Token::Subkind::kInt16):
        subtype = types::PrimitiveSubtype::kInt16;
        break;
    CASE_IDENTIFIER(Token::Subkind::kInt32):
        subtype = types::PrimitiveSubtype::kInt32;
        break;
    CASE_IDENTIFIER(Token::Subkind::kInt64):
        subtype = types::PrimitiveSubtype::kInt64;
        break;
    CASE_IDENTIFIER(Token::Subkind::kUint8):
        subtype = types::PrimitiveSubtype::kUint8;
        break;
    CASE_IDENTIFIER(Token::Subkind::kUint16):
        subtype = types::PrimitiveSubtype::kUint16;
        break;
    CASE_IDENTIFIER(Token::Subkind::kUint32):
        subtype = types::PrimitiveSubtype::kUint32;
        break;
    CASE_IDENTIFIER(Token::Subkind::kUint64):
        subtype = types::PrimitiveSubtype::kUint64;
        break;
    CASE_IDENTIFIER(Token::Subkind::kFloat32):
        subtype = types::PrimitiveSubtype::kFloat32;
        break;
    CASE_IDENTIFIER(Token::Subkind::kFloat64):
        subtype = types::PrimitiveSubtype::kFloat64;
        break;
    default:
        return Fail();
    }

    Token start = ConsumeToken(OfKind(Peek().kind()));
    if (!Ok())
        return Fail();
    return std::make_unique<raw::PrimitiveType>(start, MarkLastUseful(), subtype);
}

std::unique_ptr<raw::RequestHandleType> Parser::ParseRequestHandleType() {
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kRequest));
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftAngle), true);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kRightAngle), true);
    if (!Ok())
        return Fail();

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::RequestHandleType>(start, MarkLastUseful(), std::move(identifier), nullability);
}

std::unique_ptr<raw::Type> Parser::ParseType() {
    switch (Peek().combined()) {
    CASE_TOKEN(Token::Kind::kIdentifier): {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        auto nullability = types::Nullability::kNonnullable;
        if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
            if (!Ok())
                return Fail();
            nullability = types::Nullability::kNullable;
        }
        return std::make_unique<raw::IdentifierType>(identifier->start_, MarkLastUseful(), std::move(identifier), nullability);
    }

    CASE_IDENTIFIER(Token::Subkind::kArray): {
        auto type = ParseArrayType();
        if (!Ok())
            return Fail();
        return type;
    }

    CASE_IDENTIFIER(Token::Subkind::kVector): {
        auto type = ParseVectorType();
        if (!Ok())
            return Fail();
        return type;
    }

    CASE_IDENTIFIER(Token::Subkind::kString): {
        auto type = ParseStringType();
        if (!Ok())
            return Fail();
        return type;
    }

    CASE_IDENTIFIER(Token::Subkind::kHandle): {
        auto type = ParseHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    CASE_IDENTIFIER(Token::Subkind::kRequest): {
        auto type = ParseRequestHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    TOKEN_PRIMITIVE_TYPE_CASES: {
        auto type = ParsePrimitiveType();
        if (!Ok())
            return Fail();
        return type;
    }

    default:
        return Fail();
    }
}

std::unique_ptr<raw::ConstDeclaration>
Parser::ParseConstDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kConst, attributes);

    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kEqual), true);
    if (!Ok())
        return Fail();
    auto constant = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ConstDeclaration>(start, MarkLastUseful(), std::move(attributes), std::move(type),
                                                   std::move(identifier), std::move(constant));
}

std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kEqual), true);
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    Token start;
    if (attributes != nullptr) {
        start = attributes->start_;
    } else {
        start = identifier->start_;
    }
    return std::make_unique<raw::EnumMember>(start, MarkLastUseful(), std::move(identifier), std::move(member_value), std::move(attributes));
}

std::unique_ptr<raw::EnumDeclaration>
Parser::ParseEnumDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::EnumMember>> members;

    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kEnum, attributes);

    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::PrimitiveType> subtype;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        subtype = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }
    ConsumeToken(OfKind(Token::Kind::kLeftCurly), true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek().combined()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly), true);
            return Done;

        TOKEN_ATTR_CASES:
            // intentional fallthrough for attribute parsing
        TOKEN_TYPE_CASES:
            members.emplace_back(ParseEnumMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::EnumDeclaration>(start, MarkLastUseful(),
                                                  std::move(attributes), std::move(identifier),
                                                  std::move(subtype), std::move(members));
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Parameter>(type->start_, MarkLastUseful(), std::move(type), std::move(identifier));
}

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<raw::Parameter>> parameter_list;
    Token start;

    switch (Peek().combined()) {
    default:
        break;

    TOKEN_TYPE_CASES:
        auto parameter = ParseParameter();
        if (start.kind() != Token::Kind::kNotAToken) {
            start = parameter->start_;
        }
        parameter_list.emplace_back(std::move(parameter));
        if (!Ok())
            return Fail();
        while (Peek().kind() == Token::Kind::kComma) {
            ConsumeToken(OfKind(Token::Kind::kComma), true);
            if (!Ok())
                return Fail();
            switch (Peek().combined()) {
            TOKEN_TYPE_CASES:
                parameter_list.emplace_back(ParseParameter());
                if (!Ok())
                    return Fail();
                break;

            default:
                return Fail();
            }
        }
    }

    return std::make_unique<raw::ParameterList>(start, MarkLastUseful(), std::move(parameter_list));
}

std::unique_ptr<raw::InterfaceMethod> Parser::ParseInterfaceMethod(std::unique_ptr<raw::AttributeList> attributes) {
    Token start;
    auto ordinal = ParseOrdinal();
    if (!Ok())
        return Fail();
    if (attributes != nullptr && attributes->attributes_->attributes_.size() != 0) {
        start = attributes->start_;
    } else {
        start = ordinal->start_;
    }

    std::unique_ptr<raw::Identifier> method_name;
    std::unique_ptr<raw::ParameterList> maybe_request;
    std::unique_ptr<raw::ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(OfKind(Token::Kind::kLeftParen), true);
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(OfKind(Token::Kind::kRightParen), true);
        if (!Ok())
            return false;
        return true;
    };

    if (MaybeConsumeToken(OfKind(Token::Kind::kArrow))) {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_response))
            return Fail();
    } else {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_request))
            return Fail();

        if (MaybeConsumeToken(OfKind(Token::Kind::kArrow))) {
            if (!Ok())
                return Fail();
            if (!parse_params(&maybe_response))
                return Fail();
        }
    }

    assert(method_name);
    assert(maybe_request || maybe_response);

    return std::make_unique<raw::InterfaceMethod>(start, MarkLastUseful(),
                                                  std::move(attributes),
                                                  std::move(ordinal),
                                                  std::move(method_name),
                                                  std::move(maybe_request),
                                                  std::move(maybe_response));
}

std::unique_ptr<raw::InterfaceDeclaration>
Parser::ParseInterfaceDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<raw::InterfaceMethod>> methods;

    // The first token may be the word "interface", or it may be the beginning
    // of the attribute list.
    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kInterface, attributes);

    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        for (;;) {
            superinterfaces.emplace_back(ParseCompoundIdentifier());
            if (!Ok())
                return Fail();
            if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
                break;
        }
    }

    ConsumeToken(OfKind(Token::Kind::kLeftCurly), true);
    if (!Ok())
        return Fail();

    auto parse_member = [&methods, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek().kind()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly), true);
            return Done;

        case Token::Kind::kNumericLiteral:
            methods.emplace_back(ParseInterfaceMethod(std::move(attributes)));
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<raw::InterfaceDeclaration>(start, MarkLastUseful(),
                                                       std::move(attributes), std::move(identifier),
                                                       std::move(superinterfaces),
                                                       std::move(methods));
}

std::unique_ptr<raw::StructMember> Parser::ParseStructMember() {
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_default_value;
    if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    Token start;
    if (attributes != nullptr) {
        start = attributes->start_;
    } else {
        start = type->start_;
    }
    return std::make_unique<raw::StructMember>(start, MarkLastUseful(),
                                               std::move(type), std::move(identifier),
                                               std::move(maybe_default_value), std::move(attributes));
}

std::unique_ptr<raw::StructDeclaration>
Parser::ParseStructDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::StructMember>> members;

    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kStruct, attributes);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftCurly), true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek().combined()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly), true);
            return Done;

        TOKEN_ATTR_CASES:
            // intentional fallthrough for attribute parsing
        TOKEN_TYPE_CASES:
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::StructDeclaration>(start, MarkLastUseful(),
                                                    std::move(attributes), std::move(identifier),
                                                    std::move(members));
}

std::unique_ptr<raw::TableMember>
Parser::ParseTableMember() {
    std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();

    auto ordinal = ParseOrdinal();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
        if (!Ok())
            return Fail();
        if (attributes != nullptr)
            return Fail("Cannot attach attributes to reserved ordinals");
        return std::make_unique<raw::TableMember>(ordinal->start_, MarkLastUseful(), std::move(ordinal));
    }

    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_default_value;
    if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    Token start;
    if (attributes != nullptr) {
        start = attributes->start_;
    } else {
        start = ordinal->start_;
    }

    return std::make_unique<raw::TableMember>(start, MarkLastUseful(), std::move(ordinal), std::move(type),
                                              std::move(identifier),
                                              std::move(maybe_default_value), std::move(attributes));
}

std::unique_ptr<raw::TableDeclaration>
Parser::ParseTableDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::TableMember>> members;

    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kTable, attributes);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftCurly), true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek().combined()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly), true);
            return Done;

        CASE_TOKEN(Token::Kind::kNumericLiteral):
        TOKEN_ATTR_CASES:
            members.emplace_back(ParseTableMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail("Tables must have at least one member");

    return std::make_unique<raw::TableDeclaration>(start, MarkLastUseful(),
                                                   std::move(attributes), std::move(identifier),
                                                   std::move(members));
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    Token start;
    if (attributes != nullptr) {
        start = attributes->start_;
    } else {
        start = type->start_;
    }
    return std::make_unique<raw::UnionMember>(start, MarkLastUseful(), std::move(type), std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::UnionDeclaration>
Parser::ParseUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::UnionMember>> members;

    Token start = ConsumeIdentifierReturnEarliest(Token::Subkind::kUnion, attributes);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kLeftCurly), true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek().combined()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly), true);
            return Done;

        TOKEN_ATTR_CASES:
            // intentional fallthrough for attribute parsing
        TOKEN_TYPE_CASES:
            members.emplace_back(ParseUnionMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        Fail();

    return std::make_unique<raw::UnionDeclaration>(start, MarkLastUseful(),
                                                   std::move(attributes), std::move(identifier),
                                                   std::move(members));
}

std::unique_ptr<raw::File> Parser::ParseFile() {
    std::vector<std::unique_ptr<raw::Using>> using_list;
    std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<raw::InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<raw::TableDeclaration>> table_declaration_list;
    std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;

    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    Token start = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kLibrary));
    if (!Ok())
        return Fail();
    auto library_name = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
    if (!Ok())
        return Fail();

    auto parse_using = [&using_list, this]() {
        switch (Peek().combined()) {
        default:
            return Done;

        CASE_IDENTIFIER(Token::Subkind::kUsing):
            using_list.emplace_back(ParseUsing());
            return More;
        }
    };

    while (parse_using() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }

    auto parse_declaration = [&const_declaration_list, &enum_declaration_list,
                              &interface_declaration_list, &struct_declaration_list,
                              &table_declaration_list, &union_declaration_list, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek().combined()) {
        default:
            return Done;

        CASE_IDENTIFIER(Token::Subkind::kConst):
            const_declaration_list.emplace_back(ParseConstDeclaration(std::move(attributes)));
            return More;

        CASE_IDENTIFIER(Token::Subkind::kEnum):
            enum_declaration_list.emplace_back(ParseEnumDeclaration(std::move(attributes)));
            return More;

        CASE_IDENTIFIER(Token::Subkind::kInterface):
            interface_declaration_list.emplace_back(
                ParseInterfaceDeclaration(std::move(attributes)));
            return More;

        CASE_IDENTIFIER(Token::Subkind::kStruct):
            struct_declaration_list.emplace_back(ParseStructDeclaration(std::move(attributes)));
            return More;

        CASE_IDENTIFIER(Token::Subkind::kTable):
            table_declaration_list.emplace_back(ParseTableDeclaration(std::move(attributes)));
            return More;

        CASE_IDENTIFIER(Token::Subkind::kUnion):
            union_declaration_list.emplace_back(ParseUnionDeclaration(std::move(attributes)));
            return More;
        }
    };

    while (parse_declaration() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon), true);
        if (!Ok())
            return Fail();
    }

    Token end = ConsumeToken(OfKind(Token::Kind::kEndOfFile), false);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::File>(
        start, end,
        std::move(attributes), std::move(library_name), std::move(using_list), std::move(const_declaration_list),
        std::move(enum_declaration_list), std::move(interface_declaration_list),
        std::move(struct_declaration_list), std::move(table_declaration_list), std::move(union_declaration_list));
}

} // namespace fidl
