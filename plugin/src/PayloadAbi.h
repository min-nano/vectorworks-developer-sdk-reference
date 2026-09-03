//
//	PayloadAbi.h
//
//	**殻（Vectorworks が読み込むプラグイン）と、外部モジュール（ペイロード）の間の
//	唯一の約束事。**
//
//	【なぜ要るか】コンパイル済みプラグイン（.vwlibrary / .vlb）は Vectorworks の起動時に
//	しか読み込まれず、読み込み済みのモジュールは差し替えられない——だから入れ替えのたびに
//	再起動が要る（plugin/src/Update.h）。そこで**調査コードの側を、Vectorworks が知らない
//	別の動的モジュールへ出して自分で読み込む**と、差し替えは「ファイルを置き換えて読み直す」
//	だけになり、再起動が要らなくなる（はず）。これはその検証のための境界。
//
//	【なぜ C の ABI か】ペイロードは**降ろして読み直す**ので、境界に C++ の型を置けない:
//
//	  * 例外は境界を越えさせない（越えた先のモジュールが消えていれば巻き戻せない）。
//	  * std::string / std::vector を跨いで渡さない（アロケータと実装の一致に頼りたくない。
//	    ペイロードは殻とは**別に**コンパイルされうる）。
//	  * 仮想関数テーブルを持つオブジェクトを殻へ残さない（**降ろした瞬間に vtable が
//	    消える**）。返した const char* も同じ理由で、呼び出し側が**その場で写す**。
//
//	【SDK を include しない】この 1 ファイルだけは SDK に依存しない（プラットフォームにも）。
//	殻とペイロードの両方が include するので、依存を持ち込むと境界の意味が薄れる。
//	CallBackPtr は void* として渡す（実体は SDK の CallBackPtr）。
//
//	使う側:
//	  * 殻   … plugin/src/PayloadHost.h（読み込み・呼び出し・アンロード）
//	  * 中身 … plugin/src/payload/PayloadMain.cpp（下の関数を export する）
//

#pragma once

#include <cstddef>

// この境界の版。**殻とペイロードで一致しない限り呼ばない**（別々にビルドされ、別々に
// 配られうるので、食い違いを実行時に検出できる唯一の手立て）。境界の形を変えたら上げる。
#define VW_PAYLOAD_ABI_VERSION 1u

// ペイロード側の export 指定。Windows は明示しないと .dll の外から見えない。
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
		// VW_PAYLOAD_ABI_VERSION（殻がコンパイルされた版）。
		unsigned int abiVersion;

		// **SDK の CallBackPtr。** ペイロードはこれを GS_InitializeVCOM へ渡して、
		// 自分の側の gSDK / gCBP / gVWMM を埋める（それらは静的ライブラリが持つ
		// **モジュールごとの**グローバルなので、読み込んだだけでは空のまま）。
		void* callbacks;

		// ログの行き先（殻の vwprobe::Report）。ペイロードは戻り値ではなくこれへ書く。
		void* logCtx;
		void (*log)(void* logCtx, const char* utf8Line);
	};

	// -----------------------------------------------------------------------
	// ペイロードが export する関数の名前（dlsym / GetProcAddress で引く）。
	// 綴りを 1 か所に持つ——殻とペイロードで食い違うと「見つからない」としか出ない。
#define VW_PAYLOAD_SYM_ABI "vw_payload_abi_version"
#define VW_PAYLOAD_SYM_INIT "vw_payload_init"
#define VW_PAYLOAD_SYM_DESCRIBE "vw_payload_describe"
#define VW_PAYLOAD_SYM_RUN "vw_payload_run"
#define VW_PAYLOAD_SYM_SHUTDOWN "vw_payload_shutdown"

	// その型。
	using VwPayloadAbiVersionFn = unsigned int (*)();
	using VwPayloadInitFn = int (*)(const VwPayloadHost*);
	using VwPayloadDescribeFn = const char* (*)();
	using VwPayloadRunFn = int (*)(const char*);
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
		kVwPayloadErrUnknownId = 5, // 知らない id を run された
		kVwPayloadErrException = 6, // 中で例外が出た（境界の手前で握り潰した）
	};
} // extern "C"
