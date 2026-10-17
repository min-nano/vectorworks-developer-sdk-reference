//
//	probes/runtime/<slug>/Deferred.h
//
//	[issue #245] 「書いた直後に同じ呼び出しの中で読む」と「Vectorworks へ制御を返した後に
//	読む」を同じ図形で比べるための、2 回走らせる仕立て。**同じファイルを各プローブの
//	ディレクトリへ写して使う**（公開ビルドは PR の殻のヘッダを持たないので、
//	probes/runtime/README.md「同じ PR で足した殻のヘッダを include しない」）。
//
//	1 回目（状態ファイルが無いとき）:
//	  1. 自分用の新規図面を開く（ブリッジが開いた図面ではないので、ブリッジの後始末の
//	     保存が掛からない——読み直しに保存が混ざらない）。
//	  2. 各ケースの setup() で書き、すぐに measure() で読む ……………… T0（同じ呼び出しの中）
//	  3. 呼び出しの中でランループを 1.5 秒回してから、もう一度読む ……… T1（入れ子のランループ）
//	  4. 1 秒後に OS のタイマーで自分の図面を前へ戻す（ブリッジの保存が別の図面を前へ
//	     出すため。前に出ていない図面は描かれない）。
//	2 回目（状態ファイルがあるとき）:
//	  5. 自分の図面へ切り替えて読む ……………………………………………… T2（VW のイベントループの後）
//	  6. T0 / T1 / T2 を並べて出し、自分の図面を閉じて（保存しない）状態ファイルを消す。
//
//	図形は名前で引き直す（ハンドルを持ち越さない）。名前は 63 文字で黙って切られるので
//	短く付ける（Findings「Investigation Techniques」）。
//

#pragma once

#include "Probe.h"

#include <chrono>
#include <cstdio>
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
#	include <dispatch/dispatch.h>
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
		(void)gSDK->SetObjectName(h, TXString(name.c_str())); // 戻り値は当てにならない
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
		if (r.Right() < r.Left())
			return "(空)";
		return "L" + DfNum(r.Left(), 1) + " T" + DfNum(r.Top(), 1) + " R" + DfNum(r.Right(), 1) +
			   " B" + DfNum(r.Bottom(), 1);
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
		std::string startedAt;
		std::map<std::string, std::string> t0;
		std::map<std::string, std::string> t1;
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
		f << "at\t" << st.startedAt << "\n";
		for (const std::string& k : st.keyOrder)
		{
			const auto a = st.t0.find(k);
			const auto b = st.t1.find(k);
			f << "v\t" << DfEscape(k) << "\t" << (a != st.t0.end() ? DfEscape(a->second) : "-")
			  << "\t" << (b != st.t1.end() ? DfEscape(b->second) : "-") << "\n";
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
			else if (cols.size() >= 2 && cols[0] == "at")
				st.startedAt = cols[1];
			else if (cols.size() >= 4 && cols[0] == "v")
			{
				st.keyOrder.push_back(cols[1]);
				st.t0[cols[1]] = cols[2];
				st.t1[cols[1]] = cols[3];
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

	// 入れ子のランループを ms ミリ秒ぶん回す（mac は短く刻んで何度も呼ぶ——1 回を長く
	// 取ると即座に戻る。Findings「Timers and Notifications」4 の 5）。
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

	Sint32& DfFrontRef()
	{
		static Sint32 sRef = 0;
		return sRef;
	}

#if GS_MAC
	void DfBringFront(void*)
	{
		if (gSDK != nullptr && DfIsOpen(DfFrontRef()))
			(void)gSDK->SwitchToOpenFile(DfFrontRef());
	}
#else
	void CALLBACK DfBringFrontWin(HWND, UINT, UINT_PTR id, DWORD)
	{
		KillTimer(nullptr, id);
		if (gSDK != nullptr && DfIsOpen(DfFrontRef()))
			(void)gSDK->SwitchToOpenFile(DfFrontRef());
	}
#endif

	// 呼び出しが戻った後に 1 度だけ、自分の図面を前へ戻す。
	void DfScheduleBringFront(Sint32 ref)
	{
		DfFrontRef() = ref;
#if GS_MAC
		dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC),
						 dispatch_get_main_queue(), nullptr, &DfBringFront);
#else
		(void)SetTimer(nullptr, 0, 1500, &DfBringFrontWin);
#endif
	}

	std::string DfNow()
	{
		const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		char buf[64];
		std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
		return buf;
	}

	std::string DfShort(const std::string& s, size_t n = 70)
	{
		return s.size() <= n ? s : s.substr(0, n) + "…";
	}

	// ---------------------------------------------------------------------------
	// 本体

	void DfLogReadings(vwprobe::Report& probe, const std::string& tag, const DeferredReadings& rs)
	{
		for (const auto& [k, v] : rs)
			probe.log("  " + tag + " " + k + " = " + v);
	}

	void RunDeferred(vwprobe::Report& probe, const std::string& slug,
					 const std::vector<DeferredCase>& cases)
	{
		DfState st;
		if (!DfLoad(slug, st))
		{
			// ===================== 1 回目 =====================
			probe.log(
				"【1 回目】書いて、同じ呼び出しの中で読む（T0）→ ランループを回して読む（T1）");
			probe.log("building(走り出し)=" + DfBool(gSDK->IsCurrentlyBuildingAnUndoEvent()));
			const size_t before = DfOpenFiles().size();
			(void)gSDK->OpenDocumentPath(nullptr, false);
			if (DfOpenFiles().size() != before + 1)
			{
				probe.fail("自分用の新規図面を開けなかった");
				return;
			}
			st.fileRef = DfActiveRef();
			st.startedAt = DfNow();
			probe.log("自分用の図面 fileRef=" + std::to_string(st.fileRef));

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
			DfSave(slug, st);
			DfScheduleBringFront(st.fileRef);
			probe.log("状態を保存した: " + DfStatePath(slug).string());
			probe.log("→ 何秒か置いてから、**同じ図面のまま**もう一度走らせる"
					  "（ブリッジなら new_document=false）。2 回目で T2 を読んで比べる。");
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
			const std::string c = t2.count(k) ? t2[k] : "(読めない)";
			const bool diff = (a != b) || (a != DfEscape(c));
			if (diff)
				++changed;
			probe.log(std::string(diff ? "★ " : "   ") + k + "  T0=" + DfShort(a) +
					  (a == b ? "" : "  T1=" + DfShort(b)) +
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
