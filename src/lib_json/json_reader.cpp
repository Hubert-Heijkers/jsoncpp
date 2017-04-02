// Copyright 2007-2011 Baptiste Lepilleur
// Copyright (C) 2016 InfoTeCS JSC. All rights reserved.
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#if !defined(JSON_IS_AMALGAMATION)
#include <json/assertions.h>
#include <json/reader.h>
#include <json/value.h>
#include "json_tool.h"
#endif // if !defined(JSON_IS_AMALGAMATION)
#include <utility>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <istream>
#include <sstream>
#include <memory>
#include <set>
#include <limits>

#if defined(_MSC_VER)
#if !defined(WINCE) && defined(__STDC_SECURE_LIB__) && _MSC_VER >= 1500 // VC++ 9.0 and above 
#define snprintf sprintf_s
#elif _MSC_VER >= 1900 // VC++ 14.0 and above
#define snprintf std::snprintf
#else
#define snprintf _snprintf
#endif
#elif defined(__ANDROID__) || defined(__QNXNTO__)
#define snprintf snprintf
#elif __cplusplus >= 201103L
#if !defined(__MINGW32__) && !defined(__CYGWIN__)
#define snprintf std::snprintf
#endif
#endif

#if defined(__QNXNTO__)
#define sscanf std::sscanf
#endif

#if defined(_MSC_VER) && _MSC_VER >= 1400 // VC++ 8.0
// Disable warning about strdup being deprecated.
#pragma warning(disable : 4996)
#endif

// Define JSONCPP_DEPRECATED_STACK_LIMIT as an appropriate integer at compile time to change the stack limit
#if !defined(JSONCPP_DEPRECATED_STACK_LIMIT)
#define JSONCPP_DEPRECATED_STACK_LIMIT 1000
#endif

static size_t const stackLimit_g = JSONCPP_DEPRECATED_STACK_LIMIT; // see readValue()

namespace Json {

#if __cplusplus >= 201103L || (defined(_CPPLIB_VER) && _CPPLIB_VER >= 520)
typedef std::unique_ptr<CharReader> CharReaderPtr;
#else
typedef std::auto_ptr<CharReader>   CharReaderPtr;
#endif

// class Features
// ////////////////////////////////

class Features {
public:
  static Features all();
  bool allowComments_;
  bool strictRoot_;
  bool allowDroppedNullPlaceholders_;
  bool allowNumericKeys_;
  bool allowSingleQuotes_;
  bool failIfExtra_;
  bool rejectDupKeys_;
  bool allowSpecialFloats_;
  int stackLimit_;
};  // Features

// Implementation of class Features
// ////////////////////////////////

Features Features::all() { return Features(); }

// class OurReader
// ////////////////////////////////

class OurReader {
public:
  OurReader(Features const& features);
  bool parse(const char* beginDoc,
             const char* endDoc,
             Value& root,
             bool collectComments = true);
  JSONCPP_STRING getFormattedErrorMessages() const;
  bool pushError(const Value& value, const JSONCPP_STRING& message);
  bool pushError(const Value& value, const JSONCPP_STRING& message, const Value& extra);

private:
  OurReader(OurReader const&);  // no impl
  void operator=(OurReader const&);  // no impl

  enum TokenType {
    tokenEndOfStream = 0,
    tokenObjectBegin,
    tokenObjectEnd,
    tokenArrayBegin,
    tokenArrayEnd,
    tokenString,
    tokenNumber,
    tokenTrue,
    tokenFalse,
    tokenNull,
    tokenNaN,
    tokenPosInf,
    tokenNegInf,
    tokenArraySeparator,
    tokenMemberSeparator,
    tokenComment,
    tokenError
  };

  class Token {
  public:
    TokenType type_;
    ptrdiff_t offsetStart_;
    ptrdiff_t offsetEnd_;
  };

  class ErrorLocation {
  public:
    size_t line_;
    size_t column_;
    ErrorLocation() : line_(0), column_(0) {}
    ErrorLocation(const ErrorLocation& location) : line_(location.line_), column_(location.column_) {}
    JSONCPP_STRING toString() const {
      char buffer[18 + 16 + 16 + 1];
      snprintf(buffer, sizeof(buffer), "Line %d, Column %d", line_, column_);
      return buffer;
    }
  };

  class ErrorInfo {
  public:
    ErrorLocation token_;
    JSONCPP_STRING message_;
    ErrorLocation extra_;
  };

  typedef std::deque<ErrorInfo> Errors;

