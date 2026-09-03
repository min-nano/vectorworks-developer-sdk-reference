//
//	payload/PayloadMain.cpp
//
//	**外部モジュール（ペイロード）の中身。** Vectorworks はこのモジュールを知らない
//	——読み込むのは殻（plugin/src/PayloadHost.cpp）で、境界は C の ABI（PayloadAbi.h）。
//	だから**降ろして、置き換えて、読み直せる**（はず）というのが検証したいこと（issue #15）。
//
//	【SDK をどう使えるようにするか】gSDK / gCBP / gVWMM は静的ライブラリ（libVWSDK.a /
//	VWSDK.lib）が持つ**モジュールごとのグローバル**である。このモジュールは自分の複製を
//	持っているので、読み込んだだけでは全部 nil のまま。殻が受け取った CallBackPtr を
//	もらって ::GS_InitializeVCOM へ渡すと、そこで埋まる——普通のプラグインの
//	plugin_module_main がやっているのと同じことを、外から材料をもらって行う形。
//	【ソース根拠】SDKLib/Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp の GS_InitializeVCOM は
//	cbp 以外の外部状態を見ておらず、書くのはそのモジュールのグローバルだけ。
//
//	【この 1 ファイルが 2 つのモジュールになる】CMake が VW_PAYLOAD_VARIANT を変えて 2 回
//	コンパイルし、変種 A と B を作る。**中身の違う 2 つのビルド**を用意するのは、
//	「降ろして別のを読んだら本当に別のコードが動いたか」を実機で見分けるため
//	（同じものを読み直しても、動いたのが新しい版かは言えない）。
//
//	【境界を越えさせないもの】例外（すべてここで受ける）、C++ のオブジェクト、
//	降ろした後も使われる文字列（返す const char* は呼び出し側がその場で写す約束）。
//

#include "PluginPrefix.h"

#include "PayloadAbi.h"

#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

// CMake が -D で渡す（変種名とビルド時刻）。渡らないローカルビルドでも壊れないように。
#ifndef VW_PAYLOAD_VARIANT
#	define VW_PAYLOAD_VARIANT "?"
#endif
#ifndef VW_PAYLOAD_BUILD_TIME
#	define VW_PAYLOAD_BUILD_TIME "unknown"
#endif

namespace
{
	const VwPayloadHost* gPayloadHost = nullptr;
	bool gPayloadReady = false;

	void Log(const std::string& line)
	{
		if (gPayloadHost != nullptr && gPayloadHost->log != nullptr)
			gPayloadHost->log(gPayloadHost->logCtx, line.c_str());
	}

	// 番地を 16 進で。**殻の側の gSDK と見比べる**ために出す（別のモジュールなら
	// 別の番地になるはずで、それが「複製を持っている」ことの実測になる）。
	std::string Hex(const void* p)
	{
		char buf[32] = {0};
		(void)std::snprintf(buf, sizeof(buf), "0x%llx",
							static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(p)));
		return buf;
	}

	// このモジュールの素性（describe が返す 1 行）。**静的に 1 度だけ作る**——返した
	// ポインタは呼び出し側がその場で写す約束だが、寿命はモジュールと同じにしておく。
	const std::string& Description()
	{
		static const std::string sText = std::string("variant=") + VW_PAYLOAD_VARIANT +
										 " abi=" + std::to_string(VW_PAYLOAD_ABI_VERSION) +
										 " built=" + VW_PAYLOAD_BUILD_TIME;
		return sText;
	}

	// 図面を**読むだけ**の自己診断。ここが通れば「別モジュールから SDK が使えた」と言える。
	int SelfTest()
	{
		if (!gPayloadReady || gSDK == nil)
			return kVwPayloadErrNotInit;

		Log("  [payload] " + Description());
		Log("  [payload] gSDK=" + Hex(gSDK) + " gCBP=" + Hex((void*)gCBP));
		Log("  [payload] このモジュール内の関数の番地=" + Hex((const void*)&SelfTest));

		const MCObjectHandle current = gSDK->GetCurrentLayer();
		if (current == nil)
		{
			Log("  [payload] カレントレイヤが取れない（文書が開いていない？）");
			return kVwPayloadOk; // SDK 呼び出し自体は通っている
		}
		// ISDK::GetObjectName は**戻り値ではなく出力引数**で返す
		// （void GetObjectName(MCObjectHandle, TXString&)）。
		TXString name;
		gSDK->GetObjectName(current, name);
		Log("  [payload] カレントレイヤ: " + std::string(static_cast<const char*>(name)));

		// 例のプローブと同じ数え方（図面を変更しない）。
		size_t design = 0;
		size_t sheet = 0;
		for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
			 h = gSDK->NextObject(h))
		{
			if (!VWLayerObj::IsLayerObject(h))
				continue;
			VWLayerObj layer(h);
			if (layer.GetLayerType() == kLayerSheet)
				++sheet;
			else
				++design;
		}
		Log("  [payload] デザインレイヤ " + std::to_string(design) + " 枚 / シートレイヤ " +
			std::to_string(sheet) + " 枚");
		return kVwPayloadOk;
	}
} // namespace

