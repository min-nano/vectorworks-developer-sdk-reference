//
//	PayloadHost.h
//
//	**殻の側。** 本体（ペイロード）を自分で読み込み、C の ABI（PayloadAbi.h）で呼び、
//	降ろすところまで。Vectorworks はこのモジュールの存在を知らない——だから**入れ替えに
//	Vectorworks の再起動が要らない**（macOS で実測済み。Findings「プラグインモジュールの
//	読み込みと入れ替え」）。
//
//	【いつ読み、いつ降ろすか】**メニューコマンドが走るたびに読んで、終わったら降ろす。**
//	読み込みは 0.3〜0.4 秒（本体が SDK を丸ごと抱えているため）で、この道具の使い方には
//	十分に軽い。この作りにすると:
//
//	  * 入れ替えが**自動で効く**。新しい本体を置けば、次にメニューを開いたときに動く
//	    （「読み直す」を押させる必要が無い）。
//	  * 読み込んだままの状態が残らない。降ろすときにコードがスタックに無いことが
//	    自明になり、寿命の危うさが消える。
//
//	【必ず複製してから読む】同梱のファイルを直接は読まない。世代ごとに一時ディレクトリへ
//	写して、その複製を読む。理由は 2 つ:
//	  * **Windows は読み込み中の DLL を消せない・置き換えられない。** 直接読むと、
//	    Vectorworks を動かしたまま入れ替えられなくなる（＝この仕組みの意味が無くなる）。
//	  * 同じパスを使い回すと、OS のキャッシュで「置き換えたのに古いまま」を見逃しうる。
//
//	【どの本体を読むか】**群ごとに 1 本**ある（main のプローブが 1 本、PR のプローブが
//	PR ごとに 1 本）。殻はメニューを開いた時点ではどれも読み込まず、カタログ
//	（plugin/src/PayloadCatalog.h）で一覧を出し、**選ばれた群の 1 本だけ**を読む。
//	1 つの群が壊れていても、他の群は選べる。
//
//	【SDK に依存しない】この 2 ファイルは SDK の型を使わない（プリコンパイルヘッダ経由で
//	宣言は入るが触らない）。パスの組み立ては純粋な関数に切ってあり、plugin/tests から
//	SDK 抜きで確かめられる（自動アップデートの UpdateParse.h と同じ作法）。
//

#pragma once

#include "PayloadAbi.h"

#include <string>
#include <vector>

namespace vwprobe
{
	// -----------------------------------------------------------------------
	// 純粋な部分（パスの組み立て）。**プラットフォーム依存の呼び出しを含まない**ので、
	// そのまま単体テストできる（plugin/tests/PayloadPathTests.cpp）。
	namespace payload
	{
		// 配られる本体のファイル名。**群（main / PR ごと）に 1 本**あり、名前に群が入る
		// （plugin/CMakeLists.txt の vw_add_payload と対）。拡張子を .vwpayload にして
		// あるのは、**Vectorworks にプラグインとして拾わせないため**（.vlb / .vwlibrary
		// だと Plug-Ins フォルダの走査に引っかかる）。
		inline std::string FileNameFor(const std::string& group)
		{
			return "VwSdkProbesPayload-" + group + ".vwpayload";
		}

		// 本体をどれも読み込まずに「何がどこに入っているか」を知るための索引。
		// **殻はまずこれを読み、選ばれた群の本体だけを読み込む**（plugin/src/ProbeMenu.cpp。
		// 中身の形と読み手は plugin/src/PayloadCatalog.h）。入れ替えスクリプト
		// （plugin/scripts/vw-probes-update.*）も「いま入っているビルド」をここから読む
		// ——プラグインを起動せずに分かる必要があるため。
		inline std::string CatalogFileName()
		{
			return "VwSdkProbes.probes.txt";
		}

