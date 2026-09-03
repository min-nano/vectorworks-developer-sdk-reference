//
//	PayloadPathTests.cpp
//
//	外部モジュール（ペイロード）の**パスの組み立て**の単体テスト
//	（plugin/src/PayloadHost.h の vwprobe::payload 名前空間）。
//
//	【なぜこれだけテストがあるか】UpdateParseTests.cpp と同じ理由。ここは SDK にも
//	プラットフォームにも依存しない文字列処理で、**壊れても静かに壊れる**——同梱物を
//	見つけられなければ「ペイロードが無い」としか出ず、置き場所が 1 文字ずれただけなのか
//	ビルドに入っていないのかを実機で切り分ける羽目になる。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/PayloadPathTests.cpp -o /tmp/t && /tmp/t
//

#include "PayloadHost.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vwprobe::payload;

namespace
{
	int gFailures = 0;

	void checkEq(const std::string& actual, const std::string& expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: [%s]\n  実際: [%s]\n", what, expected.c_str(),
						actual.c_str());
			++gFailures;
		}
	}
} // namespace

int main()
{
	// --- ファイル名 ---------------------------------------------------------
	// 拡張子は .vwpayload（Vectorworks にプラグインとして拾わせない）。
	checkEq(FileName(), "VwSdkProbesPayload.vwpayload", "本体のファイル名");
	checkEq(BuildInfoFileName(), "VwSdkProbesPayload.build-info.txt", "素性の控えのファイル名");

	// --- macOS: **バンドルの中ではなく隣** -----------------------------------
	// 中に置くと、本体を差し替えるたびに殻の署名（リソースまで封をする）が壊れる。
	checkEq(MacPayloadPathFromBinary("/U/Plug-Ins/VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes",
									 "P.vwpayload"),
			"/U/Plug-Ins/P.vwpayload", "mac: 本体はバンドルの隣");
	checkEq(MacPayloadPathFromBinary("/どこでもない/VwSdkProbes", "P.vwpayload"), "",
			"mac: 形が違えば空を返す");

	// --- Windows: モジュールの隣 --------------------------------------------
	checkEq(WinPayloadPathFromModule("C:\\P\\VwSdkProbes.vlb", "P.vwpayload"), "C:\\P\\P.vwpayload",
			"win: 同梱ペイロードの位置");
	checkEq(WinPayloadPathFromModule("/U/P/VwSdkProbes.vlb", "P.vwpayload"), "/U/P/P.vwpayload",
			"win: 区切りが / でも通す");
	checkEq(WinPayloadPathFromModule("VwSdkProbes.vlb", "P.vwpayload"), "",
			"win: フォルダが分からなければ空を返す");

	// --- 一時ディレクトリへの複製先 -----------------------------------------
	// **世代ごとに違う名前になる**ことが肝（Windows は読み込み中のファイルを置き換え
	// られないので、同じ名前を使い回すと 2 世代目が読めない）。
	checkEq(TempCopyPath("/tmp", "1", "P.vwpayload", '/'), "/tmp/VwSdkProbes-1-P.vwpayload",
			"mac: 複製先");
	checkEq(TempCopyPath("C:\\Temp\\", "2", "P.vwpayload", '\\'),
			"C:\\Temp\\VwSdkProbes-2-P.vwpayload", "win: 末尾の区切りは重ねない");
	{
		const std::string a = TempCopyPath("/tmp", "1", "P.vwpayload", '/');
		const std::string b = TempCopyPath("/tmp", "2", "P.vwpayload", '/');
		if (a == b)
		{
			std::printf("FAIL: 世代が違えば複製先も違う\n");
			++gFailures;
		}
	}

	if (gFailures > 0)
	{
		std::printf("\n%d 件失敗しました。\n", gFailures);
		return EXIT_FAILURE;
	}
	std::printf("PayloadPath: すべて通りました。\n");
	return EXIT_SUCCESS;
}
