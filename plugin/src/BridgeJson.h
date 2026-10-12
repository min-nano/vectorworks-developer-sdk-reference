//
//	BridgeJson.h
//
//	**最小の JSON。** SDK もプラットフォームも知らず、値の組み立て・書き出し・読み取りだけを
//	持つ。用途は 1 つ——**MCP ブリッジ**（BridgeSpool.h）が Claude 側の MCP サーバ
//	（plugin/mcp/vw-mcp-server.py）と交わす要求／応答の器である。
//
//	【出どころ】実プラグイン（vectorworks-plugin-import-ifc-homeskz の src/core/Json.{h,cpp}）を
//	ヘッダだけの形に写したもの。このリポジトリの「純粋な部分」はヘッダ 1 つ＋標準ライブラリで
//	書き、lint の unit-tests が `g++ <テスト 1 ファイル>` で確かめる決まりなので、.cpp を持たない。
//
//	【なぜ自前か】殻は SDK 同梱の nlohmann::json を使えるが、それでは単体テスト（SDK 抜き）で
//	受け渡しの作法を確かめられない。要るのは「素直な JSON を読み書きする」だけなので自前にする。
//
//	【決定性】オブジェクトのメンバは**挿入順のまま**保持して、その順で書き出す。
//
//	【外から来たものを読む】要求は**プラグインの外**（別プロセス）が書いたテキストである。
//	壊れた入力・悪意ある入力で異常終了しないことが要件なので:
//	  * 入れ子の深さに上限を設ける（深い配列で再帰させてスタックを溢れさせない）。
//	  * 失敗は例外ではなく戻り値と error 文字列で返す（境界を越えさせない）。
//
//	【数値】書き出しはロケールに依存させない（`std::locale::classic` を当てる）。
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vwprobe::bridge
{
	// JSON の値 1 つ。null / 真偽 / 数 / 文字列 / 配列 / オブジェクトのいずれか。
	class Json
	{
	public:
		enum class Kind
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object,
		};

		Json() = default;

		// --- 作る ----------------------------------------------------------
		static Json null();
		static Json boolean(bool value);
		static Json number(double value);
		static Json integer(long long value);
		static Json string(std::string value);
		static Json array();
		static Json object();

		// --- 見る ----------------------------------------------------------
		Kind kind() const
		{
			return fKind;
		}
		bool isNull() const
		{
			return fKind == Kind::Null;
		}
		bool isArray() const
		{
			return fKind == Kind::Array;
		}
		bool isObject() const
		{
			return fKind == Kind::Object;
		}

		// 型が違えば既定値を返す（呼ぶ側に型検査を分散させないための約束）。
		bool asBool(bool fallback = false) const;
		double asNumber(double fallback = 0.0) const;
		std::string asString(const std::string& fallback = std::string()) const;

		// 配列の要素（配列でなければ空）。
		const std::vector<Json>& items() const
		{
			return fItems;
		}

		// オブジェクトのメンバ（挿入順）。
		const std::vector<std::pair<std::string, Json>>& members() const
		{
			return fMembers;
		}

		// オブジェクトのメンバ 1 つ。**無ければ null を返す**（呼ぶ側が has() を挟まずに
		// 既定値つきの as*() へ繋げられる）。
		const Json& at(const std::string& key) const;
		bool has(const std::string& key) const;

		// --- 追加する ------------------------------------------------------
		// 配列へ 1 つ（配列でなければ何もしない）。
		void push(Json value);
		// オブジェクトへ 1 つ。**同じ鍵が既にあれば置き換える**（並びは最初に入れた位置の
		// まま。後から書き換えても順が動かないほうが読みやすい）。
		void set(std::string key, Json value);

		// --- 書き出す・読み取る --------------------------------------------
		// 1 行の JSON テキスト（整形しない。読むのは機械だけ）。
		std::string dump() const;

		// 読み取り。成功したら true。失敗したら out は変更せず error に理由が入る。
		static bool parse(const std::string& text, Json& out, std::string& error);

	private:
		Kind fKind = Kind::Null;
		bool fBool = false;
		double fNumber = 0.0;
		std::string fString;
		std::vector<Json> fItems;
		std::vector<std::pair<std::string, Json>> fMembers;
	};

	// JSON の文字列リテラル 1 つ分（引用符と傍線を含む）。
	std::string jsonQuote(const std::string& text);

	namespace json_detail
	{
		// 入れ子の上限。**外から来たテキストで再帰を深くさせない**ための歯止め
		// （[[[[[… と書いただけでスタックを溢れさせられるのを防ぐ）。ブリッジが運ぶのは
		// 素直な 2〜3 段の構造なので、64 段あれば実用上は上限に達しない。
		inline constexpr int kMaxDepth = 64;

		// 空のときに返す不変の値（at() が「無ければ null」を返すための実体）。
		inline const Json& NullValue()
		{
			static const Json value;
			return value;
		}

		// --- 書き出し ------------------------------------------------------

		inline void AppendEscaped(std::string& out, const std::string& text)
		{
			out += '"';
			for (const char raw : text)
			{
				const auto ch = static_cast<unsigned char>(raw);
				switch (ch)
				{
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				case '\b':
					out += "\\b";
					break;
				case '\f':
					out += "\\f";
					break;
				default:
					if (ch < 0x20)
					{
						// 制御文字は \u00XX。ここだけ 16 進で書く。
						static const char* const kHex = "0123456789abcdef";
						out += "\\u00";
						out += kHex[(ch >> 4) & 0x0F];
						out += kHex[ch & 0x0F];
					}
					else
					{
						// **UTF-8 はそのまま通す。** 日本語のレイヤ名・クラス名を \u へ
						// 展開する理由が無い（JSON として正しく、読むときも楽）。
						out += raw;
					}
					break;
				}
			}
			out += '"';
		}

		inline void AppendNumber(std::string& out, double value)
		{
			// 非有限は JSON で表せない。**null に置き換える**（例外にすると 1 つの異常値で
			// 応答全体が消える）。
			if (!std::isfinite(value))
			{
				out += "null";
				return;
			}
			// 整数で表せるものは整数として書く（件数・種別番号が 3.000000 と出ると読みにくい）。
			if (value == std::floor(value) && std::abs(value) < 1e15)
			{
				out += std::to_string(static_cast<long long>(value));
				return;
			}
			// **ロケールを固定する**（小数点がカンマになる環境で壊れた JSON を出力しない）。
			//
			// **短く書けるなら短く書く。** max_digits10（17 桁）で一律に書くと 0.0125 が
			// 0.012500000000000001 になり、読む側——人にとっても Claude にとっても——
			// 雑音でしかない。15 → 16 → 17 桁の順に試し、**読み戻して同じ値になった
			// 最初のもの**を採る（17 桁まで行けば必ず一致するので、値は失われない）。
			for (int digits = 15; digits <= std::numeric_limits<double>::max_digits10; ++digits)
			{
				std::ostringstream oss;
				oss.imbue(std::locale::classic());
				oss.precision(digits);
				oss << value;
				const std::string text = oss.str();

				std::istringstream iss(text);
				iss.imbue(std::locale::classic());
				double back = 0.0;
				iss >> back;
				if (back == value || digits == std::numeric_limits<double>::max_digits10)
				{
					out += text;
					return;
				}
			}
		}

		inline void AppendValue(std::string& out, const Json& value)
		{
			switch (value.kind())
			{
			case Json::Kind::Null:
				out += "null";
				return;
			case Json::Kind::Bool:
				out += value.asBool() ? "true" : "false";
				return;
			case Json::Kind::Number:
				AppendNumber(out, value.asNumber());
				return;
			case Json::Kind::String:
				AppendEscaped(out, value.asString());
				return;
			case Json::Kind::Array:
			{
				out += '[';
				bool first = true;
				for (const Json& item : value.items())
				{
					if (!first)
						out += ',';
					first = false;
					AppendValue(out, item);
				}
				out += ']';
				return;
			}
			case Json::Kind::Object:
			{
				out += '{';
				bool first = true;
				for (const auto& member : value.members())
				{
					if (!first)
						out += ',';
					first = false;
					AppendEscaped(out, member.first);
					out += ':';
					AppendValue(out, member.second);
				}
				out += '}';
				return;
			}
			}
		}

		// --- 読み取り ------------------------------------------------------

		// 再帰下降。fPos が次に読む位置で、失敗したら fError に理由を入れて false を返す。
		class Parser
		{
		public:
			explicit Parser(std::string_view text) : fText(text) {}

			bool run(Json& out)
			{
				skipSpace();
				if (!value(out, 0))
					return false;
				skipSpace();
				if (fPos != fText.size())
					return fail("末尾に余分な文字があります");
				return true;
			}

			const std::string& error() const
			{
				return fError;
			}

		private:
			// **参照ではなく view で持つ**（参照のメンバは clang-tidy が警告する。
			// 読むだけなので view で足りる）。
			std::string_view fText;
			std::size_t fPos = 0;
			std::string fError;

			bool fail(const std::string& why)
			{
				if (fError.empty())
					fError = why + "（" + std::to_string(fPos) + " 文字目）";
				return false;
			}

			bool atEnd() const
			{
				return fPos >= fText.size();
			}

			char peek() const
			{
				return fPos < fText.size() ? fText[fPos] : '\0';
			}

			void skipSpace()
			{
				while (fPos < fText.size())
				{
					const char ch = fText[fPos];
					if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
						++fPos;
					else
						break;
				}
			}

			bool literal(const char* word, Json made, Json& out)
			{
				const std::string text(word);
				if (fText.compare(fPos, text.size(), text) != 0)
					return fail("知らない綴りです");
				fPos += text.size();
				out = std::move(made);
				return true;
			}

			bool value(Json& out, int depth)
			{
				if (depth > kMaxDepth)
					return fail("入れ子が深すぎます");
				if (atEnd())
					return fail("値がありません");
				switch (peek())
				{
				case '{':
					return objectValue(out, depth);
				case '[':
					return arrayValue(out, depth);
				case '"':
				{
					std::string text;
					if (!stringValue(text))
						return false;
					out = Json::string(std::move(text));
					return true;
				}
				case 't':
					return literal("true", Json::boolean(true), out);
				case 'f':
					return literal("false", Json::boolean(false), out);
				case 'n':
					return literal("null", Json::null(), out);
				default:
					return numberValue(out);
				}
			}

			bool objectValue(Json& out, int depth)
			{
				++fPos; // '{'
				Json made = Json::object();
				skipSpace();
				if (peek() == '}')
				{
					++fPos;
					out = std::move(made);
					return true;
				}
				for (;;)
				{
					skipSpace();
					if (peek() != '"')
						return fail("鍵は文字列でなければなりません");
					std::string key;
					if (!stringValue(key))
						return false;
					skipSpace();
					if (peek() != ':')
						return fail("鍵の後に ':' がありません");
					++fPos;
					skipSpace();
					Json member;
					if (!value(member, depth + 1))
						return false;
					made.set(std::move(key), std::move(member));
					skipSpace();
					if (peek() == ',')
					{
						++fPos;
						continue;
					}
					if (peek() == '}')
					{
						++fPos;
						out = std::move(made);
						return true;
					}
					return fail("',' か '}' が必要です");
				}
			}

			bool arrayValue(Json& out, int depth)
			{
				++fPos; // '['
				Json made = Json::array();
				skipSpace();
				if (peek() == ']')
				{
					++fPos;
					out = std::move(made);
					return true;
				}
				for (;;)
				{
					skipSpace();
					Json item;
					if (!value(item, depth + 1))
						return false;
					made.push(std::move(item));
					skipSpace();
					if (peek() == ',')
					{
						++fPos;
						continue;
					}
					if (peek() == ']')
					{
						++fPos;
						out = std::move(made);
						return true;
					}
					return fail("',' か ']' が必要です");
				}
			}

			// \uXXXX 1 つ分を読む（読めたら code に符号位置を入れる）。
			bool hex4(unsigned int& code)
			{
				if (fPos + 4 > fText.size())
					return fail("\\u の後が足りません");
				code = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char ch = fText[fPos + static_cast<std::size_t>(i)];
					unsigned int digit = 0;
					if (ch >= '0' && ch <= '9')
						digit = static_cast<unsigned int>(ch - '0');
					else if (ch >= 'a' && ch <= 'f')
						digit = static_cast<unsigned int>(ch - 'a') + 10U;
					else if (ch >= 'A' && ch <= 'F')
						digit = static_cast<unsigned int>(ch - 'A') + 10U;
					else
						return fail("\\u の後は 16 進 4 桁です");
					code = (code << 4U) | digit;
				}
				fPos += 4;
				return true;
			}

			// 符号位置を UTF-8 で追加する。
			static void appendUtf8(std::string& out, unsigned int code)
			{
				if (code < 0x80U)
				{
					out += static_cast<char>(code);
				}
				else if (code < 0x800U)
				{
					out += static_cast<char>(0xC0U | (code >> 6U));
					out += static_cast<char>(0x80U | (code & 0x3FU));
				}
				else if (code < 0x10000U)
				{
					out += static_cast<char>(0xE0U | (code >> 12U));
					out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
					out += static_cast<char>(0x80U | (code & 0x3FU));
				}
				else
				{
					out += static_cast<char>(0xF0U | (code >> 18U));
					out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
					out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
					out += static_cast<char>(0x80U | (code & 0x3FU));
				}
			}

			bool stringValue(std::string& out)
			{
				++fPos; // '"'
				out.clear();
				while (!atEnd())
				{
					const char ch = fText[fPos];
					if (ch == '"')
					{
						++fPos;
						return true;
					}
					if (ch != '\\')
					{
						out += ch;
						++fPos;
						continue;
					}
					++fPos; // '\'
					if (atEnd())
						return fail("傍線の後が足りません");
					const char esc = fText[fPos++];
					switch (esc)
					{
					case '"':
						out += '"';
						break;
					case '\\':
						out += '\\';
						break;
					case '/':
						out += '/';
						break;
					case 'b':
						out += '\b';
						break;
					case 'f':
						out += '\f';
						break;
					case 'n':
						out += '\n';
						break;
					case 'r':
						out += '\r';
						break;
					case 't':
						out += '\t';
						break;
					case 'u':
					{
						unsigned int code = 0;
						if (!hex4(code))
							return false;
						// 上位サロゲートなら下位と組にして 1 文字にする（😀 等）。
						if (code >= 0xD800U && code <= 0xDBFFU && fPos + 1 < fText.size() &&
							fText[fPos] == '\\' && fText[fPos + 1] == 'u')
						{
							const std::size_t mark = fPos;
							fPos += 2;
							unsigned int low = 0;
							if (!hex4(low))
								return false;
							if (low >= 0xDC00U && low <= 0xDFFFU)
								code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
							else
								fPos = mark; // 組でなかった。単独の符号位置として扱う。
						}
						appendUtf8(out, code);
						break;
					}
					default:
						return fail("知らない傍線の綴りです");
					}
				}
				return fail("文字列が閉じていません");
			}

			bool numberValue(Json& out)
			{
				const std::size_t start = fPos;
				if (peek() == '-' || peek() == '+')
					++fPos;
				bool digits = false;
				while (!atEnd() && fText[fPos] >= '0' && fText[fPos] <= '9')
				{
					++fPos;
					digits = true;
				}
				if (!atEnd() && fText[fPos] == '.')
				{
					++fPos;
					while (!atEnd() && fText[fPos] >= '0' && fText[fPos] <= '9')
					{
						++fPos;
						digits = true;
					}
				}
				if (!digits)
					return fail("数として読めません");
				if (!atEnd() && (fText[fPos] == 'e' || fText[fPos] == 'E'))
				{
					++fPos;
					if (!atEnd() && (fText[fPos] == '-' || fText[fPos] == '+'))
						++fPos;
					bool expDigits = false;
					while (!atEnd() && fText[fPos] >= '0' && fText[fPos] <= '9')
					{
						++fPos;
						expDigits = true;
					}
					if (!expDigits)
						return fail("指数の桁がありません");
				}
				// **ロケールを固定して読む**（書き出しと対。core/Json.h「数値」）。
				std::istringstream iss(std::string(fText.substr(start, fPos - start)));
				iss.imbue(std::locale::classic());
				double value = 0.0;
				iss >> value;
				if (iss.fail())
					return fail("数として読めません");
				out = Json::number(value);
				return true;
			}
		};
	} // namespace json_detail

	// -----------------------------------------------------------------------
	inline Json Json::null()
	{
		return {};
	}

	inline Json Json::boolean(bool value)
	{
		Json made;
		made.fKind = Kind::Bool;
		made.fBool = value;
		return made;
	}

	inline Json Json::number(double value)
	{
		Json made;
		made.fKind = Kind::Number;
		made.fNumber = value;
		return made;
	}

	inline Json Json::integer(long long value)
	{
		return number(static_cast<double>(value));
	}

	inline Json Json::string(std::string value)
	{
		Json made;
		made.fKind = Kind::String;
		made.fString = std::move(value);
		return made;
	}

	inline Json Json::array()
	{
		Json made;
		made.fKind = Kind::Array;
		return made;
	}

	inline Json Json::object()
	{
		Json made;
		made.fKind = Kind::Object;
		return made;
	}

	inline bool Json::asBool(bool fallback) const
	{
		return fKind == Kind::Bool ? fBool : fallback;
	}

	inline double Json::asNumber(double fallback) const
	{
		return fKind == Kind::Number ? fNumber : fallback;
	}

	inline std::string Json::asString(const std::string& fallback) const
	{
		return fKind == Kind::String ? fString : fallback;
	}

	inline const Json& Json::at(const std::string& key) const
	{
		for (const auto& member : fMembers)
			if (member.first == key)
				return member.second;
		return json_detail::NullValue();
	}

	inline bool Json::has(const std::string& key) const
	{
		return std::any_of(fMembers.begin(), fMembers.end(),
						   [&key](const std::pair<std::string, Json>& member)
						   { return member.first == key; });
	}

	inline void Json::push(Json value)
	{
		if (fKind != Kind::Array)
			return;
		fItems.push_back(std::move(value));
	}

	inline void Json::set(std::string key, Json value)
	{
		if (fKind != Kind::Object)
			return;
		for (auto& member : fMembers)
		{
			if (member.first == key)
			{
				member.second = std::move(value);
				return;
			}
		}
		fMembers.emplace_back(std::move(key), std::move(value));
	}

	inline std::string Json::dump() const
	{
		std::string out;
		json_detail::AppendValue(out, *this);
		return out;
	}

	inline bool Json::parse(const std::string& text, Json& out, std::string& error)
	{
		error.clear();
		json_detail::Parser parser(text);
		Json made;
		if (!parser.run(made))
		{
			error = parser.error();
			if (error.empty())
				error = "JSON として読めません";
			return false;
		}
		out = std::move(made);
		return true;
	}

	inline std::string jsonQuote(const std::string& text)
	{
		std::string out;
		json_detail::AppendEscaped(out, text);
		return out;
	}
} // namespace vwprobe::bridge