		// macOS: .../<Plug-Ins>/VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes
		//     → .../<Plug-Ins>/<fileName>
		//
		// **バンドルの中には置かない。** mac のバンドルは署名がリソースまで封をするので、
		// Contents/Resources のファイルを差し替えると署名が壊れる（次の起動で読み込めなく
		// なりうる）。隣に置けば、本体を何度置き換えても殻の署名に触れない。
		inline std::string MacPayloadPathFromBinary(const std::string& binaryPath,
													const std::string& fileName)
		{
			const std::string::size_type at = binaryPath.rfind("/Contents/MacOS/");
			if (at == std::string::npos)
				return "";
			const std::string bundle = binaryPath.substr(0, at);
			const std::string::size_type slash = bundle.rfind('/');
			if (slash == std::string::npos)
				return "";
			return bundle.substr(0, slash + 1) + fileName;
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

		// 読み込むのは**同梱物そのものではなく一時ディレクトリへ写した複製**（上記）。
		// tag には世代を区別できる文字列を渡す（同じ名前を使い回さないことが肝）。
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
	// 読み込んだモジュール 1 つ（薄い包み）。**デストラクタでは降ろさない**——降ろす
	// （close）のは明示的な操作で、失敗の理由を呼び出し側へ返す必要があるため。
	class PayloadModule
	{
	public:
		PayloadModule() = default;
		~PayloadModule();

		PayloadModule(const PayloadModule&) = delete;
		PayloadModule& operator=(const PayloadModule&) = delete;

		// 読み込む。失敗したら false ＋ OS の言い分（dlerror / エラーコード）。
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

		// export された関数を引く。無ければ nullptr。
		void* symbol(const char* name) const;

	private:
		void* fHandle = nullptr;
		std::string fPath;
	};

	// -----------------------------------------------------------------------
	// 殻が見るプローブ 1 件。**ペイロードから写した値**（向こうの const char* は次の
	// 呼び出しまでしか生きていないので、受け取った時点で写す。PayloadAbi.h）。
	struct PayloadProbeInfo
	{
		std::string id;
		std::string title;
		std::string summary;
		std::string pr; // PR 番号（main 由来は空）
		std::string commit;
		std::string branch;
		std::string prTitle;
	};

	// -----------------------------------------------------------------------
	// **本体との付き合い 1 回ぶん。** 複製 → 読み込み → init → 一覧の取得までを load が
	// 行い、unload が降ろして複製を片付ける。使い方は plugin/src/ProbeMenu.cpp。
	class Payload
	{
	public:
		Payload() = default;
		~Payload();

		Payload(const Payload&) = delete;
		Payload& operator=(const Payload&) = delete;

		// 読み込んで使える状態にする。**どの本体を読むかは呼び出し側が決める**
		// （path は殻の隣に置かれた .vwpayload。群はカタログが教える）。
		// logCtx/log はプローブのログの受け口（**例外を投げてはならない**）。
		// 失敗したら false ＋ 人に見せる理由。
		bool load(const std::string& path, void* callbacks, void* logCtx,
				  void (*log)(void*, const char*), std::string& error);

		// 降ろして複製を消す（何度呼んでもよい）。
		void unload();

		bool isLoaded() const
		{
			return fLoaded;
		}

		// 読み込んだ本体の素性。
		const std::string& buildId() const
		{
			return fBuildId;
		}
		const std::string& commit() const
		{
			return fCommit;
		}
		const std::string& branch() const
		{
			return fBranch;
		}
		const std::string& buildTime() const
		{
			return fBuildTime;
		}
		// 同梱物の在り処（見つからなかったときの案内に使う）。
		const std::string& sourcePath() const
		{
			return fSourcePath;
		}

		const std::vector<PayloadProbeInfo>& probes() const
		{
			return fProbes;
		}

		// プローブ 1 件を走らせる。ログは load で渡した受け口へ 1 行ずつ流れる。
		// 走らせられなかったときだけ false（プローブ自身の失敗は outcome に入る）。
		bool run(const std::string& id, std::string& outcome, std::string& logPath, double& seconds,
				 std::string& error);

	private:
		// **本体へ渡した VwPayloadHost の実体。** load のローカルにしてはならない——
		// 本体はこのポインタを持ち続けてよい約束（PayloadAbi.h の「寿命」）なので、
		// **降ろすまで生かす**必要がある。ここに置くのがその保証。
		VwPayloadHost fHost{};
		PayloadModule fModule;
		std::string fTempPath;
		std::string fSourcePath;
		std::string fBuildId;
		std::string fCommit;
		std::string fBranch;
		std::string fBuildTime;
		std::vector<PayloadProbeInfo> fProbes;
		VwPayloadRunFn fRunFn = nullptr;
		VwPayloadShutdownFn fShutdownFn = nullptr;
		bool fLoaded = false;
	};

	// -----------------------------------------------------------------------
	// プラットフォーム依存の小物（実装は PayloadHost.cpp）。

	// 自分（Vectorworks が読み込んだ殻）のバイナリの絶対パス。
	std::string OwnModulePath();

	// 殻の隣に置かれたファイルの絶対パス（本体・カタログ）。見つからなければ空。
	// **本体もカタログも殻の隣**にある（バンドルの中ではない。上記）。
	std::string SiblingFilePath(const std::string& fileName);

	// 一時ディレクトリと、このプラットフォームのパス区切り。
	std::string TempDirectory();
	char PathSeparator();

	// テキストファイルを丸ごと読む（カタログ）。失敗したら false ＋ 理由。
	bool ReadTextFile(const std::string& path, std::string& text, std::string& error);

	// そのパスにファイルがあるか。**群の本体が配られているか**の確認に使う
	// （ビルドできなかった群は .vwpayload が配られない。plugin/cmake/ProbeCatalog.cmake）。
	bool FileExists(const std::string& path);

	// ファイルの複製と削除（失敗したら false ＋ 理由）。
	bool CopyFileTo(const std::string& from, const std::string& to, std::string& error);
	bool RemoveFileAt(const std::string& path, std::string& error);

	// そのパスのモジュールが**まだプロセスに残っているか**（降ろせたかの確認）。
	bool IsModuleStillLoaded(const std::string& path);
} // namespace vwprobe
