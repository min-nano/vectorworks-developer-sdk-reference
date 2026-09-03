//
//	PayloadAbi.h
//
//	**殻（Vectorworks が読み込むプラグイン）と、本体（ペイロード）の間の唯一の約束事。**
//
//	【なぜ 2 つに割れているか】コンパイル済みプラグインは Vectorworks の起動時にしか
//	読み込まれず、読み込み済みのモジュールは差し替えられない。そこで**中身（プローブ）を
//	Vectorworks が知らない別の動的モジュールへ出し、殻が自分で読み込む**。すると中身の
//	入れ替えは「ファイルを置き換えて読み直す」だけになり、**Vectorworks の再起動が要らない**
//	（macOS で実測済み。Findings「プラグインモジュールの読み込みと入れ替え」）。
//
//	    Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）  … 起動時に 1 度きり
//	                                  │ dlopen / LoadLibrary
//	                                  ▼
//	                              本体（プローブ）                   … メニューを開くたびに読み直す
//
//	【なぜ C の ABI か】本体は**降ろして読み直す**ので、境界に C++ の型を置けない:
//
//	  * 例外を越えさせない（越えた先のモジュールが消えていれば巻き戻せない）。
//	  * std::string / std::vector を跨いで渡さない（**殻と本体は別々にビルドされ、別々に
//	    配られる**——アロケータや実装の一致に頼れない）。
//	  * 仮想関数テーブルを持つオブジェクトを殻へ残さない（降ろした瞬間に vtable が消える）。
//
//	【渡す構造体の寿命】**VwPayloadHost は本体がその場で写す。**殻がどこにそれを置いたか
//	（ローカルか、メンバか）は本体からは分からないので、ポインタを持ち続けてはならない。
//	殻の側も**降ろすまで生かす**（二重の歯止め。片方だけ古くても壊れないように）。
//	これを落とすと、本体が腐ったポインタから関数ポインタを読んで**スタックの番地へ分岐し、
//	Vectorworks ごと落ちる**——実際に落とした（Findings「プラグインモジュールの読み込みと
//	入れ替え」）。逆向き（本体 → 殻）の VwPayloadInfo / VwPayloadProbe / VwPayloadResult は
//	**殻が写す**ので、本体は次の呼び出しまで持っていればよい。
//
//	【返る文字列の寿命】ペイロードが返す `const char*` は、**次にペイロードを呼ぶまで**か
//	**降ろすまで**しか生きていない。殻は受け取ったその場で std::string へ写すこと。
//
//	【版が食い違ったら呼ばない】殻と本体は独立に配られるので、食い違いは実行時にしか
//	検出できない。`vw_payload_abi_version()` が殻の VW_PAYLOAD_ABI_VERSION と一致しない
//	ペイロードは**読み込んだだけで捨てる**（殻は「殻ごと入れ替えてください」と案内する）。
//	境界の形を変えたら必ず版を上げること。
//
//	【SDK を include しない】この 1 ファイルだけは SDK にもプラットフォームにも依存しない。
//	殻と本体の両方が include するので、依存を持ち込むと境界の意味が薄れる。CallBackPtr は
//	void* として渡す（実体は SDK の CallBackPtr）。
//
//	使う側:
//	  * 殻   … plugin/src/PayloadHost.h（読み込み・呼び出し・アンロード）
//	  * 本体 … plugin/src/payload/PayloadMain.cpp（下の関数を export する）
//

#pragma once

#include <cstddef>

// 境界の版。**形を変えたら上げる。**
//   1 … 検証用（describe / run のみ）
//   2 … プローブの一覧と実行を載せた（本番の形）
#define VW_PAYLOAD_ABI_VERSION 2u