  bool readToken(Token& token);
  void skipSpaces();
  bool match(const char* pattern, int patternLength);
  bool readComment();
  bool readCStyleComment();
  bool readCppStyleComment();
  bool readString();
  bool readStringSingleQuote();
  bool readNumber(bool checkInf);
  bool readValue();
  bool readObject(Token& token);
  bool readArray(Token& token);
  bool decodeNumber(Token& token);
  bool decodeNumber(Token& token, Value& decoded);
  bool decodeString(Token& token);
  bool decodeString(Token& token, JSONCPP_STRING& decoded);
  bool decodeDouble(Token& token);
  bool decodeDouble(Token& token, Value& decoded);
  bool decodeUnicodeCodePoint(Token& token,
                              const char*& current,
                              const char* end,
                              unsigned int& unicode);
  bool decodeUnicodeEscapeSequence(Token& token,
                                   const char*& current,
                                   const char* end,
                                   unsigned int& unicode);
  bool addError(const JSONCPP_STRING& message, Token& token, const char* extra = 0);
  bool recoverFromError(TokenType skipUntilToken);
  bool addErrorAndRecover(const JSONCPP_STRING& message,
                          Token& token,
                          TokenType skipUntilToken);
  Value& currentValue();
  char getNextChar();
  void getLocationLineAndColumn(const ptrdiff_t offset, ErrorLocation& location) const;
  void addComment(const char* begin, const char* end, CommentPlacement placement);
  void skipCommentTokens(Token& token);

  typedef std::stack<Value*> Nodes;
  Nodes nodes_;
  Errors errors_;
  JSONCPP_STRING document_;
  const char* begin_;
  const char* end_;
  const char* current_;
  const char* lastValueEnd_;
  Value* lastValue_;
  JSONCPP_STRING commentsBefore_;

