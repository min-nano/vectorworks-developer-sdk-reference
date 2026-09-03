//
//	PayloadHostHolder.h
//
//	**本体（ペイロード）が、殻から渡された VwPayloadHost を写して持つ入れ物。**
//
//	【なぜ写すのか——ここを落として Vectorworks ごと落とした】殻が渡してくる
//	`const VwPayloadHost*` が殻のどこを指しているか（ローカルか、メンバか）は、本体からは
//	分からない。最初の実装はポインタをそのまま持っていて、殻の側がそれを `load()` の
//	**ローカル**に置いていた。`load()` から戻った時点でその番地は他所に使い回され、
//	プローブが最初の 1 行を書いた瞬間に「ログの受け口」としてスタックのゴミを呼び出して
//	落ちた（Instruction Abort。Findings「プラグインモジュールの読み込みと入れ替え」）。
//
//	だから**境界を越えて来たものは、その場で写す**。逆向き（本体 → 殻）は殻が写す
//	（PayloadAbi.h「渡す構造体の寿命」）。殻の側も降ろすまで生かしてはいるが、それは
//	二重の歯止めであって、片方だけでは足りない——殻と本体は別々に配られるので、
//	**古い相手と組んでも壊れない**ことが要る。
//
//	【SDK にもプラットフォームにも依存しない】だから plugin/tests から単体で確かめられる
//	（plugin/tests/PayloadHostHolderTests.cpp）。写しているかどうかは、渡した記憶域を
//	後から塗り潰しても受け口が生きていることで確かめる。
//

#pragma once

#include "PayloadAbi.h"

#include <cstddef>

namespace vwprobe
{
	namespace payload
	{
		class HostHolder
		{
		public:
			// 受け取って**写す**。版と大きさが合わなければ写さずに理由を返す
			// （戻り値は VwPayloadStatus。0 が成功）。
			int adopt(const VwPayloadHost* host);

			// 手放す（降ろす直前・初期化に失敗したとき）。
			void forget()
			{
				fHost = VwPayloadHost{};
				fValid = false;
			}

			bool valid() const
			{
				return fValid;
			}

			// SDK の CallBackPtr（GS_InitializeVCOM へ渡すもの）。
			void* callbacks() const
			{
				return fValid ? fHost.callbacks : nullptr;
			}

			// ログを 1 行流す。受け口が無ければ黙って捨てる（ログのために落ちない）。
			void log(const char* line) const
			{
				if (fValid && fHost.log != nullptr)
					fHost.log(fHost.logCtx, (line != nullptr) ? line : "");
			}

		private:
			VwPayloadHost fHost{};
			bool fValid = false;
		};

		inline int HostHolder::adopt(const VwPayloadHost* host)
		{
			this->forget();
			if (host == nullptr)
				return kVwPayloadErrHost;
			// 版と大きさの二重の歯止め（殻と本体は別々にビルドされ、別々に配られる）。
			if (host->abiVersion != VW_PAYLOAD_ABI_VERSION)
				return kVwPayloadErrAbi;
			if (host->size < sizeof(VwPayloadHost))
				return kVwPayloadErrAbi;
			if (host->callbacks == nullptr)
				return kVwPayloadErrHost;

			// 大きさは確かめてあるので、**こちらが知っている分だけ**写せばよい
			// （殻のほうが新しく、後ろに知らない項目が付いていても構わない）。
			fHost = *host;
			fHost.size = static_cast<unsigned int>(sizeof(VwPayloadHost));
			fValid = true;
			return kVwPayloadOk;
		}
	} // namespace payload
} // namespace vwprobe