// ペイロード側の export 指定。Windows は明示しないと DLL の外から見えない。
#if defined(_WIN32)
#	define VW_PAYLOAD_EXPORT extern "C" __declspec(dllexport)
#else
#	define VW_PAYLOAD_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C"
{
	// -----------------------------------------------------------------------
	// 殻がペイロードへ渡すもの。**ペイロードはこれ以外に殻を知らない。**
	struct VwPayloadHost
	{
		// sizeof(VwPayloadHost)。版が食い違ったときに「短い構造体を長いつもりで読む」
		// 事故を防ぐ（abiVersion と二重の歯止め）。
		unsigned int size;
		// 殻がコンパイルされた VW_PAYLOAD_ABI_VERSION。
		unsigned int abiVersion;

		// **SDK の CallBackPtr。** ペイロードはこれを GS_InitializeVCOM へ渡して、
		// 自分の側の gSDK / gCBP / gVWMM を埋める（それらは静的ライブラリが持つ
		// **モジュールごとの**グローバルなので、読み込んだだけでは空のまま）。
		void* callbacks;

		// ログの行き先。プローブが 1 行書くたびに呼ばれる（末尾の改行は含まない）。
		// **例外を投げてはならない**（境界を越えて戻る途中で巻き戻せない）。
		// logCtx は殻の持ち物で、**降ろすまで生きている**ことだけが約束（上記「寿命」）。
		void* logCtx;
		void (*log)(void* logCtx, const char* utf8Line);
	};

	// -----------------------------------------------------------------------
	// ペイロード自身の素性。「いま動いている本体はどのビルドか」を殻が言えるように。
	struct VwPayloadInfo
	{
		unsigned int size;
		const char* buildId; // **入れ替えの新旧を比べる鍵**（scripts/gather-probes.sh）
		const char* commit;	   // 短縮 sha
		const char* branch;	   //
		const char* buildTime; // ISO 8601（UTC）
		unsigned int probeCount;
	};

	// プローブ 1 件。素性（コードが名乗る）と出所（ビルドのときに決まる）を 1 つにまとめて
	// 渡す——殻は表示するだけなので、分けて持つ意味が無い。
	struct VwPayloadProbe
	{
		unsigned int size;
		const char* id;		 // 一意な slug（ディレクトリ名と一致）
		const char* title;	 // ピッカーの 1 行
		const char* summary; // 何を確かめるプローブか
		const char* pr;		 // PR 番号（main から取ったものは空文字）
		const char* commit;	 // 短縮 sha
		const char* branch;	 //
		const char* prTitle; // PR のタイトル（あれば）
	};

	// プローブ 1 件を走らせた結果。**ログ本文はここに入らない**——走っている間に
	// VwPayloadHost::log で 1 行ずつ流してあるので、殻はそれを溜めておく。
	struct VwPayloadResult
	{
		unsigned int size;
		int failed;			 // 0 = 成功 / 1 = 失敗か例外
		const char* outcome; // 「成功」「失敗: …」「例外で中断: …」
		const char* logPath; // ログファイル（開けなかったときは空文字）
		double seconds;		 // 所要
	};

	// -----------------------------------------------------------------------
	// ペイロードが export する関数の名前（dlsym / GetProcAddress で引く）。
	// 綴りを 1 か所に持つ——殻と本体で食い違うと「見つからない」としか出ない。
#define VW_PAYLOAD_SYM_ABI "vw_payload_abi_version"
#define VW_PAYLOAD_SYM_INIT "vw_payload_init"
#define VW_PAYLOAD_SYM_INFO "vw_payload_info"
#define VW_PAYLOAD_SYM_PROBE_AT "vw_payload_probe_at"
#define VW_PAYLOAD_SYM_RUN "vw_payload_run"
#define VW_PAYLOAD_SYM_SHUTDOWN "vw_payload_shutdown"

	// その型。
	using VwPayloadAbiVersionFn = unsigned int (*)();
	using VwPayloadInitFn = int (*)(const VwPayloadHost*);
	using VwPayloadInfoFn = int (*)(VwPayloadInfo*);
	using VwPayloadProbeAtFn = int (*)(unsigned int, VwPayloadProbe*);
	using VwPayloadRunFn = int (*)(const char*, VwPayloadResult*);
	using VwPayloadShutdownFn = void (*)();

	// -----------------------------------------------------------------------
	// 戻り値。**0 が成功**で、それ以外は理由を表す（例外は越えさせないので、
	// 失敗はすべてこの整数で返る）。
	enum VwPayloadStatus
	{
		kVwPayloadOk = 0,
		kVwPayloadErrAbi = 1, // 版か構造体の大きさが合わない
		kVwPayloadErrHost = 2, // 殻から渡されたものが足りない（callbacks が空 等）
		kVwPayloadErrVcom = 3,		// GS_InitializeVCOM に失敗した / gSDK が空のまま
		kVwPayloadErrNotInit = 4,	// init が済んでいない
		kVwPayloadErrUnknownId = 5, // 知らない id / 範囲外の index
		kVwPayloadErrException = 6, // 中で例外が出た（境界の手前で握り潰した）
	};
} // extern "C"
