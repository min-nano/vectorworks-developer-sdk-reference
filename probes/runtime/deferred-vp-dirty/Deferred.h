//
//	probes/runtime/<slug>/Deferred.h
//
//	[issue #245] 「書いた直後に同じ呼び出しの中で読む」と「Vectorworks
//へ制御を返した後に 	読む」を同じ図形で比べるための、2
//回走らせる仕立て。**同じファイルを各プローブの
//	ディレクトリへ写して使う**（公開ビルドは PR の殻のヘッダを持たないので、
//	probes/runtime/README.md「同じ PR で足した殻のヘッダを include
//しない」）。
//
//	1 回目（状態ファイルが無いとき）:
//	  1.
//自分用の新規図面を開く（ブリッジが開いた図面ではないので、ブリッジの後始末の
//	     保存が掛からない——読み直しに保存が混ざらない）。
//	  2. 各ケースの setup() で書き、すぐに measure() で読む ………………
//T0（同じ呼び出しの中）
//	  3. 呼び出しの中でランループを 1.5 秒回してから、もう一度読む ………
//T1（入れ子のランループ） 	  3b. 自動で開いた undo イベントを EndUndoEvent
//で閉じて、もう一度読む … Te
//	  4. 何もせずに戻る。**戻った後に呼ばれるもの（OS
//のタイマー・dispatch・通知）を 	     仕掛けてはいけない**——殻は走らせた直後に本体を
//dlclose するので、後から呼ばれた 	     コールバックは解放済みのコードへ飛んで VW
//ごと落ちる（実際に落とした。#245）。 	     ブリッジで回すときは、1 回目の後に
//vw_cleanup でブリッジの図面を閉じる——
//	     ブリッジは走らせた後に自分の図面を前へ出して保存するので、閉じれば自分の図面が
//	     前に残る（前に出ていない図面は描かれない）。
//	2 回目（状態ファイルがあるとき）:
//	  5. 自分の図面へ切り替えて読む ……………………………………………… T2（VW
//のイベントループの後）
//	  6. T0 / T1 / T2
//を並べて出し、自分の図面を閉じて（保存しない）状態ファイルを消す。
//
//	図形は名前で引き直す（ハンドルを持ち越さない）。名前は 63
//文字で黙って切られるので 	短く付ける（Findings「Investigation Techniques」）。
//

#pragma once

