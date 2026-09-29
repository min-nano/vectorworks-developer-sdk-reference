//
//	probes/runtime/drawing-label-layout/probe.cpp
//
//	[issue #149] 図面ラベル（`Drawing Label2`）のラベルレイアウト。**第 3 回まで**
//	（PR #150）で確定したこと:
//
//	  * レイアウトはラベル自身のプロファイルグループ。テキストは
//	    `IDataTagTextLinkSupport` に対応していない（式は空）。
//	  * **欄を決めているのはテキストが持つ隠れた状態**——複製すれば図面タイトルが
//	    出るが、同じ文字列から作り直した版は文字どおりにしか出ない。複製した
//	    テキストの**文字を潰しても**図面タイトルが出た（＝文字は無視される）。
//	  * 注釈へ入れたラベルは `Link State`=1 になり、ホストのビューポートの
//	    タイトルの変更に追随する。ただし**置いた直後の `Title` は古いまま**。
//	  * **`SetParamValue("Title", …)` は `Link State` が 0 でも 1 でも効く。**
//	  * 隠れた状態はテキストにぶら下がる 2 つのユーザーデータ（型 76）で、
//	    2 つ目の中に **UTF-16 の BOM（`ff fe`）に続く `#` で始まる文字列**が見えた。
//
//	**残りはその文字列を読むこと。** issue が聞いている「動的な文字列の式に何を
//	書くと図面タイトルが出るか」の答えそのものなので、ここだけを取りに行く。
//	レイアウトの 3 つのテキスト（タイトル / 縮尺 / 図番）について、ぶら下がっている
//	ユーザーデータの中の UTF-16 文字列を全部拾って出す。
//
//	図面は壊す前提（新規の空図面で走らせる）。undo イベントは開かない。
//

#include "Probe.h"

#include <string>

namespace
{
	std::string DlToUtf8(const TXString& value)
	{
		const char* utf8 = static_cast<const char*>(value);
		return utf8 != nullptr ? std::string(utf8) : std::string();
	}

	// UTF-16LE を UTF-8 へ。BMP だけ（サロゲートは出てこない想定）。
	std::string DlUtf16LeToUtf8(const char* bytes, size_t maxUnits)
	{
		std::string out;
		for (size_t i = 0; i < maxUnits; ++i)
		{
			const unsigned low = static_cast<unsigned char>(bytes[i * 2]);
			const unsigned high = static_cast<unsigned char>(bytes[i * 2 + 1]);
			const unsigned code = low | (high << 8);
			if (code == 0)
				break;
			if (code < 0x80)
			{
				out += static_cast<char>(code);
			}
			else if (code < 0x800)
			{
				out += static_cast<char>(0xC0 | (code >> 6));
				out += static_cast<char>(0x80 | (code & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xE0 | (code >> 12));
				out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (code & 0x3F));
			}
		}
		return out;
	}

	// ハンドルの生バイトから UTF-16 の BOM を探し、続く文字列を全部出す。
	void DlDumpUtf16Strings(vwprobe::Report& probe, const std::string& tag, MCObjectHandle h)
	{
		if (h == nil)
		{
			probe.log("  " + tag + ": nil");
			return;
		}
		size_t size = 0;
		gSDK->GSGetHandleSize(h, size);
		const char* base = *reinterpret_cast<char* const*>(h);
		if (base == nullptr || size < 4)
		{
			probe.log("  " + tag + ": 中身が読めない（大きさ=" + std::to_string(size) + "）");
			return;
		}
		int found = 0;
		for (size_t i = 0; i + 3 < size && found < 6; ++i)
		{
			if (static_cast<unsigned char>(base[i]) != 0xFF ||
				static_cast<unsigned char>(base[i + 1]) != 0xFE)
				continue;
			const size_t start = i + 2;
			size_t units = (size - start) / 2;
			if (units > 64)
				units = 64;
			const std::string text = DlUtf16LeToUtf8(base + start, units);
			probe.log("  " + tag + " +" + std::to_string(start) + " UTF-16='" + text + "'");
			++found;
			i = start + text.size(); // 同じ文字列の中でもう一度当たらないよう進める
		}
		if (found == 0)
			probe.log("  " + tag +
					  ": UTF-16 の BOM が見つからない（大きさ=" + std::to_string(size) + "）");
	}