// ---------------------------------------------------------------------------
// SDK の作法: GS_InitializeVCOM がこの関数を呼ぶので、**プラグインでなくてもモジュール側に
// 定義が要る**（Include/VectorworksSDK.h の注記どおり。無いとリンクで未解決になる）。
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver()
{
	return SDK_VERSION;
}

// ---------------------------------------------------------------------------
// ここから下が殻との境界（PayloadAbi.h）。**例外を外へ出さない。**

VW_PAYLOAD_EXPORT unsigned int vw_payload_abi_version()
{
	return VW_PAYLOAD_ABI_VERSION;
}

VW_PAYLOAD_EXPORT int vw_payload_init(const VwPayloadHost* host)
{
	try
	{
		if (host == nullptr)
			return kVwPayloadErrHost;
		// 版と大きさの二重の歯止め（殻とペイロードは別々にビルドされうる）。
		if (host->abiVersion != VW_PAYLOAD_ABI_VERSION || host->size < sizeof(VwPayloadHost))
			return kVwPayloadErrAbi;
		if (host->callbacks == nullptr)
			return kVwPayloadErrHost;

		gPayloadHost = host;

		// **ここが要（かなめ）。** 自分の側の gSDK / gCBP / gVWMM を埋める。
		const VCOMError err = ::GS_InitializeVCOM(host->callbacks);
		if (err != kVCOMError_NoError)
		{
			Log("  [payload] GS_InitializeVCOM が失敗: " + std::to_string((long)err));
			gPayloadHost = nullptr;
			return kVwPayloadErrVcom;
		}
		if (gSDK == nil)
		{
			Log("  [payload] GS_InitializeVCOM は通ったが gSDK が nil のまま");
			gPayloadHost = nullptr;
			return kVwPayloadErrVcom;
		}

		gPayloadReady = true;
		return kVwPayloadOk;
	}
	catch (...)
	{
		gPayloadHost = nullptr;
		gPayloadReady = false;
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT const char* vw_payload_describe()
{
	// init の前でも答えられる（殻が「何を読んだか」を先に言えるように）。
	return Description().c_str();
}

VW_PAYLOAD_EXPORT int vw_payload_run(const char* id)
{
	try
	{
		const std::string which = (id != nullptr) ? id : "";
		if (which == "self-test")
			return SelfTest();
		Log("  [payload] 知らない id: " + which);
		return kVwPayloadErrUnknownId;
	}
	catch (const std::exception& error)
	{
		Log(std::string("  [payload] 例外: ") + error.what());
		return kVwPayloadErrException;
	}
	catch (...)
	{
		Log("  [payload] 素性の分からない例外");
		return kVwPayloadErrException;
	}
}

VW_PAYLOAD_EXPORT void vw_payload_shutdown()
{
	// 降ろす直前に殻が呼ぶ。**VW へ渡したものを残さない**のがここの仕事
	// （このモジュールの番地を Vectorworks 側に持たれたまま降ろすと、次に触った瞬間に落ちる）。
	gPayloadReady = false;
	gPayloadHost = nullptr;
}
