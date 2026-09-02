//
//	Probe.h
//
//	**調査コード（プローブ）が使う唯一のヘッダ。** 1 つのプローブは
//	`probes/runtime/<slug>/probe.cpp` 1 ファイルで完結し、次の形で書く:
//
//	    #include "Probe.h"
//
//	    VW_PROBE("layer-order", "レイヤの重ね順を実測する",
//	             "レイヤを 3 枚作り、並べ替えた結果を読み戻す")
//	    {
//	        probe.log("はじめ");
//	        MCObjectHandle layer = gSDK->GetNamedLayer("試験");
//	        if (layer == nullptr)
//	            probe.fail("レイヤを作れなかった");
//	    }
//
//	`VW_PROBE` が展開するのは「本体関数 1 つ＋自己登録する静的オブジェクト 1 つ」だけで、
//	どちらも**内部リンケージ**（static）。つまり**別々の PR が書いた別々のファイルは、
//	同じ名前を使っていても衝突しない**——これが「1 つのプラグインに複数の PR のコマンドを
//	同居させる」ための土台になっている（並べ方・集め方は plugin/README.md）。
//
//	結果は戻り値ではなく `probe`（Report&）へ書く。**ログは 1 行書くたびにファイルへも
//	流す**ので、プローブが VectorWorks ごと落としても、そこまでの行は残る（落とし方まで
//	含めて知見になる調査があるため。書き出し先は Report::logPath()）。
//
//	【SDK 依存】このヘッダは VectorworksSDK.h を include する（TXString を直接 log できる
//	ようにするため）。プローブ側は "Probe.h" だけを include すれば SDK も一緒に入る。
//

#pragma once

#include "PluginPrefix.h"

#include <string>
#include <vector>

namespace vwprobe
{
	// -----------------------------------------------------------------------
	// 調査結果の書き出し口。プローブ本体は `probe` という名前でこれを受け取る。
	class Report
	{
	public:
		Report() = default;
		~Report();

		Report(const Report&) = delete;
		Report& operator=(const Report&) = delete;

		// 1 行書く（末尾の改行は不要）。**呼ぶたびにログファイルへも書いて flush する**
		// ——落ちても直前までが残るように。
		void log(const std::string& line);
		// TXString をそのまま渡せる口（SDK から読み戻した値をログに出す用）。
		// TXString の operator const char*() は UTF-8 を返す。
		void log(const TXString& line);

		// 「調べたいことは確かめられなかった」と記録する。最初の 1 件を結果の見出しに
		// 出す（2 件目以降も本文には残る）。例外を投げるより、**続きも動かして情報を
		// 増やす**ほうが調査としては有益なので、fail は処理を止めない。
		void fail(const std::string& why);

		bool failed() const
		{
			return fFailed;
		}
		const std::string& failure() const
		{
			return fFailure;
		}
		// ここまでに書かれたログの全文（結果ダイアログのテキスト欄に出す）。
		const std::string& text() const
		{
			return fText;
		}
		// ログの実ファイル。開けなかったときは空。
		const std::string& logPath() const
		{
			return fLogPath;
		}

		// ログファイルを開く（プローブを走らせる直前に 1 度だけ呼ぶ）。開けなくても
		// 黙って続ける——ログはメモリにも溜まっているので、ダイアログは変わらず読める。
		void openLog(const std::string& path);

	private:
		std::string fText;
		std::string fFailure;
		std::string fLogPath;
		void* fFile = nullptr; // std::FILE*（このヘッダに <cstdio> を持ち込まないため）
		bool fFailed = false;
	};

	// プローブ本体の型。
	using ProbeFn = void (*)(Report&);

	// -----------------------------------------------------------------------
	// 登録済みプローブ 1 件（コード側が名乗る素性）。
	struct Probe
	{
		std::string id; // 一意な slug。**ディレクトリ名と一致させる**（集約時に検証）
		std::string title;	 // ピッカーに出る 1 行
		std::string summary; // 何を確かめるプローブか（結果の見出しに出す）
		ProbeFn run = nullptr;
	};

	// VW_PROBE が作る静的オブジェクト。コンストラクタで自分を登録する。
	class Registrar
	{
	public:
		Registrar(const char* id, const char* title, const char* summary, ProbeFn run);
	};

	// -----------------------------------------------------------------------
	// プローブの「出所」。**コードには書かない**——どの PR のどのコミットから取り込んだ
	// かはビルドのときに決まるので、集約スクリプト（scripts/gather-probes.sh）が
	// 生成する ProbeProvenance.cpp が持つ。ローカルビルドでは commit="local"。
	struct Provenance
	{
		std::string id;
		std::string pr;		// PR 番号（main から取ったものは空）
		std::string commit; // 短縮 sha
		std::string branch;
		std::string title; // PR のタイトル（あれば）
	};

	// 生成された表を登録する（生成ファイルの静的オブジェクトが 1 度だけ呼ぶ）。
	class ProvenanceRegistrar
	{
	public:
		explicit ProvenanceRegistrar(const std::vector<Provenance>& entries);
	};

	// -----------------------------------------------------------------------
	// 登録されたプローブの一覧（**id 昇順**。列挙順に依存しない決定的な並び）。
	const std::vector<Probe>& probes();

	// id に対応する出所。無ければ nullptr（ローカルで足しただけのプローブ）。
	const Provenance* provenanceOf(const std::string& id);

	// ログの既定の書き出し先（一時ディレクトリ/VwSdkProbes-<id>.log）。環境変数
	// VW_PROBE_LOG にパスを入れると、そこへ差し替えられる。
	std::string defaultLogPath(const std::string& id);
} // namespace vwprobe

// ---------------------------------------------------------------------------
// プローブ 1 件を定義して登録するマクロ。**1 ファイルに 1 つだけ**書く
// （展開する名前が固定なので、2 つ書くと同じファイル内で衝突する。これは意図した
//  制約で、「1 プローブ 1 ディレクトリ」という集約の単位と一致する）。
//
// 本体は `probe`（vwprobe::Report&）を受け取る。使い方はこのファイル冒頭の例を参照。
#define VW_PROBE(id_, title_, summary_)                                                            \
	static void VwProbeBody(::vwprobe::Report& probe);                                             \
	static const ::vwprobe::Registrar sVwProbeRegistrar(id_, title_, summary_, &VwProbeBody);      \
	static void VwProbeBody(::vwprobe::Report& probe)
