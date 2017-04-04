// Copyright 2007-2010 Baptiste Lepilleur
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#ifndef CPPTL_JSON_READER_H_INCLUDED
#define CPPTL_JSON_READER_H_INCLUDED

#if !defined(JSON_IS_AMALGAMATION)
#include "value.h"
#endif // if !defined(JSON_IS_AMALGAMATION)
#include <deque>
#include <iosfwd>
#include <stack>
#include <string>
#include <istream>

// Disable warning C4251: <data member>: <type> needs to have dll-interface to
// be used by...
#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)
#pragma warning(push)
#pragma warning(disable : 4251)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#pragma pack(push, 8)

namespace Json {

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

/** Interface for reading JSON from a char array.
 */
class JSON_API Tokenizer {
public:
  virtual ~Tokenizer() {}
  /** \brief Set up the <a HREF="http://www.json.org">JSON</a> document to be 
   tokenized by this tokenizer.
   *
   * \param beginDoc Pointer to the beginning of the document.
   * \param endDoc Pointer to the end of the dcoument.
   */
  virtual void init(char const* beginDoc, char const* endDoc) = 0;
  /** \brief Read the next token from the <a HREF="http://www.json.org">JSON</a>
   document.
   *
   * \return The type of the next available token or tokenEndOfStream if the end
   of the document has been reached.
   */
  virtual TokenType readToken() = 0;
  /** \brief Read the name/value pair from the <a HREF="http://www.json.org">JSON</a>
   document.
   *
   * \param name [out] The, optionally decoded, name identifying the name/value pair.
   * \param value [out] The token type of the value in the name/value pair.
   * \return /c true if a genuine next token is available, /c false in case of an
   error or the end of the document has been reached.
   */
  virtual TokenType readNVP(JSONCPP_STRING& name) = 0;
  /** \brief Returns the decoded string value for the current token. 
   *
   * \param value [out] The decoded string value of the current/last token.
   * \return /c true if the type of the last token was indeed a string and a decoded 
   value is returned, /c false otherwise.
   */
  virtual bool getDecodedString(JSONCPP_STRING& value) = 0;
  /** \brief Returns the decoded double value of the current number token. 
   *
   * \param value [out] The double value of the current/last token.
   * \return /c true if the type of the last token was indeed a number and a decoded
   value is returned, /c false otherwise.
   */
  virtual bool getDecodedDouble(double& value) = 0;
  /** \brief Gives access to the raw data defining the current token.
   * \remark Note that the represention is not 0 terminated and that the difference
   between the beginToken and endToken will need to be used to calculate the number
   of chars to be considered part of the token!
   * \param beginToken [out] Pointer to the beginning of raw data for the token.
   * \param endToken [out] Pointer to the end of the raw data for the token.
   * \return /c true if the raw data could be retrieved, /c false otherwise.
   */
  virtual bool getRawString(const char*& beginToken, const char*& endToken) = 0;
  /** \brief Returns the error associated with the last error token.
   * \return The string containing the error that was found in the JSON document.
   */
  virtual JSONCPP_STRING getError() = 0;

