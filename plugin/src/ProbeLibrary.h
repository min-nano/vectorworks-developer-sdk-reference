//
//	ProbeLibrary.h
//
//	**「呼ばれる側」の道具。** このプラグインが登録する**プラグインライブラリルーチン**
//	（VectorScript から見れば「関数ライブラリ」の拡張機能）。
//
//	なぜ殻に置くか
//	--------------
//	「あるプラグインから、別のプラグインの機能を名前で呼べるか」を実機で確かめるには
//	**呼ばれる側が要る**（[issue #217]）。呼ぶ側はプローブ（本体＝`.vwpayload` の中）で
//	書けるが、**呼ばれる側は Vectorworks に登録されていなければならない**——登録は
//	`plugin_module_main` で行い、VW はそこで受け取った番地を握り続けるので、
//	**入れ替わる本体には置けない**（[Findings「プラグインモジュール」](../../Findings/Plug-in%20Modules.md)）。
//	だから殻（起動時に 1 度だけ読まれる側）に置く。
//
//	これで実機では「**本体モジュールのプローブ → VW の名前解決 → 殻が登録した拡張機能**」
//	という、**モジュールを跨いだ呼び出し**がそのまま測れる。呼ぶ側と呼ばれる側は別々の
//	`.dylib` / `.dll` で、互いのシンボルを一切知らない。
//
//	登録する 3 本（どれも中身は自明な変換だけ。測りたいのは**引数と結果の受け渡し**）:
//
//	| 名前 | 署名 | 確かめること |
//	| --- | --- | --- |
//	| `VwSdkProbes_Echo`  | `(input: STRING): STRING`                             | 文字列を渡して文字列を受け取れるか |
//	| `VwSdkProbes_Sum`   | `(a: LONG, b: LONG): LONG`                            | 数値を複数渡して数値を受け取れるか |
//	| `VwSdkProbes_Out`   | `(input: STRING, VAR outText: STRING, VAR outCount: LONG): BOOLEAN` | **VAR（出力）引数**で返せるか |
//
//	scope は `kVLIBScopeUniversal`——**VectorScript からも SDK からも**呼べる
//	（`Kernel/API/MiniCadCallBacks.h`。`kVLIBScopeVSOnly` にすると
//	`ISDK::CallPluginLibrary` から呼べなくなる）。
//

#pragma once

#include "VectorworksSDK.h"

namespace vwprobe
{
	using namespace VWFC::PluginSupport;

	// ---------------------------------------------------------------------
	// 呼ぶ側が `ISDK::CallPluginLibrary` / `vs.*` へ渡す名前。**プローブ側はこの
	// ヘッダを include できない**（殻のヘッダは公開ビルドの本体に無い。
	// probes/runtime/README.md）ので、プローブは同じ綴りを自分で書き写す。
	// ここを変えるときは probes/runtime/ の呼ぶ側も一緒に直すこと。
	constexpr const char* kLibEchoName = "VwSdkProbes_Echo";
	constexpr const char* kLibSumName = "VwSdkProbes_Sum";
	constexpr const char* kLibOutName = "VwSdkProbes_Out";

	// 登録する関数の表。**`fName == nullptr` で終端**する約束
	// （`VWExtensionVSFunctions::GetFunctionsCount` が nullptr まで数える）。
	const SFunctionDef* libraryDef();

	// ---------------------------------------------------------------------
	// 実装の受け皿。VW は**関数名ではなく選択子（表の添字）**で呼んでくるので、
	// 添字から名前へ引き直して振り分ける（SDK の `ADD_LIB_FUNCTION_Ex` と同じ考え方。
	// マクロを使わず手で書いてあるのは、**上限の範囲検査を入れるため**——マクロは
	// `routineSelector < 0` だけしか見ない）。
	class CProbeLibraryRoutine : public VWPluginLibraryRoutine
	{
	public:
		void DispatchRoutine(Sint32 routineSelector, VWPluginLibraryArgTable& argTable) override;

	private:
		static void Echo(VWPluginLibraryArgTable& argTable);
		static void Sum(VWPluginLibraryArgTable& argTable);
		static void Out(VWPluginLibraryArgTable& argTable);
	};

	// 呼び出しの入口（VW が `IID_VSFunctionsEventSink` で引く受け口）。
	// **ルーチンの登録はここのコンストラクタで行う**——基底の `Execute` は登録済みの
	// ルーチン全部に `DispatchRoutine` を回すだけなので、受け口が出来た時点で
	// 揃っていればよい。
	class CProbeLibrary_EventSink : public VWVSFunctions_EventSink
	{
	public:
		explicit CProbeLibrary_EventSink(IVWUnknown* parent);
		~CProbeLibrary_EventSink() override;
	};

	// 拡張機能そのもの。
	class CExtProbeLibrary : public VWExtensionVSFunctions
	{
		DEFINE_VWVSFunctionsExtension;

	public:
		explicit CExtProbeLibrary(CallBackPtr cbp);
		~CExtProbeLibrary() override;

	protected:
		// **`.vwr` の VLIB リソースを開かせない。** 関数の定義は上の表（コード）が
		// 持っているので、リソース側に同じものを置く必要が無い。既定（`_None`）だと
		// VW はリソースを探しにいく。
		Uint32 Initialize() override;

		// 登録は受け口のコンストラクタで済んでいる（上記）。
		void InitRoutines() override;
	};
} // namespace vwprobe