#include "Probe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if GS_MAC
#	include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
	using DeferredReadings = std::vector<std::pair<std::string, std::string>>;

	struct DeferredCase
	{
		std::string id;	  // 短い id（名前の接頭辞にも使う。英数字）
		std::string what; // 何を確かめるか（ログに出す 1 行）
		std::function<void(vwprobe::Report&)> setup;
		std::function<DeferredReadings()> measure;
	};

	// ---------------------------------------------------------------------------
	// 小道具

	std::string DfNum(double v, int prec = 4)
	{
		if (v > 1e300)
			return "+MAX";
		if (v < -1e300)
			return "-MAX";
		char buf[64];
		std::snprintf(buf, sizeof buf, "%.*f", prec, v);
		return buf;
	}

	std::string DfBool(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string DfName(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		TXString name;
		gSDK->GetObjectName(h, name);
		return static_cast<const char*>(name);
	}

	// 名前を付け、読み戻して確かめる（63 文字で黙って切られるため）。
	bool DfSetName(vwprobe::Report& probe, MCObjectHandle h, const std::string& name)
	{
		if (h == nullptr)
		{
			probe.fail("名前を付ける相手が nil: " + name);
			return false;
		}
		(void)gSDK->SetObjectName(h,
								  TXString(name.c_str())); // 戻り値は当てにならない
		if (DfName(h) != name)
		{
			probe.fail("名前が付かない: " + name + " → " + DfName(h));
			return false;
		}
		return true;
	}

	MCObjectHandle DfFind(const std::string& name)
	{
		return gSDK->GetNamedObject(TXString(name.c_str()));
	}

	std::string DfBounds(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		WorldRect r;
		if (!gSDK->GetObjectBounds(h, r))
			return "(false)";
		// Left() などは空の矩形で 0 を返すので、生の欄を読む（何も描いていない PIO は
		// 「反転した空矩形」±DBL_MAX を返す。Findings「Investigation Techniques」）。
		if (r.right < r.left)
			return "(空 L" + DfNum(r.left, 1) + " R" + DfNum(r.right, 1) + ")";
		return "L" + DfNum(r.left, 1) + " T" + DfNum(r.top, 1) + " R" + DfNum(r.right, 1) + " B" +
			   DfNum(r.bottom, 1);
	}

	// 直下の子の数（型ごとの内訳つき。例 "9 [3:2 84:1 90:6]"）。
	std::string DfChildren(MCObjectHandle h)
	{
		if (h == nullptr)
			return "(nil)";
		std::map<short, int> byType;
		int n = 0;
		for (MCObjectHandle c = gSDK->FirstMemberObj(h); c != nullptr; c = gSDK->NextObject(c))
		{
			++n;
			++byType[gSDK->GetObjectTypeN(c)];
		}
		std::string s = std::to_string(n) + " [";
		bool first = true;
		for (const auto& [type, count] : byType)
		{
			if (!first)
				s += " ";
			first = false;
			s += std::to_string(type) + ":" + std::to_string(count);
		}
		return s + "]";
	}

	// ---------------------------------------------------------------------------
	// 状態ファイル（1 回目 → 2 回目への受け渡し）

	std::filesystem::path DfStatePath(const std::string& slug)
	{
		std::error_code ec;
		return std::filesystem::temp_directory_path(ec) / ("VwSdkProbes-" + slug + ".deferred");
	}

	struct DfState
	{
		Sint32 fileRef = 0;
		std::string stage; // "prepared"（図面を開いただけ）か "written"（書いて
						   // T0〜Te を読んだ）
		std::string startedAt;
		std::map<std::string, std::string> t0;
		std::map<std::string, std::string> t1;
		std::map<std::string, std::string> te; // undo イベントを閉じた直後
		std::vector<std::string> keyOrder;
	};

	std::string DfEscape(const std::string& s)
	{
		std::string out;
		for (char c : s)
			out += (c == '\n' || c == '\t') ? ' ' : c;
		return out;
	}

	void DfSave(const std::string& slug, const DfState& st)
	{
		std::ofstream f(DfStatePath(slug), std::ios::trunc);
		f << "ref\t" << st.fileRef << "\n";
		f << "stage\t" << st.stage << "\n";
		f << "at\t" << st.startedAt << "\n";
		for (const std::string& k : st.keyOrder)
		{
			const auto a = st.t0.find(k);
			const auto b = st.t1.find(k);
			const auto e = st.te.find(k);
			f << "v\t" << DfEscape(k) << "\t" << (a != st.t0.end() ? DfEscape(a->second) : "-")
			  << "\t" << (b != st.t1.end() ? DfEscape(b->second) : "-") << "\t"
			  << (e != st.te.end() ? DfEscape(e->second) : "-") << "\n";
		}
	}

	bool DfLoad(const std::string& slug, DfState& st)
	{
		std::ifstream f(DfStatePath(slug));
		if (!f)
			return false;
		std::string line;
		while (std::getline(f, line))
		{
			std::vector<std::string> cols;
			std::stringstream ss(line);
			std::string col;
			while (std::getline(ss, col, '\t'))
				cols.push_back(col);
			if (cols.size() >= 2 && cols[0] == "ref")
				st.fileRef = static_cast<Sint32>(std::stol(cols[1]));
			else if (cols.size() >= 2 && cols[0] == "stage")
				st.stage = cols[1];
			else if (cols.size() >= 2 && cols[0] == "at")
				st.startedAt = cols[1];
			else if (cols.size() >= 4 && cols[0] == "v")
			{
				st.keyOrder.push_back(cols[1]);
				st.t0[cols[1]] = cols[2];
				st.t1[cols[1]] = cols[3];
				st.te[cols[1]] = cols.size() >= 5 ? cols[4] : "-";
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------------
	// 図面と時計

	std::vector<std::pair<Sint32, bool>> DfOpenFiles()
	{
		std::vector<std::pair<Sint32, bool>> out;
		VectorWorks::TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
			out.emplace_back(files[i].fFileRef, files[i].fIsActive);
		return out;
	}

	Sint32 DfActiveRef()
	{
		for (const auto& [ref, active] : DfOpenFiles())
			if (active)
				return ref;
		return -1;
	}

	bool DfIsOpen(Sint32 ref)
	{
		for (const auto& [r, active] : DfOpenFiles())
			if (r == ref)
				return true;
		return false;
	}

	// 利用者の既定のテンプレート（⌘N が使うもの）の見込みの場所。無ければ空。
	std::string DfDefaultTemplate()
	{
#if GS_MAC
		const char* home = std::getenv("HOME");
		if (home == nullptr)
			return "";
		const std::filesystem::path p = std::filesystem::path(home) /
										"Library/Application Support/Vectorworks/2026/Libraries/"
										"Defaults/Templates/Default.sta";
#else
		const char* appdata = std::getenv("APPDATA");
		if (appdata == nullptr)
			return "";
		const std::filesystem::path p = std::filesystem::path(appdata) /
										"Nemetschek/Vectorworks/2026/Libraries/Defaults/Templates/"
										"Default.sta";
#endif
		std::error_code ec;
		return std::filesystem::exists(p, ec) ? p.string() : "";
	}

	// 自分用の図面を開く。fromTemplate なら既定のテンプレートから（.sta
	// を渡すと無題の新規に なる。Findings「Documents」）。OpenDocumentPath(nullptr)
	// で開いた図面はテンプレートの
	// 資源を持たない——図面ラベルの既定レイアウトに文字が無い等（#245）。
	bool DfOpenOwnDocument(vwprobe::Report& probe, bool fromTemplate)
	{
		const size_t before = DfOpenFiles().size();
		const std::string tpl = fromTemplate ? DfDefaultTemplate() : "";
		if (!tpl.empty())
		{
			VectorWorks::Filing::IFileIdentifierPtr fileID(VectorWorks::Filing::IID_FileIdentifier);
			if (fileID && fileID->Set(TXString(tpl.c_str())) == kVCOMError_NoError)
				(void)gSDK->OpenDocumentPath(fileID, false);
			probe.log("自分用の図面をテンプレートから開く: " + tpl);
		}
		else
		{
			if (fromTemplate)
				probe.log("既定のテンプレートが見つからないので、空の図面を開く");
			(void)gSDK->OpenDocumentPath(nullptr, false);
		}
		if (DfOpenFiles().size() != before + 1)
		{
			probe.fail("自分用の新規図面を開けなかった");
			return false;
		}
		return true;
	}

	// 入れ子のランループを ms ミリ秒ぶん回す（mac は短く刻んで何度も呼ぶ——1
	// 回を長く 取ると即座に戻る。Findings「Timers and Notifications」4 の 5）。
	void DfPump(int ms)
	{
		const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while (std::chrono::steady_clock::now() < until)
		{
#if GS_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
#else
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
		}
	}

	// いまどの時点を読んでいるか（0=T0 / 1=T1 / 2=T2）。T2 でだけ書き込みを伴う読み
	// （例: データタグの更新手順を掛け直してから読む）をしたいケースのため。T0 / T1
	// で 書くと、後の時点の読みが「VW が自分で変えたのか」を表さなくなる。
	int& DfPhase()
	{
		static int sPhase = 0;
		return sPhase;
	}

	std::string DfNow()
	{
		const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		char buf[64];
		std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
		return buf;
	}

	std::string DfShort(const std::string& s, size_t n = 90)
	{
		if (s.size() <= n)
			return s;
		while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
			--n; // UTF-8 の途中で切らない
		return s.substr(0, n) + "…";
	}

	// ---------------------------------------------------------------------------
	// 本体

	void DfLogReadings(vwprobe::Report& probe, const std::string& tag, const DeferredReadings& rs)
	{
		for (const auto& [k, v] : rs)
			probe.log("  " + tag + " " + k + " = " + v);
	}

	// prepareFirst=true なら「図面を開くだけ」の回を先に 1
	// 回挟む（開いた同じ呼び出しの中で
	// 書くと、まだ一度もイベントループを通っていない図面になる——ビューポートの描画などが
	// それで変わるかを切り分けるため。#245）。
	void RunDeferred(vwprobe::Report& probe, const std::string& slug,
					 const std::vector<DeferredCase>& cases, bool prepareFirst = false,
					 bool fromTemplate = false)
	{
		DfState st;
		const bool loaded = DfLoad(slug, st);
		if (!loaded && prepareFirst)
		{
			// ===================== 準備の回 =====================
			if (!DfOpenOwnDocument(probe, fromTemplate))
				return;
			st.fileRef = DfActiveRef();
			st.stage = "prepared";
			st.startedAt = DfNow();
			DfSave(slug, st);
			probe.log(
				"【準備】自分用の図面を開いただけで戻る（fileRef=" + std::to_string(st.fileRef) +
				"）。この図面を前にしたまま、もう一度走らせると書いて読む。");
			return;
		}
		if (!loaded || st.stage == "prepared")
		{
			// ===================== 1 回目 =====================
			probe.log("【1 回目】書いて、同じ呼び出しの中で読む（T0）→ "
					  "ランループを回して読む（T1）");
			probe.log("building(走り出し)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
			if (loaded)
			{
				std::error_code ec;
				if (!DfIsOpen(st.fileRef))
				{
					std::filesystem::remove(DfStatePath(slug), ec);
					probe.fail("準備の回の図面が見つからない。状態を消したので、次は準備か"
							   "らやり直し");
					return;
				}
				(void)gSDK->SwitchToOpenFile(st.fileRef);
				probe.log("準備の回（" + st.startedAt +
						  "）に開いた図面で書く fileRef=" + std::to_string(st.fileRef));
			}
			else
			{
				if (!DfOpenOwnDocument(probe, fromTemplate))
					return;
				st.fileRef = DfActiveRef();
				probe.log("自分用の図面 fileRef=" + std::to_string(st.fileRef));
			}
			st.stage = "written";
			st.startedAt = DfNow();

			DfPhase() = 0;
			for (const DeferredCase& c : cases)
			{
				probe.log("[" + c.id + "] " + c.what);
				try
				{
					c.setup(probe);
				}
				catch (const std::exception& e)
				{
					probe.fail("[" + c.id + "] setup で例外: " + e.what());
				}
				catch (...)
				{
					probe.fail("[" + c.id + "] setup で例外");
				}
				DeferredReadings rs;
				try
				{
					rs = c.measure();
				}
				catch (...)
				{
					probe.fail("[" + c.id + "] measure で例外");
				}
				DfLogReadings(probe, "T0", rs);
				for (const auto& [k, v] : rs)
				{
					const std::string key = c.id + "." + k;
					st.keyOrder.push_back(key);
					st.t0[key] = v;
				}
			}

			probe.log("building(T0 の後)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
			probe.log("ランループを 1500 ms 回す");
			DfPump(1500);
			if (DfActiveRef() != st.fileRef)
				(void)gSDK->SwitchToOpenFile(st.fileRef);
			DfPhase() = 1;
			for (const DeferredCase& c : cases)
			{
				DeferredReadings rs;
				try
				{
					rs = c.measure();
				}
				catch (...)
				{
					probe.fail("[" + c.id + "] measure(T1) で例外");
				}
				for (const auto& [k, v] : rs)
					st.t1[c.id + "." + k] = v;
			}
			probe.log("building(T1 の後)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));

			// **自動で開いた undo
			// イベントを閉じて戻る**（実プラグインは自分のイベントを閉じて
			// 戻る。閉じずに戻ると、利用者が何か操作するまで開いたまま残り、その間ブリッジが
			// 受け付けを見送って止まる——#245 で 7
			// 分止まった）。閉じた直後にも読む（Te）。
			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
				probe.log("EndUndoEvent()=" + DfBool(gSDK->EndUndoEvent()));
			probe.log("building(閉じた後)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
			for (const DeferredCase& c : cases)
			{
				DeferredReadings rs;
				try
				{
					rs = c.measure();
				}
				catch (...)
				{
					probe.fail("[" + c.id + "] measure(Te) で例外");
				}
				for (const auto& [k, v] : rs)
					st.te[c.id + "." + k] = v;
			}
			probe.log("building(Te の後)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
			if (gSDK->IsCurrentlyBuildingAnUndoEvent())
				(void)gSDK->EndUndoEvent(); // 読みでまた開いていたら閉じ直す
			DfSave(slug, st);
			probe.log("状態を保存した: " + DfStatePath(slug).string());
			probe.log("→ 何秒か置いてから、**この図面を前にしたまま**もう一度走らせる。"
					  "ブリッジなら vw_cleanup（ブリッジの図面を閉じる）→ "
					  "new_document=false。");
			return;
		}

		// ===================== 2 回目 =====================
		probe.log("【2 回目】1 回目（" + st.startedAt +
				  "）の図面を、VW へ制御を返した後に読む（T2）");
		probe.log("building(走り出し)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
		std::error_code ec;
		if (!DfIsOpen(st.fileRef))
		{
			std::filesystem::remove(DfStatePath(slug), ec);
			probe.fail("1 回目の図面（fileRef=" + std::to_string(st.fileRef) +
					   "）が見つからない。状態を消したので、次は 1 回目からやり直しになる");
			return;
		}
		probe.log("前に出ていた図面 fileRef=" + std::to_string(DfActiveRef()));
		(void)gSDK->SwitchToOpenFile(st.fileRef);

		std::map<std::string, std::string> t2;
		DfPhase() = 2;
		for (const DeferredCase& c : cases)
		{
			DeferredReadings rs;
			try
			{
				rs = c.measure();
			}
			catch (...)
			{
				probe.fail("[" + c.id + "] measure(T2) で例外");
			}
			for (const auto& [k, v] : rs)
				t2[c.id + "." + k] = v;
		}

		int changed = 0;
		probe.log("---- 比べる（差があった欄だけ ★）----");
		for (const std::string& k : st.keyOrder)
		{
			const std::string a = st.t0[k];
			const std::string b = st.t1[k];
			const std::string e = st.te[k];
			const std::string c = t2.count(k) ? t2[k] : "(読めない)";
			const bool diff = (a != b) || (a != e) || (a != DfEscape(c));
			if (diff)
				++changed;
			probe.log(std::string(diff ? "★ " : "   ") + k + "  T0=" + DfShort(a) +
					  (a == b ? "" : "  T1=" + DfShort(b)) + (a == e ? "" : "  Te=" + DfShort(e)) +
					  (a == DfEscape(c) ? "" : "  T2=" + DfShort(c)));
		}
		probe.log("差のあった欄: " + std::to_string(changed) + " / " +
				  std::to_string(st.keyOrder.size()));
		for (const auto& [k, v] : t2)
		{
			bool known = false;
			for (const std::string& kk : st.keyOrder)
				known = known || kk == k;
			if (!known)
				probe.log("（T2 だけにある欄）" + k + " = " + v);
		}

		(void)gSDK->CloseDocument(); // 戻り値は当てにならない（Findings「Documents」）
		probe.log("自分の図面を閉じた: " + DfBool(!DfIsOpen(st.fileRef)));
		std::filesystem::remove(DfStatePath(slug), ec);
	}
} // namespace