  class JSON_API Factory {
  public:
    virtual ~Factory() {}
    /** \brief Allocate a Tokenizer via operator new() passing it the 
     * <a HREF="http://www.json.org">JSON</a> document to be read, token by token.
     * The document must be a UTF-8 encoded string containing the document to read.
     *
     * \param beginDoc Pointer on the beginning of the UTF-8 encoded string of the
     document to read.
     * \param endDoc Pointer on the end of the UTF-8 encoded string of the document 
     to read.
     *        Must be >= beginDoc.
     * \throw std::exception if something goes wrong (e.g. invalid settings)
     */
    virtual Tokenizer* newTokenizer() const = 0;
  };  // Factory
};  // Reader

/** \brief Build a Tokenizer implementation.

Usage:
\code
  using namespace Json;
  TokenizerBuilder builder;
  Tokenizer* tokenizer( builder.newTokenizer());
  builder["collectComments"] = false;
  char const* begin = doc.data();
  char const* end = begin + doc.size();
  tokenizer->init(begin, end);
  for ( bool bContinue = true; bContinue == true; )
  {
    const char* tbegin;
    const char* tend;
    TokenType ttype( tokenizer->NextToken(&tbegin, &tend));
    switch ( ttype )
    {
    case tokenObjectBegin:
      ...
      break;
    case tokenArrayBegin:
      ...
      break;
    case tokenString:
      JSONCPP_STRING strval;
      tokenizer->decodeString(tbegin, tend, strval);
      ...
      break;
    case tokenNumber:
      double dval;
      tokenizer->decodeNumber(tbegin, tend, dval);
      ...
      break;
    case tokenComment:
      JSONCPP_STRING comments = JSONCPP_STRING(tbegin, tend);
      ...
      break;
    case Tokenizer::TokenType::tokenEndOfStream:
      // Nothing to parse apparently, you decide if you accept this as valid;-)
      bContinue = false;
      break;
    case Tokenizer::TokenType::tokenError:
      // An error occurred, the tokenizer might have additional error information
      JSONCPP_STRING errors = JSONCPP_STRING(tbegin, tend);
      ...
      bContinue = false;
      break;
    default:
      // Unexpected token type at the root presumably
      ...
      bContinue = false;
      break;
    }
  }
\endcode
*/
class JSON_API TokenizerBuilder : public Tokenizer::Factory {
public:
  // Note: We use a Json::Value so that we can add data-members to this class
  // without a major version bump.
  /** Configuration of this builder.
    These are case-sensitive.
    Available settings (case-sensitive):
    - `"collectComments": false or true`
      - true to collect comment and allow writing them
        back during serialization, false to discard comments.
        This parameter is ignored if allowComments is false.
    - `"allowComments": false or true`
      - true if comments are allowed.
    - `"strictRoot": false or true`
      - true if root must be either an array or an object value
    - `"allowDroppedNullPlaceholders": false or true`
      - true if dropped null placeholders are allowed. (See StreamWriterBuilder.)
    - `"allowNumericKeys": false or true`
      - true if numeric object keys are allowed.
    - `"allowSingleQuotes": false or true`
      - true if '' are allowed for strings (both keys and values)
    - `"stackLimit": integer`
      - Exceeding stackLimit (recursive depth of `readValue()`) will
        cause an exception.
      - This is a security issue (seg-faults caused by deeply nested JSON),
        so the default is low.
    - `"failIfExtra": false or true`
      - If true, `parse()` returns false when extra non-whitespace trails
        the JSON value in the input string.
    - `"rejectDupKeys": false or true`
      - If true, `parse()` returns false when a key is duplicated within an object.
    - `"allowSpecialFloats": false or true`
      - If true, special float values (NaNs and infinities) are allowed 
        and their values are lossfree restorable.

    You can examine 'settings_` yourself
    to see the defaults. You can also write and read them just like any
    JSON Value.
    \sa setDefaults()
    */
  Json::Value settings_;

  TokenizerBuilder();
  ~TokenizerBuilder() JSONCPP_OVERRIDE;

  Tokenizer* newTokenizer() const JSONCPP_OVERRIDE;

  /** \return true if 'settings' are legal and consistent;
   *   otherwise, indicate bad settings via 'invalid'.
   */
  bool validate(Json::Value* invalid) const;

  /** A simple way to update a specific setting.
   */
  Value& operator[](JSONCPP_STRING key);

  /** Called by ctor, but you can use this to reset settings_.
   * \pre 'settings' != NULL (but Json::null is fine)
   * \remark Defaults:
   * \snippet src/lib_json/json_reader.cpp ReaderBuilderDefaults
   */
  static void setDefaults(Json::Value* settings);
  /** Same as old Features::strictMode().
   * \pre 'settings' != NULL (but Json::null is fine)
   * \remark Defaults:
   * \snippet src/lib_json/json_reader.cpp ReaderBuilderStrictMode
   */
  static void strictMode(Json::Value* settings);
};

/** Interface for reading JSON from a char array.
 */
class JSON_API CharReader {
public:
  virtual ~CharReader() {}
  /** \brief Read a Value from a <a HREF="http://www.json.org">JSON</a>
   document.
   * The document must be a UTF-8 encoded string containing the document to read.
   *
   * \param beginDoc Pointer on the beginning of the UTF-8 encoded string of the
   document to read.
   * \param endDoc Pointer on the end of the UTF-8 encoded string of the
   document to read.
   *        Must be >= beginDoc.
   * \param root [out] Contains the root value of the document if it was
   *             successfully parsed.
   * \param errs [out] Formatted error messages (if not NULL)
   *        a user friendly string that lists errors in the parsed
   * document.
   * \return \c true if the document was successfully parsed, \c false if an
   error occurred.
   */
  virtual bool parse(
      char const* beginDoc, char const* endDoc,
      Value* root, JSONCPP_STRING* errs) = 0;

