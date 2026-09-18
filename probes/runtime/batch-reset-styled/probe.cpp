//
//	probes/runtime/batch-reset-styled/probe.cpp
//
//	[issue #81] **1 本ごとの `ResetObject` を、まとめて 1 度に寄せられるか。**
//
//	取り込みの描画時間の 51〜68% を構造材（1 本 93〜130ms）が占めており、その 1 本は
//	`CreateCustomObjectPath` → クラス → スタイル関連付け → ストーリバウンド → パラメータ →
//	**`ResetObject`** → 読み戻し、をそのまま 200〜400 回繰り返している。ここを
//	「全部置く → まとめて 1 度リセット → 潰れたものだけ第 2 パスで直す」に組み替えて
//	よいかどうかを、実機で決める。
//
//	## 先に CI（`sdk-grep`）で分かっていること（この走査の前提）
//
//	  - **再生成・再描画を止めておく口は SDK に無い。** `ISDK.h` 全体に `Batch` /
//	    `Defer` / `Suspend` / `Freeze` の名を持つ関数は無く（`CustomBatchConvert` と
//	    `DoBatchPrintOrExport` は別物）、`Defer` は SDK のヘッダ全体で 1 件も出ない。
//	    再描画側にあるのは `RedrawRect`（**描き直しを起こす**口）だけで、止める口は無い。
//	  - **`CreateCustomObjectPath` だけは `doRegen` を持つ**
//	    （`CreateCustomObjectPath(name, path, profile, bool doRegen = true)`）。
//	    SDK のヘッダ全体でこの引数を持つのはこの 1 本だけ。
//	  - `UpdateStyledObjects(RefNumber)` の説明は VectorScript 側も
//	    "Update all objects of the specified style." だけで、**ジオメトリの作り直しを
//	    含むかは書かれていない**。PIO 側には `kAction_BeginStyledObjectsUpdate` /
//	    `kAction_UpdateStyledObject` / `kAction_EndStyledObjectsUpdate` が届くと
//	    分かっているが、それが「描画属性を流す」だけなのかは実機でしか分からない。
//
//	## この走査で決めること
//
//	  1. **`ResetObject` を呼ばないと、ほんとうに何も起きないのか。**（A 群）
//	     バウンドを書いただけ・作っただけの部材が、どんなパスを持っているか。
//	  2. **1 度で全部を作り直す口はあるか。**（B 群）まとめて置いた部材に対して
//	     - `ResetObject(レイヤのハンドル)`（VS の `ResetObject` は「どの型でも受ける」と
//	       書かれているので、レイヤごと作り直せるなら 1 回で済む）
//	     - `RedrawRect`（再描画の口。**ジオメトリに触らない**ことを確かめる側）
//	     を順に叩き、パスが作り直されるかを読む。
//	  3. **`UpdateStyledObjects` はジオメトリの作り直しを兼ねるか。**（C 群。issue の (b)）
//	     スタイルを当てた部材を `ResetObject` 抜きで置き、`UpdateStyledObjects` を
//	     1 回だけ呼んで、実体が出るかを読む。
//	  4. **寄せたときに何秒縮むのか。**（D 群）1 本ごとに `ResetObject` する今の作りと、
//	     `doRegen=false` で置いてから後でまとめて `ResetObject` する作りを、
//	     同じ本数で計って比べる。**ここが「組み替える価値があるか」の数字**になる。
//
//	## 測り方
//
//	部材は**鉛直**（`(0,0,0) → (0,0,1)`）で作る。**渡す長さ 1 は「死角」(0, 1e-7) の外**
//	なので、作り直しが走れば必ずバウンドどおり（span 3000）へ書き換わる（#61 / #75）。
//	つまり**読み戻した Z の差が 1 のままなら「作り直されていない」、3000 なら
//	「作り直された」**と、1 つの数字で読み分けられる。
//
//	バウンドは `LayerElevation` の offset 2500 / 5500 を ID 0 / 1 に書く（#75 と同じ。
//	新規の空図面で成立し、階も `_Story` バウンドも要らない）。
//
//	**スタイルは実機に頼らず、プローブの中で作る**（`CreatePluginStyle`）。
//	図面にスタイルが無くても走るようにするためで、これが失敗したら C 群だけを
//	`fail` にして、A・B・D 群の結果は残す。
//

