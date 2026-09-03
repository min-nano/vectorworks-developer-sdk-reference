//
//	PayloadHostHolderTests.cpp
//
//	**殻から渡された VwPayloadHost を、本体が本当に「写して」持っているかの単体テスト**
//	（plugin/src/PayloadHostHolder.h）。
//
//	【なぜこれがテストされているか】ここを写さずにポインタで持っていて、**Vectorworks を
//	落とした**。殻はその構造体を load() のローカルに置いていたので、load から戻った時点で
//	番地は他所へ使い回され、プローブが最初の 1 行を書いた瞬間に「ログの受け口」として
//	スタックのゴミを呼び出した（Findings「プラグインモジュールの読み込みと入れ替え」）。
//	**この壊れ方は実機でしか出ない**——コンパイルもリンクも通るし、CI のビルドも通る。
//	だから写しているかどうかだけは、SDK 抜きでここで確かめる。
//
//	写しているかの確かめ方は「渡した記憶域を**後から塗り潰す**」。ポインタで持っていれば
//	受け口は壊れ、写していれば何事も無く動く。
//
//	走らせ方（CI の lint ワークフローも同じ）:
//	    g++ -std=c++20 -Wall -Wextra -Werror -I plugin/src
//	        plugin/tests/PayloadHostHolderTests.cpp -o /tmp/t && /tmp/t
//

#include "PayloadHostHolder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using vwprobe::payload::HostHolder;

namespace
{
	int gFailures = 0;

	void check(bool ok, const char* what)
	{
		if (!ok)
		{
			std::printf("FAIL: %s\n", what);
			++gFailures;
		}
	}

	void checkEqInt(int actual, int expected, const char* what)
	{
		if (actual != expected)
		{
			std::printf("FAIL: %s\n  期待: %d\n  実際: %d\n", what, expected, actual);
			++gFailures;
		}
	}

	// ログの受け口（本物と同じく、ctx を貰って行を溜める）。
	void Collect(void* ctx, const char* line)
	{
		auto* sink = static_cast<std::vector<std::string>*>(ctx);
		sink->push_back((line != nullptr) ? line : "<nullptr>");
	}

	int gDummyCallbacks = 0;

	// 殻が渡してくるものを模す。**わざと使い回される記憶域の上に置く**
	// （実機では殻の load() のスタックフレームがそれに当たる）。
	void FillHost(void* storage, void* logCtx)
	{
		VwPayloadHost host{};
		host.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
		host.abiVersion = VW_PAYLOAD_ABI_VERSION;
		host.callbacks = &gDummyCallbacks;
		host.logCtx = logCtx;
		host.log = &Collect;
		std::memcpy(storage, &host, sizeof(VwPayloadHost));
	}
} // namespace

int main()
{
	std::vector<std::string> lines;

	// --- 受け取れないもの ---------------------------------------------------
	{
		HostHolder holder;
		checkEqInt(holder.adopt(nullptr), kVwPayloadErrHost, "nullptr は受け取らない");
		check(!holder.valid(), "受け取れなかったら valid ではない");

		VwPayloadHost host{};
		host.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
		host.abiVersion = VW_PAYLOAD_ABI_VERSION + 1u;
		host.callbacks = &gDummyCallbacks;
		checkEqInt(holder.adopt(&host), kVwPayloadErrAbi, "版が違えば受け取らない");

		host.abiVersion = VW_PAYLOAD_ABI_VERSION;
		host.size = static_cast<unsigned int>(sizeof(VwPayloadHost)) - 1u;
		checkEqInt(holder.adopt(&host), kVwPayloadErrAbi, "構造体が短ければ受け取らない");

		host.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
		host.callbacks = nullptr;
		checkEqInt(holder.adopt(&host), kVwPayloadErrHost, "callbacks が無ければ受け取らない");
		check(!holder.valid(), "一度も成功していなければ valid ではない");

		// 受け取れなかった入れ物へログを流しても、黙って捨てるだけ（落ちない）。
		holder.log("捨てられるはずの行");
		check(lines.empty(), "受け取る前のログは捨てる");
	}

	// --- 殻のほうが新しい（後ろに知らない項目が付いている）------------------
	{
		HostHolder holder;
		std::vector<unsigned char> storage(sizeof(VwPayloadHost) + 64, 0xAB);
		FillHost(storage.data(), &lines);
		auto* host = reinterpret_cast<VwPayloadHost*>(storage.data());
		host->size = static_cast<unsigned int>(storage.size()); // 殻は長い構造体を渡してきた
		checkEqInt(holder.adopt(host), kVwPayloadOk, "長い構造体は受け取れる");
		check(holder.valid(), "受け取ったら valid");
	}

	// --- **本題**: 渡した記憶域を塗り潰しても受け口が生きているか -----------
	{
		lines.clear();
		HostHolder holder;
		std::vector<unsigned char> storage(sizeof(VwPayloadHost), 0);
		FillHost(storage.data(), &lines);
		checkEqInt(holder.adopt(reinterpret_cast<const VwPayloadHost*>(storage.data())),
				   kVwPayloadOk, "ふつうに受け取れる");
		check(holder.callbacks() == &gDummyCallbacks, "callbacks を写している");

		// **殻の記憶域が使い回された**ことにする（実機では load() から戻った瞬間）。
		std::memset(storage.data(), 0x5A, storage.size());

		holder.log("塗り潰した後の 1 行");
		check(lines.size() == 1 && lines[0] == "塗り潰した後の 1 行",
			  "渡された記憶域を塗り潰してもログが届く（＝写している）");
		check(holder.callbacks() == &gDummyCallbacks, "塗り潰しても callbacks は変わらない");

		// nullptr の行は空文字にして渡す（受け口に nullptr を触らせない）。
		lines.clear();
		holder.log(nullptr);
		check(lines.size() == 1 && lines[0].empty(), "nullptr の行は空文字で渡す");

		// 手放したら、以後は黙って捨てる（降ろした後に触られても落ちない）。
		lines.clear();
		holder.forget();
		check(!holder.valid(), "forget したら valid ではない");
		check(holder.callbacks() == nullptr, "forget したら callbacks も無い");
		holder.log("降ろした後の行");
		check(lines.empty(), "forget した後のログは捨てる");
	}

	if (gFailures > 0)
	{
		std::printf("\n%d 件失敗しました。\n", gFailures);
		return EXIT_FAILURE;
	}
	std::printf("PayloadHostHolder: すべて通りました。\n");
	return EXIT_SUCCESS;
}