  class JSON_API Factory {
  public:
    virtual ~Factory() {}
    /** \brief Allocate a CharReader via operator new().
     * \throw std::exception if something goes wrong (e.g. invalid settings)
     */
    virtual CharReader* newCharReader() const = 0;
  };  // Factory
};  // Reader

/** \brief Build a CharReader implementation.

Usage:
\code
  using namespace Json;
  CharReaderBuilder builder;
  builder["collectComments"] = false;
  Value value;
  JSONCPP_STRING errs;
  bool ok = parseFromStream(builder, std::cin, &value, &errs);
\endcode
*/
class JSON_API CharReaderBuilder : public CharReader::Factory {
public:
  // Note: We use a Json::Value so that we can add data-members to this class
  // without a major version bump.
  /** Configuration of this builder.
    These are case-sensitive.
    Available settings (case-sensitive):
    - `"collectComments": false or true`
      - true to collect comment and allow writing them
        back during serialization, false to discard comments.
        This parameter is ignored if allowComments is false.
    - `"allowComments": false or true`
      - true if comments are allowed.
    - `"strictRoot": false or true`
      - true if root must be either an array or an object value
    - `"allowDroppedNullPlaceholders": false or true`
      - true if dropped null placeholders are allowed. (See StreamWriterBuilder.)
    - `"allowNumericKeys": false or true`
      - true if numeric object keys are allowed.
    - `"allowSingleQuotes": false or true`
      - true if '' are allowed for strings (both keys and values)
    - `"stackLimit": integer`
      - Exceeding stackLimit (recursive depth of `readValue()`) will
        cause an exception.
      - This is a security issue (seg-faults caused by deeply nested JSON),
        so the default is low.
    - `"failIfExtra": false or true`
      - If true, `parse()` returns false when extra non-whitespace trails
        the JSON value in the input string.
    - `"rejectDupKeys": false or true`
      - If true, `parse()` returns false when a key is duplicated within an object.
    - `"allowSpecialFloats": false or true`
      - If true, special float values (NaNs and infinities) are allowed 
        and their values are lossfree restorable.

    You can examine 'settings_` yourself
    to see the defaults. You can also write and read them just like any
    JSON Value.
    \sa setDefaults()
    */
  Json::Value settings_;

  CharReaderBuilder();
  ~CharReaderBuilder() JSONCPP_OVERRIDE;

  CharReader* newCharReader() const JSONCPP_OVERRIDE;

  /** \return true if 'settings' are legal and consistent;
   *   otherwise, indicate bad settings via 'invalid'.
   */
  bool validate(Json::Value* invalid) const;

  /** A simple way to update a specific setting.
   */
  Value& operator[](JSONCPP_STRING key);

  /** Called by ctor, but you can use this to reset settings_.
   * \pre 'settings' != NULL (but Json::null is fine)
   * \remark Defaults:
   * \snippet src/lib_json/json_reader.cpp ReaderBuilderDefaults
   */
  static void setDefaults(Json::Value* settings);
  /** Same as old Features::strictMode().
   * \pre 'settings' != NULL (but Json::null is fine)
   * \remark Defaults:
   * \snippet src/lib_json/json_reader.cpp ReaderBuilderStrictMode
   */
  static void strictMode(Json::Value* settings);
};

/** Consume entire stream and use its begin/end.
  * Someday we might have a real StreamReader, but for now this
  * is convenient.
  */
bool JSON_API parseFromStream(
    CharReader::Factory const&,
    JSONCPP_ISTREAM&,
    Value* root, std::string* errs);

/** \brief Read from 'sin' into 'root'.

 Always keep comments from the input JSON.

 This can be used to read a file into a particular sub-object.
 For example:
 \code
 Json::Value root;
 cin >> root["dir"]["file"];
 cout << root;
 \endcode
 Result:
 \verbatim
 {
 "dir": {
     "file": {
     // The input stream JSON would be nested here.
     }
 }
 }
 \endverbatim
 \throw std::exception on parse error.
 \see Json::operator<<()
*/
JSON_API JSONCPP_ISTREAM& operator>>(JSONCPP_ISTREAM&, Value&);

} // namespace Json

#pragma pack(pop)

#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)
#pragma warning(pop)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#endif // CPPTL_JSON_READER_H_INCLUDED
