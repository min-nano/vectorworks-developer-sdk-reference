//
//	ExtPioMatrix.h
//
//	**調査のための一時的な PIO**（パラメトリックオブジェクト）。issue #185 の
//	「OIP 編集時の `Recalculate` で PIO の行列を読む口は実際の配置を返すか」を、
//	**原点から離し・回転させた PIO**で実機に測らせるためだけに在る。
//	**役目を終えたら殻から外す**（`CLAUDE.md`「実機確認プラグイン」。外し方は
//	`PioMatrixTrace.h` の末尾）。
//
//	【なぜ殻に置くのか】拡張機能の登録はモジュールの読み込みのときに起きるので、
//	入れ替えできる本体（`.vwpayload`）には置けない。したがって**この PIO は公開ビルドでは
//	確かめられない**——PR の Actions の成果物を手で入れてもらう（`plugin/README.md`
//	「PR のビルドを手で入れて確かめるとき」）。
//
//	【何をするか】絵は「ローカル原点から `TraceLength` だけ伸びる線 1 本」だけで、**本体は
//	`Recalculate` の中で行列を読む 4 つの口を突き合わせてファイルへ書き溜めること**
//	（`PioMatrixTrace.h`）。プローブ（`probes/runtime/pio-matrix-*`）が前後に印を入れ、
//	溜まった行を吐き出して機械で比べる。
//
//	【読む口が 4 つある】SDK の実装を読むと（`sdk-grep`）**同じ呼び出しに落ちる組が
//	ある**ので、4 つを同じ `Recalculate` の中で並べて記録する:
//	  * `VWParametricObj::GetObjectToWorldTransform` → `GS_GetEntityMatrix(…, true)`
//	  * `VWObject::GetObjectMatrix`                  → `GS_GetEntityMatrix(…, true)`
//	  * `gSDK->GetEntityMatrix`                      → ISDK の口（legacy Z を通さない）
//	  * `VWObject::GetObjectModelMatrix`             → `gSDK->GetEntityMatrix`
//	並べて記録するのは、**「口を変えれば直る」のか「どの口でも同じ」なのかを、推測でなく
//	1 回の実行で決める**ため。
//
//	【呼ばれた文脈は `OnAddState` で分かる】`kObjXPropAcceptStates` を立てておくと、
//	リセットの理由（`ObjectState::EStateType`。`kObjectExternalReset` ＝外からの
//	`ResetObject` ／ `kParameterChangedReset` ＝ OIP でパラメータを編集）が
//	`Recalculate` の直前に届く（issue #183 で実測済み）。
//

#pragma once

#include "VectorworksSDK.h"

namespace vwprobe
{
	using namespace VWFC::PluginSupport;

	// PIO のイベントを受ける側。`Recalculate` が本体。
	class CPioMatrix_EventSink : public VWParametric_EventSink
	{
	public:
		explicit CPioMatrix_EventSink(IVWUnknown* parent);
		~CPioMatrix_EventSink() override;

		// 拡張プロパティ（`kObjXPropAcceptStates` を立てる）。
		EObjectEvent OnInitXProperties(CodeRefID objectID) override;
		// リセットの理由が届く口。`Recalculate` の直前に来る。
		EObjectEvent OnAddState(ObjectState& stateInfo) override;
		// 作り直し。4 つの口で行列を読んで書き溜め、線 1 本を描く。
		EObjectEvent Recalculate() override;
	};

	// PIO の拡張そのもの。**サブタイプは issue #183 と同じ線分**
	// （`kParametricSubType_Linear`）にする——#183 の表へ行を足すのが目的なので、
	// 被験体を変えると「配置を変えたから違うのか、サブタイプを変えたから違うのか」が
	// 分からなくなる。実プラグインの耐力壁 PIO も線分である。なお線分にしても両端は
	// 行列に効かない（`GetLinearObjectPos` は `(0,0)`–`(0,0)` のまま。#181 で実測）ので、
	// 行列を決めるのは `CreateCustomObject(名前, 位置, 角度)` だけである。
	class CExtObjPioMatrix : public VWExtensionParametric
	{
		DEFINE_VWParametricExtension;

	public:
		explicit CExtObjPioMatrix(CallBackPtr cbp);
		~CExtObjPioMatrix() override;
	};
} // namespace vwprobe