#include "Probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// バウンドの解決Z（新規の空図面＝レイヤ高さ 0 で 2500 / 5500 になる）。
	const double kBoundOffsetLow = 2500;
	const double kBoundOffsetHigh = 5500;
	const double kBoundSpan = kBoundOffsetHigh - kBoundOffsetLow; // 3000

	// 作るときに渡す Z の差。**死角の外**なので、作り直しが走れば 3000 になる。
	const double kSeedSpan = 1;

	// B / C 群の 1 群あたりの本数（読み戻しの明細を全部出せる程度に小さく）。
	const int kProbeGroupCount = 5;
	// D 群（時間を計る）の本数。1 本 100ms として 1 群 3 秒。4 群で 12 秒ほど。
	const int kTimedGroupCount = 30;

	std::string Num(double value)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.17g", value);
		return std::string(buffer);
	}

	std::string Ms(double milliseconds)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.1f", milliseconds);
		return std::string(buffer);
	}

	std::string Count(size_t value)
	{
		return std::to_string(static_cast<long long>(value));
	}

	// 経過時間を測る（ミリ秒）。**実機の体感と比べられる数字を出すのがこの調査の柱の 1 つ**
	// なので、内訳ごとに測って足し合わせる。
	class Stopwatch
	{
	public:
		Stopwatch() : fStart(std::chrono::steady_clock::now()) {}
		void restart()
		{
			fStart = std::chrono::steady_clock::now();
		}
		double elapsedMs() const
		{
			const std::chrono::duration<double, std::milli> delta =
				std::chrono::steady_clock::now() - fStart;
			return delta.count();
		}

	private:
		std::chrono::steady_clock::time_point fStart;
	};

	// 1 本ぶんの素性。
	struct Member
	{
		MCObjectHandle handle = nullptr;
		bool boundsWritten = false;
	};

	bool ReadPathEndpoints(MCObjectHandle pio, WorldPt3& outP0, WorldPt3& outP1)
	{
		MCObjectHandle path = gSDK->GetCustomObjectPath(pio);
		if (path == nullptr)
			return false;
		const Boolean got0 = gSDK->NurbsGetPt3D(path, 0, 0, outP0);
		const Boolean got1 = gSDK->NurbsGetPt3D(path, 0, 1, outP1);
		return got0 && got1;
	}

	// 読み戻した Z の差。読めなければ NaN。
	double ReadSpan(MCObjectHandle pio)
	{
		WorldPt3 p0, p1;
		if (!ReadPathEndpoints(pio, p0, p1))
			return std::nan("");
		return p1.z - p0.z;
	}

	// 「作り直されたか」の判定。**バウンドの span にビット一致するか**では見ない
	// ——作り直しは 1〜2 ULP の残差を残す（#67 / #71）ので、1e-6 の窓で見る。
	bool LooksRebuilt(double span)
	{
		return std::fabs(std::fabs(span) - kBoundSpan) < 1e-6;
	}

	bool LooksUntouched(double span)
	{
		return std::fabs(std::fabs(span) - kSeedSpan) < 1e-9;
	}

	std::string DescribeSpan(double span)
	{
		if (std::isnan(span))
			return "(パスを読めなかった)";
		std::string text = Num(span);
		if (LooksRebuilt(span))
			text += "（**作り直された**）";
		else if (LooksUntouched(span))
			text += "（渡したまま＝**作り直されていない**）";
		else
			text += "（**どちらでもない**）";
		return text;
	}

	// `LayerElevation` のバウンドを 1 本書く（#75 と同じ書き方）。
	bool WriteLayerElevationBound(MCObjectHandle pio, short id, double offset)
	{
		// 型は `MockUp` 名前空間（`ISDK.h`）。修飾しないと構文チェックが通らない。
		MockUp::SStoryObjectData data;
		data.fBound = MockUp::eStoryObjectBound_LayerElevation;
		data.fBoundStory = 0;
		data.fLayerLevelType = "";
		data.fOffset = offset;
		return gSDK->SetObjectStoryBound(pio, static_cast<MockUp::TObjectBoundID>(id), data);
	}

	// 構造材 PIO を 1 本作る。**`ResetObject` は呼ばない**（呼ぶかどうかが主語なので）。
	// `doRegen` は `CreateCustomObjectPath` の第 4 引数へそのまま渡す。
	Member CreateMember(bool doRegen, bool writeBounds)
	{
		Member member;

		MCObjectHandle curve = gSDK->CreateNurbsCurve(WorldPt3(0, 0, 0), false, 1);
		if (curve == nullptr)
			return member;
		gSDK->Add3DVertex(curve, WorldPt3(0, 0, kSeedSpan), true);
		// 座標を明示的に入れ直す（#67 / #74 / #75 と同じ作法）。
		if (gSDK->NurbsGetNumPts(curve, 0) >= 2)
		{
			gSDK->NurbsSetPt3D(curve, 0, 0, WorldPt3(0, 0, 0));
			gSDK->NurbsSetPt3D(curve, 0, 1, WorldPt3(0, 0, kSeedSpan));
		}

		MCObjectHandle noProfile = nullptr;
		member.handle = gSDK->CreateCustomObjectPath("StructuralMember", curve, noProfile, doRegen);
		if (member.handle == nullptr)
			return member;

		if (writeBounds)
		{
			const bool wrote0 = WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
			const bool wrote1 = WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
			member.boundsWritten = wrote0 && wrote1;
		}
		return member;
	}

	// 群を 1 つ置く（作るだけ。`ResetObject` は呼ばない）。
	std::vector<Member> PlaceGroup(vwprobe::Report& probe, int count, bool doRegen)
	{
		std::vector<Member> members;
		for (int i = 0; i < count; ++i)
		{
			const Member member = CreateMember(doRegen, true);
			if (member.handle == nullptr)
			{
				probe.fail("CreateCustomObjectPath が nil を返した");
				break;
			}
			if (!member.boundsWritten)
				probe.fail("SetObjectStoryBound が false を返した");
			members.push_back(member);
		}
		return members;
	}

	// 群の読み戻しを 1 行にまとめる（**何本が作り直されたか**が主語）。
	struct GroupReadback
	{
		size_t rebuilt = 0;
		size_t untouched = 0;
		size_t other = 0;
		size_t unreadable = 0;
		double firstSpan = std::nan("");
	};

	GroupReadback ReadGroup(const std::vector<Member>& members)
	{
		GroupReadback readback;
		for (size_t i = 0; i < members.size(); ++i)
		{
			const double span = ReadSpan(members[i].handle);
			if (i == 0)
				readback.firstSpan = span;
			if (std::isnan(span))
				++readback.unreadable;
			else if (LooksRebuilt(span))
				++readback.rebuilt;
			else if (LooksUntouched(span))
				++readback.untouched;
			else
				++readback.other;
		}
		return readback;
	}

	void LogGroup(vwprobe::Report& probe, const std::string& what,
				  const std::vector<Member>& members)
	{
		const GroupReadback readback = ReadGroup(members);
		probe.log("    " + what + ": " + Count(members.size()) + " 本中 **作り直された " +
				  Count(readback.rebuilt) + " 本** ／ 渡したまま " + Count(readback.untouched) +
				  " 本 ／ どちらでもない " + Count(readback.other) + " 本 ／ 読めなかった " +
				  Count(readback.unreadable) + " 本");
		probe.log("      1 本目の Z の差 = " + DescribeSpan(readback.firstSpan));
	}
} // namespace

