//
//	PayloadHost.h
//
//	**殻の側**。外部モジュール（ペイロード）を自分で読み込み、C ABI（PayloadAbi.h）で
//	呼び、降ろすところまで。Vectorworks はこのモジュールの存在を知らない——だから
//	**Vectorworks を再起動しなくても読み直せる**、というのがこの仕組みの狙い。
//
//	【いまの位置づけ】これは検証（issue #15）のための足場である。ここで
//	「読める・SDK が使える・降ろせる・置き換えて読み直せる」の 4 つが実機で確かめられたら、
//	プローブ本体をペイロードへ移す改修へ進む。確かめる筋書きは
//	probes/runtime/hot-reload/probe.cpp。
//
//	【降ろすときの約束】アンロードは**そのモジュールのコードが 1 つもスタックに
//	乗っていないとき**にしか行わない。加えて、ペイロードが返した const char*・
//	ペイロードが作ったオブジェクトを、降ろした後に触らない（消えている）。
//	この 2 つを守れる形にするために、境界は C で、返る文字列はその場で std::string へ写す。
//
//	【SDK を include しない】この2ファイル（.h/.cpp）は SDK に依存しない。パスの組み立て
//	（下の payload 名前空間）は純粋な関数として切ってあり、plugin/tests から SDK 抜きで
//	確かめられる（自動アップデートの UpdateParse.h と同じ作法）。
//

#pragma once

#include "PayloadAbi.h"

#include <string>

namespace vwprobe
{
	// -----------------------------------------------------------------------
	// 純粋な部分（パスの組み立て）。**プラットフォーム依存の呼び出しを含まない**ので、
	// そのまま単体テストできる（plugin/tests/UpdateParseTests.cpp）。
	namespace payload
	{
		// 配られるペイロードのファイル名。拡張子を .vwpayload にしてあるのは、
		// **Vectorworks にプラグインとして拾わせないため**（.vlb / .vwlibrary だと
		// Plug-Ins フォルダの走査に引っかかる）。
		inline std::string FileName(const std::string& variant)
		{
			return "VwSdkProbesPayload-" + variant + ".vwpayload";
		}

		// macOS: .../<name>.vwlibrary/Contents/MacOS/<name>
		//     → .../<name>.vwlibrary/Contents/Resources/<fileName>
		// （同梱スクリプトと同じ置き場所。UpdateParse.h の MacScriptPathFromBinary と対）
		inline std::string MacPayloadPathFromBinary(const std::string& binaryPath,
													const std::string& fileName)
		{
			const std::string marker = "/Contents/MacOS/";
			const std::string::size_type at = binaryPath.rfind(marker);
			if (at == std::string::npos)
				return "";
			return binaryPath.substr(0, at) + "/Contents/Resources/" + fileName;
		}

		// Windows: .../<Plug-Ins>/VwSdkProbes.vlb → .../<Plug-Ins>/<fileName>
		inline std::string WinPayloadPathFromModule(const std::string& modulePath,
													const std::string& fileName)
		{
			const std::string::size_type slash = modulePath.find_last_of("\\/");
			if (slash == std::string::npos)
				return "";
			return modulePath.substr(0, slash + 1) + fileName;
		}

		// 読み込むのは**同梱物そのものではなく、一時ディレクトリへ写した複製**。
		// 理由は 2 つ:
		//   * Windows は**読み込み中の DLL を消せない・置き換えられない**ので、世代ごとに
		//     別のファイルとして置くしかない（実機での差し替えもこの形になる）。
		//   * 同じパスを再利用すると、OS のキャッシュ（mac の dyld・署名の検証結果）が
		//     効いて「置き換えたのに古いままだった」を見逃しうる。
		// tag には世代を区別できる文字列（変種名＋通し番号など）を渡す。
		inline std::string TempCopyPath(const std::string& tempDir, const std::string& tag,
										const std::string& fileName, char separator)
		{
			std::string dir = tempDir;
			if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
				dir.pop_back();
			return dir + separator + "VwSdkProbes-" + tag + "-" + fileName;
		}
	} // namespace payload

	// -----------------------------------------------------------------------
	// 読み込んだペイロード 1 つ。**デストラクタでは降ろさない**——降ろす（close）のは
	// 明示的な操作で、失敗の理由を呼び出し側へ返す必要があるため。降ろし忘れたまま
	// 捨てた場合はモジュールが残るだけで、壊れはしない。
	class PayloadModule
	{
	public:
		PayloadModule() = default;
		~PayloadModule();

		PayloadModule(const PayloadModule&) = delete;
		PayloadModule& operator=(const PayloadModule&) = delete;

		// 読み込む。失敗したら false を返し、error に OS の言い分（dlerror / エラーコード）を入れる。
		bool open(const std::string& path, std::string& error);

		// 降ろす。**呼ぶ前に、このモジュールのコードがスタックに無いことを確かめること。**
		bool close(std::string& error);

		bool isOpen() const
		{
			return fHandle != nullptr;
		}
		const std::string& path() const
		{
			return fPath;
		}
		// 読み込んだ実体の番地（同じファイルを読み直したときに別物かを言うため）。
		const void* handle() const
		{
			return fHandle;
		}

		// export された関数を引く。無ければ nullptr。
		void* symbol(const char* name) const;

	private:
		void* fHandle = nullptr;
		std::string fPath;
	};

	// -----------------------------------------------------------------------
	// プラットフォーム依存の小物（実装は PayloadHost.cpp）。

	// 自分（Vectorworks が読み込んだ殻）のバイナリの絶対パス。
	std::string OwnModulePath();

	// 同梱されているペイロード（変種 variant）の絶対パス。見つからなければ空。
	std::string BundledPayloadPath(const std::string& variant);

	// 一時ディレクトリと、このプラットフォームのパス区切り。
	std::string TempDirectory();
	char PathSeparator();

	// ファイルを複製する（上書き）。失敗したら false ＋ 理由。
	bool CopyFileTo(const std::string& from, const std::string& to, std::string& error);

	// ファイルを消す。失敗したら false ＋ 理由（**Windows で「読み込み中だから消せない」
	// を確かめるのにも使う**ので、失敗は必ずしも異常ではない）。
	bool RemoveFileAt(const std::string& path, std::string& error);

	// そのパスのモジュールが**まだプロセスに残っているか**（降ろせたかの確認）。
	// mac は dlopen(RTLD_NOLOAD)、Windows は GetModuleHandleW で見る。
	bool IsModuleStillLoaded(const std::string& path);
} // namespace vwprobe
