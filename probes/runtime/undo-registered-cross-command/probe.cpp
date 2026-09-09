//
//	probes/runtime/undo-registered-cross-command/probe.cpp
//
//	[issue #54] 自分の undo イベントに登録して EndUndoEvent() で閉じたレイヤは、
//	「そのコマンドが終わった後」の別のコマンド実行から、スクリプトエンジン経由の
//	Undo（DoMenuTextByName('Undo', 0)）で戻せるかを確かめる。
//
//	Findings/Undo.md「間接経路: スクリプトエンジン経由で DoMenuTextByName 相当を呼ぶ」は
//	「呼び出し元（C++）がしたことは取り消しスタックに載っていない」と書いているが、これは
//	probes/runtime/script-engine-undo/probe.cpp（既に削除済み）の pre マーカーが**undo
//	イベントの外**で CreateLayer していたための観測であり、「登録していれば載らない」と
//	いう意味ではない（issue #54 の指摘。ホームズ君 IFC 取り込みプラグインの実機フィード
//	バックでは、登録して閉じたイベントが次のコマンド実行の Undo で実際に戻っている）。
//
//	このプローブは 2 回に分けて走らせる。本体はモジュール内の static 変数で前回の
//	状態を覚えている——プラグイン本体（ペイロード）はコマンドをまたいで同じモジュールが
//	載ったままなので（plugin/README.md「殻と本体」）、これで cross-command の状態を
//	再現できる。
//
//	  1 回目: SetUndoMethod(kUndoSwapObjects) + NameUndoEvent(...) でイベントを開き、
//	          新規レイヤを作って AddAfterSwapObject で登録し、EndUndoEvent() で閉じる。
//	          ここでコマンドが終わる（＝ DoInterface が戻る）。
//	  2 回目（別のメニュー実行）: 前回のレイヤがまだ残っているか読み戻してから、
//	          IVectorScriptEngine 経由で DoMenuTextByName('Undo', 0) を 1 回呼び、
//	          もう一度読み戻す。
//
//	目視は要らない——「レイヤ名が読み戻せるか」だけで判定できる（issue #54 が指摘する
//	「効いたかは読み戻しで判定する」をそのまま踏襲）。1 回目と 2 回目の間に他の undo
//	可能な操作（レイヤ・図形の作成/削除等）を挟むと、2 回目の Undo が別の操作を戻して
//	しまい判定を汚すので行わないこと。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Scripting/IVectorScriptEngine.h"
#include "VWFC/VWObjects/VWDocument.h"
#include "VWFC/VWObjects/VWLayerObj.h"

#include <string>

namespace
{
	using namespace VectorWorks::Scripting;

	// probes/runtime/script-engine-undo/probe.cpp（既に削除済み）と同じ辿り方
	// （ISDK に名前でレイヤを引く呼び出しは無い——Findings「レイヤのハンドルを直接
	// DeleteObject する」冒頭）。
	bool LayerExistsByName(const TXString& name)
	{
		for (MCObjectHandle h = VWDocument::GetDrawingHeaderFristMember(); h != nil;
			 h = gSDK->NextObject(h))
		{
			if (!VWLayerObj::IsLayerObject(h))
				continue;
			VWLayerObj layer(h); // SDK のラッパは const 修飾が揃っていないので非 const で持つ
			if (layer.GetObjectName() == name)
				return true;
		}
		return false;
	}

	std::string ExistsWord(bool exists)
	{
		return exists ? "残っている" : "消えた";
	}

	// 2 回目の実行から見えればよいだけなので、モジュール内の static で足りる。
	TXString& PreviousLayerName()
	{
		static TXString sName;
		return sName;
	}
} // namespace

VW_PROBE("undo-registered-cross-command",
		 "登録して閉じた undo イベントは、次のコマンド実行の Undo で戻るか",
		 "1 回目でレイヤを作って自分の undo イベントに登録・close し、2 回目でスクリプト"
		 "エンジン経由の Undo を 1 回掛けて、前回のレイヤが消えるかを読み戻しで確かめる"
		 "（issue #54）")
{
	TXString& previous = PreviousLayerName();

	if (previous.GetLength() == 0)
	{
		// --- 1 回目: 登録して閉じる ---
		const TXString name = "probe-undo-cross-command-1";

		probe.log("1 回目: undo イベントを開く（SetUndoMethod + NameUndoEvent）");
		gSDK->SetUndoMethod(kUndoSwapObjects);
		gSDK->NameUndoEvent("probe-undo-registered-cross-command");

		MCObjectHandle layer = gSDK->CreateLayer(name, kLayerDesign);
		if (layer == nil)
		{
			gSDK->EndAndRemoveUndoEvent();
			probe.fail("1 回目: CreateLayer が nil を返した");
			return;
		}
		probe.log("1 回目: レイヤを作った（" + std::string(static_cast<const char*>(name)) +
				  "）。存在確認: " + ExistsWord(LayerExistsByName(name)));

		Boolean added = gSDK->AddAfterSwapObject(layer);
		probe.log(std::string("1 回目: AddAfterSwapObject の戻り値=") +
				  (added ? "true" : "false"));

		Boolean ended = gSDK->EndUndoEvent();
		probe.log(std::string("1 回目: EndUndoEvent() 戻り値=") + (ended ? "true" : "false") +
				  " / undo building=" +
				  (gSDK->IsCurrentlyBuildingAnUndoEvent() ? "yes" : "no"));

		previous = name;
		probe.log("1 回目はここまで。もう一度このプローブを選んで走らせてください"
				  "（2 回目で Undo を掛けます。それまで他の undo 可能な操作をしないこと）");
		return;
	}

	// --- 2 回目: 前回のレイヤの残存を確認してから Undo を掛ける ---
	const TXString name = previous;
	previous = ""; // 途中で fail しても 3 回目が 1 回目からやり直せるように、先に戻す

	probe.log("2 回目: 前回のレイヤ（" + std::string(static_cast<const char*>(name)) +
			  "）の残存を確認する");
	const bool before = LayerExistsByName(name);
	probe.log("2 回目: undo 前の存在確認: " + ExistsWord(before));
	if (!before)
	{
		probe.fail("2 回目: 前回のレイヤが最初から無い（1 回目の後に他の undo 可能な"
				   "操作をしていないか確認すること）");
		return;
	}

	IVectorScriptEnginePtr engine(IID_VectorScriptEngine);
	if (!engine)
	{
		probe.fail("2 回目: IVectorScriptEngine を取得できなかった（VCOMPtr が空）");
		return;
	}

	const TXString script = TXString("PROCEDURE __ProbeUndoCrossCommand;\n"
									  "BEGIN\n"
									  "\tDoMenuTextByName('Undo', 0);\n"
									  "END;\n"
									  "Run(__ProbeUndoCrossCommand);\n");
	VCOMError err = engine->ExecuteScript(script);
	probe.log(std::string("2 回目: ExecuteScript(Undo) VCOMError=") + std::to_string((long)err) +
			  " succeeded=" + (VCOM_SUCCEEDED(err) ? "yes" : "no"));

	const bool after = LayerExistsByName(name);
	probe.log("2 回目: undo 後の存在確認: " + ExistsWord(after));

	if (before && !after)
		probe.log("=== 結論: 別コマンドから、前回閉じた自分の undo イベントを"
				  "1 段の Undo で戻せた ===");
	else
		probe.fail(std::string("結論: 前回閉じた自分の undo イベントは、別コマンドからの"
								"Undo では戻らなかった（before=") +
					ExistsWord(before) + " after=" + ExistsWord(after) + "）");
}