	// テキストがぶら下げているユーザーデータを全部たどって、中の文字列を出す。
	void DlDumpTextStrings(vwprobe::Report& probe, const std::string& tag, MCObjectHandle hText)
	{
		if (hText == nil)
		{
			probe.log("  " + tag + ": テキストが無い");
			return;
		}
		VWTextBlockObj text(hText);
		probe.log("  " + tag + " 見えている文字='" + DlToUtf8(text.GetText()) + "'");
		int index = 0;
		for (MCObjectHandle aux = gSDK->FirstAuxObject(hText); aux != nil && index < 6;
			 aux = gSDK->NextObject(aux))
		{
			DlDumpUtf16Strings(probe,
							   tag + " 補助[" + std::to_string(index++) +
								   "] 型=" + std::to_string(gSDK->GetObjectTypeN(aux)),
							   aux);
		}
		// テキスト自身にも入っているかもしれないので、そちらも見る。
		DlDumpUtf16Strings(probe, tag + " テキスト自身", hText);
	}

	MCObjectHandle DlLayoutTextAt(MCObjectHandle hLabel, int wanted)
	{
		MCObjectHandle hGroup = gSDK->GetCustomObjectProfileGroup(hLabel);
		if (hGroup == nil)
			return nil;
		int seen = 0;
		for (MCObjectHandle member = gSDK->FirstMemberObj(hGroup); member != nil;
			 member = gSDK->NextObject(member))
		{
			if (gSDK->GetObjectTypeN(member) != kTextNode)
				continue;
			if (seen++ == wanted)
				return member;
		}
		return nil;
	}
} // namespace

VW_PROBE("drawing-label-layout", "図面ラベル: レイアウトのテキストが持つ式を読む",
		 "ラベルレイアウトの 3 つのテキスト（タイトル / 縮尺 / 図番）のユーザーデータから、"
		 "UTF-16 で入っている動的な文字列の式を読み出す")
{
	gSDK->DefineCustomObject("Drawing Label2", kCustomObjectPrefNever);

	probe.log("■ 図面を組む");
	MCObjectHandle hDesign = gSDK->CreateLayer(TXString("調査-デザイン"), kLayerDesign);
	if (hDesign == nil)
	{
		probe.fail("CreateLayer(デザイン) が nil を返した");
		return;
	}
	gSDK->CreateRectangle(WorldRect(0, 3000, 5000, 0));

	MCObjectHandle hSheet = gSDK->CreateLayer(TXString("調査-シート"), kLayerSheet);
	if (hSheet == nil)
	{
		probe.fail("CreateLayer(シート) が nil を返した");
		return;
	}
	MCObjectHandle hViewport = gSDK->CreateViewport(hSheet);
	if (hViewport == nil)
	{
		probe.fail("CreateViewport が nil を返した");
		return;
	}
	gSDK->SetViewportLayerVisibility(hViewport, hDesign, 0);
	gSDK->SetObjectVariable(hViewport, 1032, TVariableBlock(TXString("試験タイトル")));
	gSDK->UpdateViewport(hViewport);

	MCObjectHandle hLabel =
		gSDK->CreateCustomObject("Drawing Label2", WorldPt(0, -2000), 0.0, false);
	if (hLabel == nil || !gSDK->AddViewportAnnotationObject(hViewport, hLabel))
	{
		probe.fail("図面ラベルを注釈へ置けなかった");
		return;
	}
	gSDK->ResetObject(hLabel);

	probe.log("");
	probe.log("■ レイアウトのテキストが持つ文字列");
	DlDumpTextStrings(probe, "[0] タイトル", DlLayoutTextAt(hLabel, 0));
	probe.log("");
	DlDumpTextStrings(probe, "[1] 縮尺", DlLayoutTextAt(hLabel, 1));
	probe.log("");
	DlDumpTextStrings(probe, "[2] 図番", DlLayoutTextAt(hLabel, 2));

	probe.log("");
	probe.log("■ おわり");
}
