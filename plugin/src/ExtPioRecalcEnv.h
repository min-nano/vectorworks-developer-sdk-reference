//
//	ExtPioRecalcEnv.h
//
//	**調査のための一時的な PIO**（パラメトリックオブジェクト）。issue #183 の
//	「`Recalculate` の中から見える環境は『取り込み時のリセット』と『OIP 編集』で同じか」を
//	実機で測るためだけに在る。**役目を終えたら殻から外す**（`CLAUDE.md`
//	「実機確認プラグイン」。外し方は `PioRecalcTrace.h` の末尾に書いてある）。
//
//	【なぜ殻に置くのか】拡張機能の登録はモジュールの読み込みのときに起きるので、
//	入れ替えできる本体（`.vwpayload`）には置けない。したがって**この PIO は公開ビルドでは
//	確かめられない**——PR の Actions の成果物を手で入れてもらう（`plugin/README.md`
//	「PR のビルドを手で入れて確かめるとき」）。
//
//	【何をするか】絵は「原点から `TraceLength` だけ伸びる線 1 本」だけで、**本体は
//	`Recalculate` の中で見えたものをファイルへ書き溜めること**（`PioRecalcTrace.h`）。
//	プローブ（`probes/runtime/pio-recalc-env-*`）がその前後に印を入れ、溜まった行を
//	吐き出す。
//
//	【呼ばれた文脈は `OnAddState` で分かる】`kObjXPropAcceptStates` を立てておくと、
//	リセットの理由（`ObjectState::EStateType`。`kObjectExternalReset` ＝外からの
//	`ResetObject` ／ `kParameterChangedReset` ＝ OIP でパラメータを編集）が
//	`Recalculate` の直前に届く。だから**プローブ側の印に頼らずに 2 つの文脈を見分けられる
//	見込みがある**（実機で確かめるのはそこも含む）。
//

#pragma once

#include "VectorworksSDK.h"

namespace vwprobe
{
	using namespace VWFC::PluginSupport;

	// PIO のイベントを受ける側。`Recalculate` が本体。
	class CPioRecalcEnv_EventSink : public VWParametric_EventSink
	{
	public:
		explicit CPioRecalcEnv_EventSink(IVWUnknown* parent);
		~CPioRecalcEnv_EventSink() override;

		// 拡張プロパティ（`kObjXPropAcceptStates` を立てる）。
		EObjectEvent OnInitXProperties(CodeRefID objectID) override;
		// リセットの理由が届く口。`Recalculate` の直前に来る。
		EObjectEvent OnAddState(ObjectState& stateInfo) override;
		// 作り直し。見えたものを書き溜めて、線 1 本を描く。
		EObjectEvent Recalculate() override;
	};

	// PIO の拡張そのもの（サブタイプは線分＝`kParametricSubType_Linear`）。
	class CExtObjPioRecalcEnv : public VWExtensionParametric
	{
		DEFINE_VWParametricExtension;

	public:
		explicit CExtObjPioRecalcEnv(CallBackPtr cbp);
		~CExtObjPioRecalcEnv() override;
	};
} // namespace vwprobe