VW_PROBE("batch-reset-styled", "1 本ごとの ResetObject をまとめられるかを実測する",
		 "バウンドを書いた構造材をリセット抜きで並べ、レイヤごとのリセット・再描画・"
		 "UpdateStyledObjects で実体が出るかを読む。今の作りと "
		 "doRegen=false ＋ 後でまとめてリセットの所要も計る。新規の空図面で走る")
{
	probe.log("=== この図面について ===");
	{
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		TXString layerName;
		if (currentLayer != nullptr)
			gSDK->GetObjectName(currentLayer, layerName);
		probe.log(std::string("いまのレイヤ: \"") + static_cast<const char*>(layerName) + "\"");
		probe.log("渡すパスの Z の差 = " + Num(kSeedSpan) + "（死角の外）／ バウンドの span = " +
				  Num(kBoundSpan) + "（作り直されればこちらになる）");
	}

	// =======================================================================
	// A. 物差し。**バウンドを書いただけでは何も起きない**ことと、
	//    **`ResetObject` 1 回で作り直される**ことを先に確かめる。
	//    ここが崩れていれば、以下の群は読んではいけない。
	probe.log("=== A. 物差し（リセット無しでは作り直されない／リセット 1 回で作り直される） ===");
	{
		const Member regenOn = CreateMember(true, true);
		if (regenOn.handle == nullptr)
		{
			probe.fail("A: doRegen=true の部材を作れなかった");
		}
		else
		{
			probe.log("  A1 doRegen=true で作ってバウンドを書いただけ（リセット無し）");
			probe.log("    Z の差 = " + DescribeSpan(ReadSpan(regenOn.handle)));
			gSDK->ResetObject(regenOn.handle);
			probe.log("  A2 同じ部材に ResetObject を 1 回");
			probe.log("    Z の差 = " + DescribeSpan(ReadSpan(regenOn.handle)));
		}

		const Member regenOff = CreateMember(false, true);
		if (regenOff.handle == nullptr)
		{
			probe.fail("A: doRegen=false の部材を作れなかった");
		}
		else
		{
			probe.log("  A3 **doRegen=false** で作ってバウンドを書いただけ（リセット無し）");
			probe.log("    Z の差 = " + DescribeSpan(ReadSpan(regenOff.handle)));
			gSDK->ResetObject(regenOff.handle);
			probe.log("  A4 同じ部材に ResetObject を 1 回");
			probe.log("    Z の差 = " + DescribeSpan(ReadSpan(regenOff.handle)) +
					  "（A2 と同じなら、**作るときの regen を省いても後から取り返せる**）");
		}
	}

	// =======================================================================
	// B. **1 度で全部を作り直す口はあるか。** 置いただけの群に対して、
	//    レイヤごとのリセットと再描画を順に叩く。
	probe.log("=== B. まとめて作り直す口を叩く（置いただけの群に対して） ===");
	{
		probe.log("  B1 ResetObject(レイヤのハンドル)");
		std::vector<Member> layerGroup = PlaceGroup(probe, kProbeGroupCount, false);
		LogGroup(probe, "叩く前", layerGroup);
		MCObjectHandle currentLayer = gSDK->GetCurrentLayer();
		if (currentLayer == nullptr)
		{
			probe.fail("B1: GetCurrentLayer が nil を返した");
		}
		else
		{
			Stopwatch watch;
			gSDK->ResetObject(currentLayer);
			const double elapsed = watch.elapsedMs();
			LogGroup(probe, "ResetObject(レイヤ) の後", layerGroup);
			probe.log("      所要 " + Ms(elapsed) + " ms");
		}

		probe.log("  B2 RedrawRect（再描画の口。**ジオメトリに触らない**ことを確かめる側）");
		std::vector<Member> redrawGroup = PlaceGroup(probe, kProbeGroupCount, false);
		LogGroup(probe, "叩く前", redrawGroup);
		{
			WorldRect wide;
			wide.left = -1000000;
			wide.top = 1000000;
			wide.right = 1000000;
			wide.bottom = -1000000;
			Stopwatch watch;
			gSDK->RedrawRect(wide);
			const double elapsed = watch.elapsedMs();
			LogGroup(probe, "RedrawRect の後", redrawGroup);
			probe.log("      所要 " + Ms(elapsed) + " ms");
		}
	}

	// =======================================================================
	// C. **`UpdateStyledObjects` はジオメトリの作り直しを兼ねるか**（issue の (b)）。
	//    スタイルはこのプローブの中で作る（`CreatePluginStyle`）。
	probe.log("=== C. UpdateStyledObjects（issue の (b)） ===");
	{
		// スタイルの元になる 1 本。**作り直してからスタイルにする**（実体のある部材から
		// スタイルを作るため）。
		const Member seed = CreateMember(true, true);
		RefNumber styleRef = 0;
		bool haveStyle = false;
		if (seed.handle == nullptr)
		{
			probe.fail("C: スタイルの元にする部材を作れなかった");
		}
		else
		{
			gSDK->ResetObject(seed.handle);
			probe.log("  C1 CreatePluginStyle を呼ぶ（**ここでダイアログが出たら、"
					  "この行が最後のログになる**）");
			gSDK->CreatePluginStyle(seed.handle);
			const Boolean got = gSDK->GetPluginObjectStyle(seed.handle, styleRef);
			haveStyle = (got != 0) && (styleRef != 0);
			probe.log("    GetPluginObjectStyle → " + std::string(got ? "true" : "false") +
					  " ／ styleRef = " + Count(static_cast<size_t>(styleRef)) +
					  (haveStyle ? "" : "（**スタイルを作れなかった**）"));
		}

		if (!haveStyle)
		{
			probe.fail("C: スタイルを用意できなかったので UpdateStyledObjects を測れていない");
		}
		else
		{
			probe.log("  C2 スタイルを当てた部材を **ResetObject 抜き**で " +
					  Count(static_cast<size_t>(kProbeGroupCount)) + " 本置く");
			std::vector<Member> styled;
			for (int i = 0; i < kProbeGroupCount; ++i)
			{
				Member member = CreateMember(false, false);
				if (member.handle == nullptr)
				{
					probe.fail("C2: 部材を作れなかった");
					break;
				}
				// **スタイルを当ててからバウンドを書く**（実プラグインと同じ順序）。
				if (!gSDK->SetPluginObjectStyle(member.handle, styleRef))
					probe.fail("C2: SetPluginObjectStyle が false を返した");
				const bool wrote0 = WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
				const bool wrote1 = WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
				member.boundsWritten = wrote0 && wrote1;
				if (!member.boundsWritten)
					probe.fail("C2: SetObjectStoryBound が false を返した");
				styled.push_back(member);
			}
			LogGroup(probe, "UpdateStyledObjects の前", styled);

			probe.log("  C3 UpdateStyledObjects を 1 回だけ呼ぶ");
			Stopwatch watch;
			gSDK->UpdateStyledObjects(styleRef);
			const double elapsed = watch.elapsedMs();
			LogGroup(probe, "UpdateStyledObjects の後", styled);
			probe.log("      所要 " + Ms(elapsed) + " ms（" + Count(styled.size()) + " 本ぶん）");

			// **作り直されなかったなら、その後の ResetObject では出るのか**を続けて見る
			// ——「UpdateStyledObjects を呼んだ後は ResetObject が効かなくなる」という
			// 最悪の筋（順序に依る）を潰しておくため。
			probe.log("  C4 続けて 1 本ごとの ResetObject（効き目が残っているかの確認）");
			for (size_t i = 0; i < styled.size(); ++i)
				gSDK->ResetObject(styled[i].handle);
			LogGroup(probe, "ResetObject の後", styled);
		}
	}

	// =======================================================================
	// D. **寄せたときに何秒縮むのか。** 同じ本数で、今の作りと寄せた作りを計る。
	probe.log("=== D. 所要を計る（" + Count(static_cast<size_t>(kTimedGroupCount)) +
			  " 本ずつ。数字は実機のこの図面でのもの） ===");
	{
		// D1: いまの作り。1 本ごとに 作る（doRegen=true）→ バウンド → ResetObject。
		{
			double createMs = 0, boundMs = 0, resetMs = 0;
			Stopwatch total;
			for (int i = 0; i < kTimedGroupCount; ++i)
			{
				Stopwatch watch;
				Member member = CreateMember(true, false);
				createMs += watch.elapsedMs();
				if (member.handle == nullptr)
				{
					probe.fail("D1: 部材を作れなかった");
					break;
				}
				watch.restart();
				WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
				WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
				boundMs += watch.elapsedMs();
				watch.restart();
				gSDK->ResetObject(member.handle);
				resetMs += watch.elapsedMs();
			}
			const double totalMs = total.elapsedMs();
			probe.log("  D1 いまの作り（doRegen=true → バウンド → 1 本ごとに ResetObject）");
			probe.log("    合計 " + Ms(totalMs) + " ms ／ 1 本 " + Ms(totalMs / kTimedGroupCount) +
					  " ms");
			probe.log("    内訳: 作る " + Ms(createMs) + " ms ／ バウンド " + Ms(boundMs) +
					  " ms ／ リセット " + Ms(resetMs) + " ms");
		}

		// D2: 作るときの regen だけを省く（1 本ごとのリセットは残す）。
		{
			double createMs = 0, boundMs = 0, resetMs = 0;
			Stopwatch total;
			for (int i = 0; i < kTimedGroupCount; ++i)
			{
				Stopwatch watch;
				Member member = CreateMember(false, false);
				createMs += watch.elapsedMs();
				if (member.handle == nullptr)
				{
					probe.fail("D2: 部材を作れなかった");
					break;
				}
				watch.restart();
				WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
				WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
				boundMs += watch.elapsedMs();
				watch.restart();
				gSDK->ResetObject(member.handle);
				resetMs += watch.elapsedMs();
			}
			const double totalMs = total.elapsedMs();
			probe.log("  D2 **doRegen=false** ＋ 1 本ごとに ResetObject");
			probe.log("    合計 " + Ms(totalMs) + " ms ／ 1 本 " + Ms(totalMs / kTimedGroupCount) +
					  " ms");
			probe.log("    内訳: 作る " + Ms(createMs) + " ms ／ バウンド " + Ms(boundMs) +
					  " ms ／ リセット " + Ms(resetMs) + " ms");
			probe.log("    ↑ D1 と比べて「作る」が縮んでいれば、**作成時にも regen が"
					  "走っていた**ということ");
		}

		// D3: 全部置いてから、まとめて 1 パスでリセットする（issue が提案している形）。
		{
			std::vector<Member> members;
			double placeMs = 0, resetMs = 0;
			Stopwatch total;
			{
				Stopwatch watch;
				for (int i = 0; i < kTimedGroupCount; ++i)
				{
					Member member = CreateMember(false, false);
					if (member.handle == nullptr)
					{
						probe.fail("D3: 部材を作れなかった");
						break;
					}
					WriteLayerElevationBound(member.handle, 0, kBoundOffsetLow);
					WriteLayerElevationBound(member.handle, 1, kBoundOffsetHigh);
					members.push_back(member);
				}
				placeMs = watch.elapsedMs();
				watch.restart();
				for (size_t i = 0; i < members.size(); ++i)
					gSDK->ResetObject(members[i].handle);
				resetMs = watch.elapsedMs();
			}
			const double totalMs = total.elapsedMs();
			probe.log("  D3 **全部置いてから、まとめて 1 パスで ResetObject**");
			probe.log("    合計 " + Ms(totalMs) + " ms ／ 1 本 " + Ms(totalMs / kTimedGroupCount) +
					  " ms");
			probe.log("    内訳: 置く " + Ms(placeMs) + " ms ／ 第 2 パスのリセット " +
					  Ms(resetMs) + " ms");
			LogGroup(probe, "D3 の読み戻し", members);
			probe.log("    ↑ D2 と同じくらいなら、**「後でまとめて」自体に得は無い**"
					  "（リセットの回数が同じなので）。縮んでいれば得がある");
		}
	}

	probe.log("=== まとめ ===");
	probe.log("  読む順: A（物差し）→ B（まとめて作り直す口）→ C（UpdateStyledObjects）"
			  "→ D（所要）。A が崩れていたら以下は読まない");
}