  Features const features_;
  bool collectComments_;
};  // OurReader

static bool containsNewLine(const char* begin, const char* end) {
	for (; begin < end; ++begin)
		if (*begin == '\n' || *begin == '\r')
			return true;
	return false;
}

// Implementation of class OurReader
// ////////////////////////////////

OurReader::OurReader(Features const& features)
    : errors_(), document_(), begin_(), end_(), current_(), lastValueEnd_(),
      lastValue_(), commentsBefore_(),
      features_(features), collectComments_() {
}

bool OurReader::parse(const char* beginDoc,
                   const char* endDoc,
                   Value& root,
                   bool collectComments) {
  if (!features_.allowComments_) {
    collectComments = false;
  }

  begin_ = beginDoc;
  end_ = endDoc;
  collectComments_ = collectComments;
  current_ = begin_;
  lastValueEnd_ = 0;
  lastValue_ = 0;
  commentsBefore_ = "";
  errors_.clear();
  while (!nodes_.empty())
    nodes_.pop();
  nodes_.push(&root);

  bool successful = readValue();
  Token token;
  skipCommentTokens(token);
  if (features_.failIfExtra_) {
    if ((features_.strictRoot_ || token.type_ != tokenError) && token.type_ != tokenEndOfStream) {
      addError("Extra non-whitespace after JSON value.", token);
      return false;
    }
  }
  if (collectComments_ && !commentsBefore_.empty())
    root.setComment(commentsBefore_, commentAfter);
  if (features_.strictRoot_) {
    if (!root.isArray() && !root.isObject()) {
      // Set error location to start of doc, ideally should be first token found
      // in doc
      token.type_ = tokenError;
      token.offsetStart_ = 0;
      token.offsetEnd_ = endDoc - beginDoc;
      addError(
          "A valid JSON document must be either an array or an object value.",
          token);
      return false;
    }
  }
  return successful;
}

bool OurReader::readValue() {
  //  To preserve the old behaviour we cast size_t to int.
  if (static_cast<int>(nodes_.size()) > features_.stackLimit_) throwRuntimeError("Exceeded stackLimit in readValue().");
  Token token;
  skipCommentTokens(token);
  bool successful = true;

  if (collectComments_ && !commentsBefore_.empty()) {
    currentValue().setComment(commentsBefore_, commentBefore);
    commentsBefore_ = "";
  }

  switch (token.type_) {
  case tokenObjectBegin:
    successful = readObject(token);
    currentValue().setOffsetLimit(current_ - begin_);
    break;
  case tokenArrayBegin:
    successful = readArray(token);
    currentValue().setOffsetLimit(current_ - begin_);
    break;
  case tokenNumber:
    successful = decodeNumber(token);
    break;
  case tokenString:
    successful = decodeString(token);
    break;
  case tokenTrue:
    {
    Value v(true);
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenFalse:
    {
    Value v(false);
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenNull:
    {
    Value v;
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenNaN:
    {
    Value v(std::numeric_limits<double>::quiet_NaN());
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenPosInf:
    {
    Value v(std::numeric_limits<double>::infinity());
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenNegInf:
    {
    Value v(-std::numeric_limits<double>::infinity());
    currentValue().swapPayload(v);
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    }
    break;
  case tokenArraySeparator:
  case tokenObjectEnd:
  case tokenArrayEnd:
    if (features_.allowDroppedNullPlaceholders_) {
      // "Un-read" the current token and mark the current value as a null
      // token.
      current_--;
      Value v;
      currentValue().swapPayload(v);
      currentValue().setOffsetStart(current_ - begin_ - 1);
      currentValue().setOffsetLimit(current_ - begin_);
      break;
    } // else, fall through ...
  default:
    currentValue().setOffsetStart(token.offsetStart_);
    currentValue().setOffsetLimit(token.offsetEnd_);
    return addError("Syntax error: value, object or array expected.", token);
  }

  if (collectComments_) {
    lastValueEnd_ = current_;
    lastValue_ = &currentValue();
  }

  return successful;
}

void OurReader::skipCommentTokens(Token& token) {
  if (features_.allowComments_) {
    do {
      readToken(token);
    } while (token.type_ == tokenComment);
  } else {
    readToken(token);
  }
}

bool OurReader::readToken(Token& token) {
  skipSpaces();
  token.offsetStart_ = current_ - begin_;
  char c = getNextChar();
  bool ok = true;
  switch (c) {
  case '{':
    token.type_ = tokenObjectBegin;
    break;
  case '}':
    token.type_ = tokenObjectEnd;
    break;
  case '[':
    token.type_ = tokenArrayBegin;
    break;
  case ']':
    token.type_ = tokenArrayEnd;
    break;
  case '"':
    token.type_ = tokenString;
    ok = readString();
    break;
  case '\'':
    if (features_.allowSingleQuotes_) {
    token.type_ = tokenString;
    ok = readStringSingleQuote();
    break;
    } // else continue
  case '/':
    token.type_ = tokenComment;
    ok = readComment();
    break;
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
    token.type_ = tokenNumber;
    readNumber(false);
    break;
  case '-':
    if (readNumber(true)) {
      token.type_ = tokenNumber;
    } else {
      token.type_ = tokenNegInf;
      ok = features_.allowSpecialFloats_ && match("nfinity", 7);
    }
    break;
  case 't':
    token.type_ = tokenTrue;
    ok = match("rue", 3);
    break;
  case 'f':
    token.type_ = tokenFalse;
    ok = match("alse", 4);
    break;
  case 'n':
    token.type_ = tokenNull;
    ok = match("ull", 3);
    break;
  case 'N':
    if (features_.allowSpecialFloats_) {
      token.type_ = tokenNaN;
      ok = match("aN", 2);
    } else {
      ok = false;
    }
    break;
  case 'I':
    if (features_.allowSpecialFloats_) {
      token.type_ = tokenPosInf;
      ok = match("nfinity", 7);
    } else {
      ok = false;
    }
    break;
  case ',':
    token.type_ = tokenArraySeparator;
    break;
  case ':':
    token.type_ = tokenMemberSeparator;
    break;
  case 0:
    token.type_ = tokenEndOfStream;
    break;
  default:
    ok = false;
    break;
  }
  if (!ok)
    token.type_ = tokenError;
  token.offsetEnd_ = current_ - begin_;
  return true;
}

void OurReader::skipSpaces() {
  while (current_ != end_) {
    char c = *current_;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      ++current_;
    else
      break;
  }
}

bool OurReader::match(const char* pattern, int patternLength) {
  if (end_ - current_ < patternLength)
    return false;
  int index = patternLength;
  while (index--)
    if (current_[index] != pattern[index])
      return false;
  current_ += patternLength;
  return true;
}

bool OurReader::readComment() {
  const char* commentBegin = current_ - 1;
  char c = getNextChar();
  bool successful = false;
  if (c == '*')
    successful = readCStyleComment();
  else if (c == '/')
    successful = readCppStyleComment();
  if (!successful)
    return false;

  if (collectComments_) {
    CommentPlacement placement = commentBefore;
    if (lastValueEnd_ && !containsNewLine(lastValueEnd_, commentBegin)) {
      if (c != '*' || !containsNewLine(commentBegin, current_))
        placement = commentAfterOnSameLine;
    }

    addComment(commentBegin, current_, placement);
  }
  return true;
}

static JSONCPP_STRING normalizeEOL(const char* begin, const char* end) {
	JSONCPP_STRING normalized;
	normalized.reserve(static_cast<size_t>(end - begin));
	const char* current = begin;
	while (current != end) {
		char c = *current++;
		if (c == '\r') {
			if (current != end && *current == '\n')
				// convert dos EOL
				++current;
			// convert Mac EOL
			normalized += '\n';
		}
		else {
			normalized += c;
		}
	}
	return normalized;
}

void OurReader::addComment(const char* begin, const char* end, CommentPlacement placement) {
  assert(collectComments_);
  const JSONCPP_STRING& normalized = normalizeEOL(begin, end);
  if (placement == commentAfterOnSameLine) {
    assert(lastValue_ != 0);
    lastValue_->setComment(normalized, placement);
  } else {
    commentsBefore_ += normalized;
  }
}

bool OurReader::readCStyleComment() {
  while ((current_ + 1) < end_) {
    char c = getNextChar();
    if (c == '*' && *current_ == '/')
      break;
  }
  return getNextChar() == '/';
}

bool OurReader::readCppStyleComment() {
  while (current_ != end_) {
    char c = getNextChar();
    if (c == '\n')
      break;
    if (c == '\r') {
      // Consume DOS EOL. It will be normalized in addComment.
      if (current_ != end_ && *current_ == '\n')
        getNextChar();
      // Break on Moc OS 9 EOL.
      break;
    }
  }
  return true;
}

bool OurReader::readNumber(bool checkInf) {
  const char *p = current_;
  if (checkInf && p != end_ && *p == 'I') {
    current_ = ++p;
    return false;
  }
  char c = '0'; // stopgap for already consumed character
  // integral part
  while (c >= '0' && c <= '9')
    c = (current_ = p) < end_ ? *p++ : '\0';
  // fractional part
  if (c == '.') {
    c = (current_ = p) < end_ ? *p++ : '\0';
    while (c >= '0' && c <= '9')
      c = (current_ = p) < end_ ? *p++ : '\0';
  }
  // exponential part
  if (c == 'e' || c == 'E') {
    c = (current_ = p) < end_ ? *p++ : '\0';
    if (c == '+' || c == '-')
      c = (current_ = p) < end_ ? *p++ : '\0';
    while (c >= '0' && c <= '9')
      c = (current_ = p) < end_ ? *p++ : '\0';
  }
  return true;
}
bool OurReader::readString() {
  char c = 0;
  while (current_ != end_) {
    c = getNextChar();
    if (c == '\\')
      getNextChar();
    else if (c == '"')
      break;
  }
  return c == '"';
}


bool OurReader::readStringSingleQuote() {
  char c = 0;
  while (current_ != end_) {
    c = getNextChar();
    if (c == '\\')
      getNextChar();
    else if (c == '\'')
      break;
  }
  return c == '\'';
}

bool OurReader::readObject(Token& tokenStart) {
  Token tokenName;
  JSONCPP_STRING name;
  Value init(objectValue);
  currentValue().swapPayload(init);
  currentValue().setOffsetStart(tokenStart.offsetStart_);
  while (readToken(tokenName)) {
    bool initialTokenOk = true;
    while (tokenName.type_ == tokenComment && initialTokenOk)
      initialTokenOk = readToken(tokenName);
    if (!initialTokenOk)
      break;
    if (tokenName.type_ == tokenObjectEnd && name.empty()) // empty object
      return true;
    name = "";
    if (tokenName.type_ == tokenString) {
      if (!decodeString(tokenName, name))
        return recoverFromError(tokenObjectEnd);
    } else if (tokenName.type_ == tokenNumber && features_.allowNumericKeys_) {
      Value numberName;
      if (!decodeNumber(tokenName, numberName))
        return recoverFromError(tokenObjectEnd);
      name = numberName.asString();
    } else {
      break;
    }

    Token colon;
    if (!readToken(colon) || colon.type_ != tokenMemberSeparator) {
      return addErrorAndRecover(
          "Missing ':' after object member name", colon, tokenObjectEnd);
    }
    if (name.length() >= (1U<<30)) throwRuntimeError("keylength >= 2^30");
    if (features_.rejectDupKeys_ && currentValue().isMember(name)) {
      JSONCPP_STRING msg = "Duplicate key: '" + name + "'";
      return addErrorAndRecover(
          msg, tokenName, tokenObjectEnd);
    }
    Value& value = currentValue()[name];
    nodes_.push(&value);
    bool ok = readValue();
    nodes_.pop();
    if (!ok) // error already set
      return recoverFromError(tokenObjectEnd);

    Token comma;
    if (!readToken(comma) ||
        (comma.type_ != tokenObjectEnd && comma.type_ != tokenArraySeparator &&
         comma.type_ != tokenComment)) {
      return addErrorAndRecover(
          "Missing ',' or '}' in object declaration", comma, tokenObjectEnd);
    }
    bool finalizeTokenOk = true;
    while (comma.type_ == tokenComment && finalizeTokenOk)
      finalizeTokenOk = readToken(comma);
    if (comma.type_ == tokenObjectEnd)
      return true;
  }
  return addErrorAndRecover(
      "Missing '}' or object member name", tokenName, tokenObjectEnd);
}

bool OurReader::readArray(Token& tokenStart) {
  Value init(arrayValue);
  currentValue().swapPayload(init);
  currentValue().setOffsetStart(tokenStart.offsetStart_);
  skipSpaces();
  if (current_ != end_ && *current_ == ']') // empty array
  {
    Token endArray;
    readToken(endArray);
    return true;
  }
  int index = 0;
  for (;;) {
    Value& value = currentValue()[index++];
    nodes_.push(&value);
    bool ok = readValue();
    nodes_.pop();
    if (!ok) // error already set
      return recoverFromError(tokenArrayEnd);

    Token token;
    // Accept Comment after last item in the array.
    ok = readToken(token);
    while (token.type_ == tokenComment && ok) {
      ok = readToken(token);
    }
    bool badTokenType =
        (token.type_ != tokenArraySeparator && token.type_ != tokenArrayEnd);
    if (!ok || badTokenType) {
      return addErrorAndRecover(
          "Missing ',' or ']' in array declaration", token, tokenArrayEnd);
    }
    if (token.type_ == tokenArrayEnd)
      break;
  }
  return true;
}

bool OurReader::decodeNumber(Token& token) {
  Value decoded;
  if (!decodeNumber(token, decoded))
    return false;
  currentValue().swapPayload(decoded);
  currentValue().setOffsetStart(token.offsetStart_);
  currentValue().setOffsetLimit(token.offsetEnd_);
  return true;
}

bool OurReader::decodeNumber(Token& token, Value& decoded) {
  // Attempts to parse the number as an integer. If the number is
  // larger than the maximum supported value of an integer then
  // we decode the number as a double.
  const char* current = token.offsetStart_ + begin_;
  const char* end = token.offsetEnd_ + begin_;
  bool isNegative = *current == '-';
  if (isNegative)
    ++current;
  // TODO: Help the compiler do the div and mod at compile time or get rid of them.
  Value::LargestUInt maxIntegerValue =
      isNegative ? Value::LargestUInt(-Value::minLargestInt)
                 : Value::maxLargestUInt;
  Value::LargestUInt threshold = maxIntegerValue / 10;
  Value::LargestUInt value = 0;
  while (current < end) {
    char c = *current++;
    if (c < '0' || c > '9')
      return decodeDouble(token, decoded);
    Value::UInt digit(static_cast<Value::UInt>(c - '0'));
    if (value >= threshold) {
      // We've hit or exceeded the max value divided by 10 (rounded down). If
      // a) we've only just touched the limit, b) this is the last digit, and
      // c) it's small enough to fit in that rounding delta, we're okay.
      // Otherwise treat this number as a double to avoid overflow.
      if (value > threshold || current != end ||
          digit > maxIntegerValue % 10) {
        return decodeDouble(token, decoded);
      }
    }
    value = value * 10 + digit;
  }
  if (isNegative)
    decoded = -Value::LargestInt(value);
  else if (value <= Value::LargestUInt(Value::maxInt))
    decoded = Value::LargestInt(value);
  else
    decoded = value;
  return true;
}

bool OurReader::decodeDouble(Token& token) {
  Value decoded;
  if (!decodeDouble(token, decoded))
    return false;
  currentValue().swapPayload(decoded);
  currentValue().setOffsetStart(token.offsetStart_);
  currentValue().setOffsetLimit(token.offsetEnd_);
  return true;
}

bool OurReader::decodeDouble(Token& token, Value& decoded) {
  double value = 0;
  const int bufferSize = 32;
  int count;
  ptrdiff_t const length = token.offsetEnd_ - token.offsetStart_;

  // Sanity check to avoid buffer overflow exploits.
  if (length < 0) {
    return addError("Unable to parse token length", token);
  }
  size_t const ulength = static_cast<size_t>(length);

  // Avoid using a string constant for the format control string given to
  // sscanf, as this can cause hard to debug crashes on OS X. See here for more
  // info:
  //
  //     http://developer.apple.com/library/mac/#DOCUMENTATION/DeveloperTools/gcc-4.0.1/gcc/Incompatibilities.html
  char format[] = "%lf";

  if (length <= bufferSize) {
    char buffer[bufferSize + 1];
    memcpy(buffer, token.offsetStart_ + begin_, ulength);
    buffer[length] = 0;
    fixNumericLocaleInput(buffer, buffer + length);
    count = sscanf(buffer, format, &value);
  } else {
    JSONCPP_STRING buffer(token.offsetStart_ + begin_, token.offsetEnd_ + begin_);
    count = sscanf(buffer.c_str(), format, &value);
  }

  if (count != 1)
    return addError("'" + JSONCPP_STRING(token.offsetStart_ + begin_, token.offsetEnd_ + begin_) +
                        "' is not a number.",
                    token);
  decoded = value;
  return true;
}

bool OurReader::decodeString(Token& token) {
  JSONCPP_STRING decoded_string;
  if (!decodeString(token, decoded_string))
    return false;
  Value decoded(decoded_string);
  currentValue().swapPayload(decoded);
  currentValue().setOffsetStart(token.offsetStart_);
  currentValue().setOffsetLimit(token.offsetEnd_);
  return true;
}

bool OurReader::decodeString(Token& token, JSONCPP_STRING& decoded) {
  decoded.reserve(static_cast<size_t>(token.offsetEnd_ - token.offsetStart_ - 2));
  const char* current = token.offsetStart_ + begin_ + 1; // skip '"'
  const char* end = token.offsetEnd_ + begin_ - 1;       // do not include '"'
  while (current != end) {
    char c = *current++;
    if (c == '"')
      break;
    else if (c == '\\') {
      if (current == end)
        return addError("Empty escape sequence in string", token, current);
      char escape = *current++;
      switch (escape) {
      case '"':
        decoded += '"';
        break;
      case '/':
        decoded += '/';
        break;
      case '\\':
        decoded += '\\';
        break;
      case 'b':
        decoded += '\b';
        break;
      case 'f':
        decoded += '\f';
        break;
      case 'n':
        decoded += '\n';
        break;
      case 'r':
        decoded += '\r';
        break;
      case 't':
        decoded += '\t';
        break;
      case 'u': {
        unsigned int unicode;
        if (!decodeUnicodeCodePoint(token, current, end, unicode))
          return false;
        decoded += codePointToUTF8(unicode);
      } break;
      default:
        return addError("Bad escape sequence in string", token, current);
      }
    } else {
      decoded += c;
    }
  }
  return true;
}

bool OurReader::decodeUnicodeCodePoint(Token& token,
                                    const char*& current,
                                    const char* end,
                                    unsigned int& unicode) {

  if (!decodeUnicodeEscapeSequence(token, current, end, unicode))
    return false;
  if (unicode >= 0xD800 && unicode <= 0xDBFF) {
    // surrogate pairs
    if (end - current < 6)
      return addError(
          "additional six characters expected to parse unicode surrogate pair.",
          token,
          current);
    unsigned int surrogatePair;
    if (*(current++) == '\\' && *(current++) == 'u') {
      if (decodeUnicodeEscapeSequence(token, current, end, surrogatePair)) {
        unicode = 0x10000 + ((unicode & 0x3FF) << 10) + (surrogatePair & 0x3FF);
      } else
        return false;
    } else
      return addError("expecting another \\u token to begin the second half of "
                      "a unicode surrogate pair",
                      token,
                      current);
  }
  return true;
}

bool OurReader::decodeUnicodeEscapeSequence(Token& token,
                                         const char*& current,
                                         const char* end,
                                         unsigned int& ret_unicode) {
  if (end - current < 4)
    return addError(
        "Bad unicode escape sequence in string: four digits expected.",
        token,
        current);
  int unicode = 0;
  for (int index = 0; index < 4; ++index) {
    char c = *current++;
    unicode *= 16;
    if (c >= '0' && c <= '9')
      unicode += c - '0';
    else if (c >= 'a' && c <= 'f')
      unicode += c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      unicode += c - 'A' + 10;
    else
      return addError(
          "Bad unicode escape sequence in string: hexadecimal digit expected.",
          token,
          current);
  }
  ret_unicode = static_cast<unsigned int>(unicode);
  return true;
}

bool OurReader::addError(const JSONCPP_STRING& message, Token& token, const char* extra) {
  ErrorInfo info;
  getLocationLineAndColumn(token.offsetStart_, info.token_);
  info.message_ = message;
  if (extra) {
    getLocationLineAndColumn(extra - begin_, info.extra_);
  }
  errors_.push_back(info);
  return false;
}

bool OurReader::recoverFromError(TokenType skipUntilToken) {
  size_t errorCount = errors_.size();
  Token skip;
  for (;;) {
    if (!readToken(skip))
      errors_.resize(errorCount); // discard errors caused by recovery
    if (skip.type_ == skipUntilToken || skip.type_ == tokenEndOfStream)
      break;
  }
  errors_.resize(errorCount);
  return false;
}

bool OurReader::addErrorAndRecover(const JSONCPP_STRING& message,
                                Token& token,
                                TokenType skipUntilToken) {
  addError(message, token);
  return recoverFromError(skipUntilToken);
}

Value& OurReader::currentValue() { return *(nodes_.top()); }

char OurReader::getNextChar() {
  if (current_ == end_)
    return 0;
  return *current_++;
}

void OurReader::getLocationLineAndColumn(const ptrdiff_t offset,
                                         ErrorLocation& location) const {
  const char* current = begin_;
  const char* lastLineStart = current;
  const char* end = begin_ + offset;
  location.line_ = 0;
  while (current < end && current != end_) {
    char c = *current++;
    if (c == '\r') {
      if (*current == '\n')
        ++current;
      lastLineStart = current;
      ++location.line_;
    } else if (c == '\n') {
      lastLineStart = current;
      ++location.line_;
    }
  }
  // column & line start at 1
  location.column_ = int(end - lastLineStart) + 1;
  ++location.line_;
}

JSONCPP_STRING OurReader::getFormattedErrorMessages() const {
  JSONCPP_STRING formattedMessage;
  for (Errors::const_iterator itError = errors_.begin();
       itError != errors_.end();
       ++itError) {
    const ErrorInfo& error = *itError;
    formattedMessage += "* " + error.token_.toString() + "\n";
    formattedMessage += "  " + error.message_ + "\n";
    if (error.extra_.line_ > 0)
      formattedMessage += "See " + error.extra_.toString() + " for detail.\n";
  }
  return formattedMessage;
}

bool OurReader::pushError(const Value& value, const JSONCPP_STRING& message) {
  ptrdiff_t length = end_ - begin_;
  if(value.getOffsetStart() > length
    || value.getOffsetLimit() > length)
    return false;
  ErrorInfo info;
  getLocationLineAndColumn(value.getOffsetStart(), info.token_);
  info.message_ = message;
  errors_.push_back(info);
  return true;
}

bool OurReader::pushError(const Value& value, const JSONCPP_STRING& message, const Value& extra) {
  ptrdiff_t length = end_ - begin_;
  if(value.getOffsetStart() > length
    || value.getOffsetLimit() > length
    || extra.getOffsetLimit() > length)
    return false;
  ErrorInfo info;
  getLocationLineAndColumn(value.getOffsetStart(), info.token_);
  info.message_ = message;
  getLocationLineAndColumn(extra.getOffsetStart(), info.extra_);
  errors_.push_back(info);
  return true;
}

// class OurCharReader
// ////////////////////////////////

class OurCharReader : public CharReader {
  bool const collectComments_;
  OurReader reader_;
public:
  OurCharReader(
    bool collectComments,
    Features const& features)
  : collectComments_(collectComments)
  , reader_(features)
  {}
  bool parse(
      char const* beginDoc, char const* endDoc,
      Value* root, JSONCPP_STRING* errs) JSONCPP_OVERRIDE {
    bool ok = reader_.parse(beginDoc, endDoc, *root, collectComments_);
    if (errs) {
      *errs = reader_.getFormattedErrorMessages();
    }
    return ok;
  }
};

// Implementation of class CharReaderBuilder
// ////////////////////////////////

CharReaderBuilder::CharReaderBuilder()
{
  setDefaults(&settings_);
}
CharReaderBuilder::~CharReaderBuilder()
{}
CharReader* CharReaderBuilder::newCharReader() const
{
  bool collectComments = settings_["collectComments"].asBool();
  Features features = Features::all();
  features.allowComments_ = settings_["allowComments"].asBool();
  features.strictRoot_ = settings_["strictRoot"].asBool();
  features.allowDroppedNullPlaceholders_ = settings_["allowDroppedNullPlaceholders"].asBool();
  features.allowNumericKeys_ = settings_["allowNumericKeys"].asBool();
  features.allowSingleQuotes_ = settings_["allowSingleQuotes"].asBool();
  features.stackLimit_ = settings_["stackLimit"].asInt();
  features.failIfExtra_ = settings_["failIfExtra"].asBool();
  features.rejectDupKeys_ = settings_["rejectDupKeys"].asBool();
  features.allowSpecialFloats_ = settings_["allowSpecialFloats"].asBool();
  return new OurCharReader(collectComments, features);
}
static void getValidReaderKeys(std::set<JSONCPP_STRING>* valid_keys)
{
  valid_keys->clear();
  valid_keys->insert("collectComments");
  valid_keys->insert("allowComments");
  valid_keys->insert("strictRoot");
  valid_keys->insert("allowDroppedNullPlaceholders");
  valid_keys->insert("allowNumericKeys");
  valid_keys->insert("allowSingleQuotes");
  valid_keys->insert("stackLimit");
  valid_keys->insert("failIfExtra");
  valid_keys->insert("rejectDupKeys");
  valid_keys->insert("allowSpecialFloats");
}
bool CharReaderBuilder::validate(Json::Value* invalid) const
{
  Json::Value my_invalid;
  if (!invalid) invalid = &my_invalid;  // so we do not need to test for NULL
  Json::Value& inv = *invalid;
  std::set<JSONCPP_STRING> valid_keys;
  getValidReaderKeys(&valid_keys);
  Value::Members keys = settings_.getMemberNames();
  size_t n = keys.size();
  for (size_t i = 0; i < n; ++i) {
    JSONCPP_STRING const& key = keys[i];
    if (valid_keys.find(key) == valid_keys.end()) {
      inv[key] = settings_[key];
    }
  }
  return 0u == inv.size();
}
Value& CharReaderBuilder::operator[](JSONCPP_STRING key)
{
  return settings_[key];
}
// static
void CharReaderBuilder::strictMode(Json::Value* settings)
{
//! [CharReaderBuilderStrictMode]
  (*settings)["allowComments"] = false;
  (*settings)["strictRoot"] = true;
  (*settings)["allowDroppedNullPlaceholders"] = false;
  (*settings)["allowNumericKeys"] = false;
  (*settings)["allowSingleQuotes"] = false;
  (*settings)["stackLimit"] = 1000;
  (*settings)["failIfExtra"] = true;
  (*settings)["rejectDupKeys"] = true;
  (*settings)["allowSpecialFloats"] = false;
//! [CharReaderBuilderStrictMode]
}
// static
void CharReaderBuilder::setDefaults(Json::Value* settings)
{
//! [CharReaderBuilderDefaults]
  (*settings)["collectComments"] = true;
  (*settings)["allowComments"] = true;
  (*settings)["strictRoot"] = false;
  (*settings)["allowDroppedNullPlaceholders"] = false;
  (*settings)["allowNumericKeys"] = false;
  (*settings)["allowSingleQuotes"] = false;
  (*settings)["stackLimit"] = 1000;
  (*settings)["failIfExtra"] = false;
  (*settings)["rejectDupKeys"] = false;
  (*settings)["allowSpecialFloats"] = false;
//! [CharReaderBuilderDefaults]
}

//////////////////////////////////
// global functions

bool parseFromStream(
    CharReader::Factory const& fact, JSONCPP_ISTREAM& sin,
    Value* root, JSONCPP_STRING* errs)
{
  JSONCPP_OSTRINGSTREAM ssin;
  ssin << sin.rdbuf();
  JSONCPP_STRING doc = ssin.str();
  char const* begin = doc.data();
  char const* end = begin + doc.size();
  // Note that we do not actually need a null-terminator.
  CharReaderPtr const reader(fact.newCharReader());
  return reader->parse(begin, end, root, errs);
}

JSONCPP_ISTREAM& operator>>(JSONCPP_ISTREAM& sin, Value& root) {
  CharReaderBuilder b;
  JSONCPP_STRING errs;
  bool ok = parseFromStream(b, sin, &root, &errs);
  if (!ok) {
    fprintf(stderr,
            "Error from reader: %s",
            errs.c_str());

    throwRuntimeError(errs);
  }
  return sin;
}

} // namespace Json
