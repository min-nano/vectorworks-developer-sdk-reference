//
//	probes/runtime/plugin-folder-extras/probe.cpp
//
//	[issue #277] **Plug-ins の下のプラグインのフォルダにある、モジュール以外のファイルと
//	サブフォルダを VW がどう扱うか**と、**モジュールを置いた場所からそのまま読み込むか**を測る。
//	起動をまたいで比べるので、**1 回走らせて試料を作り、VW を再起動しては走らせる**を 2 回以上
//	繰り返す（状態は一時ディレクトリの台帳で受け渡す）。
//
//	手順（どれも結果をログに書いてから次へ進む）:
//	  1. 素性——pid・プロセスの起動時刻。これまでに走らせた VW のプロセスの一覧（台帳）。
//	  2. 読み込み元——利用者の Plug-ins の下から読み込まれているモジュールを列挙し、
//	     mac は `_dyld_get_image_name`・`dladdr`（イメージの先頭番地）・`realpath`・
//	     写像されているファイルの inode（`PROC_PIDREGIONPATHINFO`）とそのパスの `stat` の inode、
//	     Windows は `GetModuleFileNameW`・`GetModuleHandleExW(FROM_ADDRESS)` が返すもの・
//	     `GetMappedFileNameW`・そのパスを書き込みで開けるか（写像中なら共有違反になる）を出す。
//	     本体（このプローブ）の `dladdr` / `GetModuleFileNameW` も出す。
//	  3. 試料——`<Plug-ins>/cli/p277/` に、cli の段 3 の形を写したもの（`bin/` の実行ファイル・
//	     `hooks/` の `.sh` / `.ps1`・`build.json`・`vw2026-install.json`・`spool/` の
//	     `bridge.lock` / `*.req.json` / `*.res.json` / `*.wait` / `*.tmp`）を置く。無ければ作り、
//	     あれば台帳と比べる（有無・inode / ファイル ID・大きさ・更新時刻・中身のハッシュ・
//	     mac は mode）。cli の実物（`cli/bin/*`・`cli/build.json` など。spool は除く）も同じく
//	     比べる——こちらは cli の install で変わりうる。
//	     （cli の `spool/` に試料を入れないのは、cli が `*.req.json` を処理してしまうため。）
//	  4. 開いているもの——mac はプロセスのファイル記述子と写像を列挙し、Plug-ins の下のものを出す。
//	     Windows は試料のファイルとフォルダを共有なし（share 0）で開けるか、試料のフォルダの名前を
//	     付け替えて戻せるか、cli のフォルダの名前を付け替えられるか（モジュールを留めているので
//	     変えられない見込み。変わってしまったら直ちに戻す）を試す。
//	  5. 警告——`VW User Log.txt` / `VW User Log Sent.txt` から、今のセッションの
//	     `Dialog` の行・INFO 以外の行・試料や cli に触れていそうな行を出す。
//
//	図面には触らない（新規の空図面で走らせる。ブリッジの既定）。
//

#include "Probe.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#	include <psapi.h>
#else
#	include <dlfcn.h>
#	include <libproc.h>
#	include <mach-o/dyld.h>
#	include <sys/proc_info.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	include <climits>
#	include <cstdlib>
#endif

namespace
{
	namespace fs = std::filesystem;

	const char* const kPfeFixtureName = "p277";

	std::string PfeU8(const fs::path& p)
	{
#if GS_WIN
		const std::wstring& w = p.native();
		if (w.empty())
			return std::string();
		int n =
			WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s((size_t)(n > 0 ? n : 0), '\0');
		if (n > 0)
			WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr,
								nullptr);
		return s;
#else
		return p.native();
#endif
	}

	std::string PfeRel(const fs::path& base, const fs::path& p)
	{
		std::error_code ec;
		fs::path r = fs::relative(p, base, ec);
		std::string s = PfeU8(ec ? p : r);
		for (char& c : s)
			if (c == '\\')
				c = '/';
		return s;
	}

	std::string PfeLower(std::string s)
	{
		for (char& c : s)
			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
		return s;
	}

	bool PfeUnder(const std::string& path, const std::string& dir)
	{
		std::string p = PfeLower(path), d = PfeLower(dir);
		for (char& c : p)
			if (c == '\\')
				c = '/';
		for (char& c : d)
			if (c == '\\')
				c = '/';
		if (!d.empty() && d.back() != '/')
			d += '/';
		return p.compare(0, d.size(), d) == 0;
	}

	std::string PfeHex(uint64_t v)
	{
		char b[32];
		std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
		return b;
	}

	std::string PfeTime(int64_t unixSec)
	{
		std::time_t t = (std::time_t)unixSec;
		std::tm tmv{};
#if GS_WIN
		localtime_s(&tmv, &t);
#else
		localtime_r(&t, &tmv);
#endif
		char b[64];
		std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
		return b;
	}

	// 中身のハッシュ（FNV-1a 64）。読めなければ 0。
	uint64_t PfeHash(const fs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		if (!in)
			return 0;
		uint64_t h = 1469598103934665603ULL;
		char buf[65536];
		while (in)
		{
			in.read(buf, sizeof buf);
			std::streamsize n = in.gcount();
			for (std::streamsize i = 0; i < n; ++i)
			{
				h ^= (unsigned char)buf[i];
				h *= 1099511628211ULL;
			}
		}
		return h;
	}

	struct PfeStat
	{
		bool ok = false;
		bool dir = false;
		uint64_t size = 0;
		int64_t mtimeNs = 0; // unix 紀元からの ns
		std::string id;		 // mac は dev:ino、Windows は volume:fileindex
		std::string mode;	 // mac のみ（8 進）
	};

	PfeStat PfeStatOf(const fs::path& p)
	{
		PfeStat s;
#if GS_WIN
		HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
							   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
							   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return s;
		BY_HANDLE_FILE_INFORMATION fi{};
		if (GetFileInformationByHandle(h, &fi))
		{
			s.ok = true;
			s.dir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			s.size = ((uint64_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
			uint64_t ft = ((uint64_t)fi.ftLastWriteTime.dwHighDateTime << 32) |
						  fi.ftLastWriteTime.dwLowDateTime;
			s.mtimeNs = ((int64_t)ft - 116444736000000000LL) * 100;
			char b[64];
			std::snprintf(b, sizeof b, "%08lx:%08lx%08lx", (unsigned long)fi.dwVolumeSerialNumber,
						  (unsigned long)fi.nFileIndexHigh, (unsigned long)fi.nFileIndexLow);
			s.id = b;
		}
		CloseHandle(h);
#else
		struct stat st;
		if (::lstat(p.c_str(), &st) != 0)
			return s;
		s.ok = true;
		s.dir = S_ISDIR(st.st_mode);
		s.size = (uint64_t)st.st_size;
		s.mtimeNs = (int64_t)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
		s.id = std::to_string((long long)st.st_dev) + ":" + std::to_string((long long)st.st_ino);
		char b[16];
		std::snprintf(b, sizeof b, "%o", (unsigned)(st.st_mode & 07777));
		s.mode = b;
#endif
		return s;
	}

	std::string PfeMtime(int64_t ns)
	{
		int64_t sec = ns / 1000000000LL;
		char b[32];
		std::snprintf(b, sizeof b, ".%09lld", (long long)(ns % 1000000000LL));
		return PfeTime(sec) + b;
	}

	// ---- 1. 素性 ---------------------------------------------------------------------------

	int64_t PfeProcessStart()
	{
#if GS_WIN
		FILETIME c{}, e{}, k{}, u{};
		if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
			return 0;
		uint64_t ft = ((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
		return (int64_t)((ft - 116444736000000000ULL) / 10000000ULL);
#else
		struct proc_bsdinfo bi;
		if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &bi, sizeof bi) != (int)sizeof bi)
			return 0;
		return (int64_t)bi.pbi_start_tvsec;
#endif
	}

	long PfePid()
	{
#if GS_WIN
		return (long)GetCurrentProcessId();
#else
		return (long)getpid();
#endif
	}

	// ---- 2. 読み込み元 ---------------------------------------------------------------------

	struct PfeModule
	{
		std::string path;
		std::vector<std::string> notes;
	};

#if GS_WIN
	std::string PfeWinErr(DWORD e)
	{
		return "GetLastError=" + std::to_string((unsigned long)e);
	}

	std::string PfeModulePath(HMODULE h)
	{
		std::wstring buf(32768, L'\0');
		DWORD n = GetModuleFileNameW(h, buf.data(), (DWORD)buf.size());
		buf.resize(n);
		return PfeU8(fs::path(buf));
	}
#endif

	// 利用者の Plug-ins の下から読み込まれたモジュール。plugIns は殻（VwSdkProbes）の場所から求める。
	std::vector<PfeModule> PfeModules(std::string& plugIns, vwprobe::Report& probe)
	{
		std::vector<PfeModule> out;
		std::vector<std::string> all;
#if GS_WIN
		std::vector<HMODULE> mods(4096);
		DWORD need = 0;
		if (!K32EnumProcessModules(GetCurrentProcess(), mods.data(),
								   (DWORD)(mods.size() * sizeof(HMODULE)), &need))
		{
			probe.log("K32EnumProcessModules が失敗: " + PfeWinErr(GetLastError()));
			return out;
		}
		mods.resize(need / sizeof(HMODULE));
		for (HMODULE h : mods)
		{
			std::string p = PfeModulePath(h);
			if (plugIns.empty() && PfeLower(fs::path(p).filename().string()) == "vwsdkprobes.vlb")
			{
				// Windows の殻は <Plug-ins>/VwSdkProbes/VwSdkProbes.vlb と 1 段深い。
				fs::path dir = fs::path(p).parent_path();
				if (PfeLower(PfeU8(dir.filename())) == "vwsdkprobes")
					dir = dir.parent_path();
				plugIns = PfeU8(dir);
			}
		}
		probe.log("プロセスのモジュール: " + std::to_string(mods.size()) + " 個");
		for (HMODULE h : mods)
		{
			std::string p = PfeModulePath(h);
			if (plugIns.empty() || !PfeUnder(p, plugIns))
				continue;
			PfeModule m;
			m.path = p;
			m.notes.push_back("HMODULE " + PfeHex((uint64_t)(uintptr_t)h));
			// モジュールの中の番地から引き直す（cli がやるのと同じ道）。
			HMODULE self = nullptr;
			const BYTE* inside = reinterpret_cast<const BYTE*>(h) + 0x1000;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
									   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								   reinterpret_cast<LPCWSTR>(inside), &self))
				m.notes.push_back(std::string("GetModuleHandleExW(FROM_ADDRESS)=") +
								  (self == h
									   ? "同じ HMODULE"
									   : "別の HMODULE " + PfeHex((uint64_t)(uintptr_t)self)) +
								  " → GetModuleFileNameW \"" + PfeModulePath(self) + "\"");
			else
				m.notes.push_back("GetModuleHandleExW(FROM_ADDRESS) が失敗: " +
								  PfeWinErr(GetLastError()));
			std::wstring mapped(32768, L'\0');
			DWORD mn =
				K32GetMappedFileNameW(GetCurrentProcess(), h, mapped.data(), (DWORD)mapped.size());
			mapped.resize(mn);
			m.notes.push_back("GetMappedFileNameW \"" + PfeU8(fs::path(mapped)) + "\"");
			fs::path fp;
			{
				std::wstring buf(32768, L'\0');
				DWORD n = GetModuleFileNameW(h, buf.data(), (DWORD)buf.size());
				buf.resize(n);
				fp = fs::path(buf);
			}
			PfeStat st = PfeStatOf(fp);
			m.notes.push_back("そのパスのファイル ID " +
							  (st.ok ? st.id : std::string("(読めない)")) + "・" +
							  std::to_string((unsigned long long)st.size) + " バイト");
			HANDLE w = CreateFileW(fp.c_str(), GENERIC_WRITE,
								   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
								   OPEN_EXISTING, 0, nullptr);
			if (w == INVALID_HANDLE_VALUE)
				m.notes.push_back("そのパスを書き込みで開く: 失敗 " + PfeWinErr(GetLastError()) +
								  "（32 なら写像中＝このファイルが読み込まれている）");
			else
			{
				m.notes.push_back("そのパスを書き込みで開く: 開けた（写像されていない？）");
				CloseHandle(w);
			}
			out.push_back(m);
		}
		{
			HMODULE self = nullptr;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
									   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								   reinterpret_cast<LPCWSTR>(&PfeHash), &self))
				probe.log("本体（このプローブ）: \"" + PfeModulePath(self) + "\"");
		}
#else
		uint32_t n = _dyld_image_count();
		for (uint32_t i = 0; i < n; ++i)
		{
			const char* name = _dyld_get_image_name(i);
			if (!name)
				continue;
			std::string p = name;
			const std::string tail = "/VwSdkProbes.vwlibrary/Contents/MacOS/VwSdkProbes";
			if (plugIns.empty() && p.size() > tail.size() &&
				p.compare(p.size() - tail.size(), tail.size(), tail) == 0)
				plugIns = p.substr(0, p.size() - tail.size());
		}
		probe.log("プロセスのイメージ: " + std::to_string(n) + " 個");
		for (uint32_t i = 0; i < n; ++i)
		{
			const char* name = _dyld_get_image_name(i);
			if (!name || plugIns.empty() || !PfeUnder(name, plugIns))
				continue;
			PfeModule m;
			m.path = name;
			const void* header = _dyld_get_image_header(i);
			m.notes.push_back("番地 " + PfeHex((uint64_t)(uintptr_t)header) + "・slide " +
							  PfeHex((uint64_t)_dyld_get_image_vmaddr_slide(i)));
			Dl_info info{};
			if (dladdr(header, &info) && info.dli_fname)
				m.notes.push_back(std::string("dladdr(先頭番地).dli_fname \"") + info.dli_fname +
								  "\"");
			else
				m.notes.push_back("dladdr が失敗");
			char real[PATH_MAX];
			if (::realpath(name, real))
				m.notes.push_back(std::string("realpath \"") + real + "\"");
			struct proc_regionwithpathinfo rp;
			std::memset(&rp, 0, sizeof rp);
			if (proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO, (uint64_t)(uintptr_t)header, &rp,
							 sizeof rp) == (int)sizeof rp)
				m.notes.push_back(std::string("写像のファイル \"") + rp.prp_vip.vip_path +
								  "\" inode " +
								  std::to_string((long long)rp.prp_vip.vip_vi.vi_stat.vst_ino));
			else
				m.notes.push_back("PROC_PIDREGIONPATHINFO が失敗");
			PfeStat st = PfeStatOf(fs::path(name));
			m.notes.push_back("そのパスの stat dev:inode " +
							  (st.ok ? st.id : std::string("(読めない)")) + "・" +
							  std::to_string((unsigned long long)st.size) + " バイト");
			out.push_back(m);
		}
		{
			Dl_info info{};
			if (dladdr(reinterpret_cast<const void*>(&PfeHash), &info) && info.dli_fname)
				probe.log(std::string("本体（このプローブ）: \"") + info.dli_fname + "\"");
		}
#endif
		return out;
	}

	// ---- 3. 試料 ---------------------------------------------------------------------------

	bool PfeWrite(const fs::path& p, const std::string& text, vwprobe::Report& probe)
	{
		std::ofstream o(p, std::ios::binary | std::ios::trunc);
		o << text;
		o.close();
		if (!o)
		{
			probe.log("書けなかった: " + PfeU8(p));
			return false;
		}
		return true;
	}

	bool PfeMakeFixture(const fs::path& f, vwprobe::Report& probe)
	{
		std::error_code ec;
		fs::create_directories(f / "bin", ec);
		fs::create_directories(f / "hooks", ec);
		fs::create_directories(f / "spool", ec);
		if (ec)
		{
			probe.log("フォルダを作れなかった: " + ec.message());
			return false;
		}
		bool ok = true;
#if GS_WIN
		wchar_t sys[MAX_PATH];
		GetSystemDirectoryW(sys, MAX_PATH);
		fs::path exe = fs::path(sys) / L"whoami.exe";
		fs::copy_file(exe, f / "bin" / "vw2026.exe", fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			probe.log("実行ファイルを写せなかった: " + PfeU8(exe) + " " + ec.message());
			ok = false;
		}
#else
		fs::copy_file("/bin/echo", f / "bin" / "vw2026", fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			probe.log("実行ファイルを写せなかった: /bin/echo " + ec.message());
			ok = false;
		}
		::chmod((f / "bin" / "vw2026").c_str(), 0755);
#endif
		ok &= PfeWrite(f / "hooks" / "post-install.sh", "#!/bin/sh\necho p277 hook\n", probe);
		ok &= PfeWrite(f / "hooks" / "post-install.ps1", "Write-Output 'p277 hook'\r\n", probe);
#if !GS_WIN
		::chmod((f / "hooks" / "post-install.sh").c_str(), 0755);
#endif
		ok &= PfeWrite(f / "build.json", "{\"version\":\"p277\",\"branch\":\"fixture\"}\n", probe);
		ok &= PfeWrite(f / "vw2026-install.json",
					   "{\"plugins\":\"p277\",\"note\":\"issue 277 fixture\"}\n", probe);
		ok &= PfeWrite(f / "spool" / "bridge.lock", "", probe);
		ok &= PfeWrite(f / "spool" / "p277-0001.req.json",
					   "{\"id\":\"p277-0001\",\"method\":\"ping\"}\n", probe);
		ok &= PfeWrite(f / "spool" / "p277-0001.res.json", "{\"id\":\"p277-0001\",\"result\":{}}\n",
					   probe);
		ok &= PfeWrite(f / "spool" / "p277-0002.wait", "", probe);
		ok &= PfeWrite(f / "spool" / "p277-0002.res.json.tmp", "{\"id\":\"p277-0002\"", probe);
		return ok;
	}

	struct PfeEntry
	{
		std::string kind; // fixture / cli
		std::string rel;  // Plug-ins からの相対
		std::string type; // file / dir
		uint64_t size = 0;
		int64_t mtimeNs = 0;
		std::string id;
		std::string mode;
		uint64_t hash = 0;
	};

	PfeEntry PfeEntryOf(const std::string& kind, const fs::path& plugIns, const fs::path& p)
	{
		PfeEntry e;
		e.kind = kind;
		e.rel = PfeRel(plugIns, p);
		PfeStat st = PfeStatOf(p);
		e.type = st.ok ? (st.dir ? "dir" : "file") : "missing";
		e.size = st.dir ? 0 : st.size;
		e.mtimeNs = st.mtimeNs;
		e.id = st.id;
		e.mode = st.mode.empty() ? "-" : st.mode;
		if (st.ok && !st.dir)
			e.hash = PfeHash(p);
		return e;
	}

	// 今の状態。試料はフォルダごと、cli の実物は spool と cli.vwlibrary / cli.vlb 以外の 1・2 階層。
	std::vector<PfeEntry> PfeSnapshot(const fs::path& plugIns, const fs::path& cli,
									  const fs::path& fixture)
	{
		std::vector<PfeEntry> out;
		std::error_code ec;
		out.push_back(PfeEntryOf("fixture", plugIns, fixture));
		for (fs::recursive_directory_iterator it(fixture, ec), end; !ec && it != end;
			 it.increment(ec))
			out.push_back(PfeEntryOf("fixture", plugIns, it->path()));
		for (fs::directory_iterator it(cli, ec), end; !ec && it != end; it.increment(ec))
		{
			std::string name = PfeLower(PfeU8(it->path().filename()));
			if (name == kPfeFixtureName || name == "spool" || name == "cli.vwlibrary" ||
				name == "cli.vlb" || name == "cli.vwr")
				continue;
			out.push_back(PfeEntryOf("cli", plugIns, it->path()));
			std::error_code ec2;
			if (fs::is_directory(it->path(), ec2))
				for (fs::directory_iterator jt(it->path(), ec2), jend; !ec2 && jt != jend;
					 jt.increment(ec2))
					out.push_back(PfeEntryOf("cli", plugIns, jt->path()));
		}
		return out;
	}

	std::string PfeLine(const PfeEntry& e)
	{
		return e.kind + "\t" + e.rel + "\t" + e.type + "\t" +
			   std::to_string((unsigned long long)e.size) + "\t" +
			   std::to_string((long long)e.mtimeNs) + "\t" + e.id + "\t" + e.mode + "\t" +
			   PfeHex(e.hash);
	}

	bool PfeParse(const std::string& line, PfeEntry& e)
	{
		std::vector<std::string> f;
		std::string cur;
		for (char c : line)
		{
			if (c == '\t')
			{
				f.push_back(cur);
				cur.clear();
			}
			else if (c != '\r')
				cur += c;
		}
		f.push_back(cur);
		if (f.size() != 8)
			return false;
		e.kind = f[0];
		e.rel = f[1];
		e.type = f[2];
		e.size = std::strtoull(f[3].c_str(), nullptr, 10);
		e.mtimeNs = std::strtoll(f[4].c_str(), nullptr, 10);
		e.id = f[5];
		e.mode = f[6];
		e.hash = std::strtoull(f[7].c_str(), nullptr, 16);
		return true;
	}

	fs::path PfeTempFile(const char* name)
	{
		std::error_code ec;
		fs::path dir = fs::temp_directory_path(ec);
		if (ec)
			dir = fs::path(".");
		return dir / name;
	}

	void PfeCompare(const std::vector<PfeEntry>& before, const std::vector<PfeEntry>& now,
					vwprobe::Report& probe)
	{
		std::map<std::string, PfeEntry> b, n;
		for (const auto& e : before)
			b[e.rel] = e;
		for (const auto& e : now)
			n[e.rel] = e;
		int same = 0, changed = 0;
		for (const auto& [rel, e] : b)
		{
			auto it = n.find(rel);
			if (it == n.end() || it->second.type == "missing")
			{
				probe.log("  ✗ 無くなった: [" + e.kind + "] " + rel);
				++changed;
				continue;
			}
			const PfeEntry& x = it->second;
			std::vector<std::string> d;
			if (x.type != e.type)
				d.push_back("種類 " + e.type + "→" + x.type);
			if (x.id != e.id)
				d.push_back("ID " + e.id + "→" + x.id);
			if (e.type == "file" && x.size != e.size)
				d.push_back("大きさ " + std::to_string((unsigned long long)e.size) + "→" +
							std::to_string((unsigned long long)x.size));
			if (x.mtimeNs != e.mtimeNs)
				d.push_back("更新時刻 " + PfeMtime(e.mtimeNs) + "→" + PfeMtime(x.mtimeNs));
			if (e.type == "file" && x.hash != e.hash)
				d.push_back("中身 " + PfeHex(e.hash) + "→" + PfeHex(x.hash));
			if (x.mode != e.mode)
				d.push_back("mode " + e.mode + "→" + x.mode);
			if (d.empty())
			{
				++same;
				continue;
			}
			++changed;
			std::string s = "  ✗ 変わった: [" + e.kind + "] " + rel + ":";
			for (const auto& x2 : d)
				s += " " + x2 + ";";
			probe.log(s);
		}
		for (const auto& [rel, x] : n)
			if (b.find(rel) == b.end())
			{
				probe.log("  ＋ 増えた: [" + x.kind + "] " + rel + " (" + x.type + ")");
				++changed;
			}
		probe.log("比べた結果: 同じ " + std::to_string(same) + " 件・違い " +
				  std::to_string(changed) + " 件");
	}

	// ---- 4. 開いているもの -----------------------------------------------------------------

#if GS_WIN
	void PfeExclusiveOpen(const fs::path& plugIns, const fs::path& p, vwprobe::Report& probe)
	{
		std::error_code ec;
		bool dir = fs::is_directory(p, ec);
		HANDLE h = CreateFileW(p.c_str(), dir ? FILE_LIST_DIRECTORY : GENERIC_READ, 0, nullptr,
							   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			probe.log("  共有なしで開けない: " + PfeRel(plugIns, p) + " " +
					  PfeWinErr(GetLastError()) + "（32 なら誰かが開いている）");
		else
		{
			probe.log("  共有なしで開けた: " + PfeRel(plugIns, p));
			CloseHandle(h);
		}
	}

	void PfeRenameBack(const fs::path& plugIns, const fs::path& p, vwprobe::Report& probe)
	{
		fs::path to = p;
		to += L".p277-renamed";
		if (!MoveFileExW(p.c_str(), to.c_str(), 0))
		{
			probe.log("  名前を変えられない: " + PfeRel(plugIns, p) + " " +
					  PfeWinErr(GetLastError()));
			return;
		}
		if (!MoveFileExW(to.c_str(), p.c_str(), 0))
		{
			probe.fail("名前を戻せなかった: " + PfeU8(to) + " → " + PfeU8(p) + " " +
					   PfeWinErr(GetLastError()) + "（手で戻すこと）");
			return;
		}
		probe.log("  名前を変えて戻せた: " + PfeRel(plugIns, p));
	}
#else
	void PfeOpenFiles(const std::string& plugIns, vwprobe::Report& probe)
	{
		pid_t pid = getpid();
		int bytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
		std::vector<struct proc_fdinfo> fds(
			(size_t)(bytes > 0 ? bytes : 0) / sizeof(struct proc_fdinfo) + 64);
		bytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(),
							 (int)(fds.size() * sizeof(struct proc_fdinfo)));
		size_t count = bytes > 0 ? (size_t)bytes / sizeof(struct proc_fdinfo) : 0;
		int vnodes = 0, under = 0;
		for (size_t i = 0; i < count; ++i)
		{
			if (fds[i].proc_fdtype != PROX_FDTYPE_VNODE)
				continue;
			struct vnode_fdinfowithpath vi;
			std::memset(&vi, 0, sizeof vi);
			if (proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO, &vi, sizeof vi) !=
				(int)sizeof vi)
				continue;
			++vnodes;
			if (!PfeUnder(vi.pvip.vip_path, plugIns))
				continue;
			++under;
			probe.log("  fd " + std::to_string(fds[i].proc_fd) + ": " +
					  PfeRel(plugIns, fs::path(vi.pvip.vip_path)));
		}
		probe.log("ファイル記述子: 全 " + std::to_string(count) + "・vnode " +
				  std::to_string(vnodes) + "・うち Plug-ins の下 " + std::to_string(under));

		// 写像（モジュールのほかに Plug-ins の下のファイルを写像していないか）。
		std::set<std::string> mapped;
		uint64_t addr = 0;
		int regions = 0;
		for (;;)
		{
			struct proc_regionwithpathinfo rp;
			std::memset(&rp, 0, sizeof rp);
			int r = proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, addr, &rp, sizeof rp);
			if (r != (int)sizeof rp)
				break;
			++regions;
			if (rp.prp_vip.vip_path[0] && PfeUnder(rp.prp_vip.vip_path, plugIns))
				mapped.insert(rp.prp_vip.vip_path);
			uint64_t next = rp.prp_prinfo.pri_address + rp.prp_prinfo.pri_size;
			if (next <= addr)
				break;
			addr = next;
		}
		probe.log("写像: " + std::to_string(regions) + " 領域・Plug-ins の下のファイル " +
				  std::to_string(mapped.size()) + " 個");
		for (const auto& m : mapped)
			probe.log("  " + PfeRel(plugIns, fs::path(m)));
	}
#endif

	// ---- 5. 警告 ---------------------------------------------------------------------------

	std::string PfeField(const std::string& line, const char* key)
	{
		std::string k = std::string("\"") + key + "\":\"";
		size_t p = line.find(k);
		if (p == std::string::npos)
			return std::string();
		p += k.size();
		size_t q = line.find('"', p);
		return line.substr(p, q == std::string::npos ? std::string::npos : q - p);
	}

	std::string PfeClip(const std::string& s, size_t max)
	{
		if (s.size() <= max)
			return s;
		size_t n = max;
		while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
			--n;
		return s.substr(0, n) + "…";
	}

	void PfeUserLog(const fs::path& userDir, vwprobe::Report& probe)
	{
		fs::path cur = userDir / "VW User Log.txt";
		fs::path sent = userDir / "VW User Log Sent.txt";
		std::string session;
		{
			std::ifstream in(cur, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				std::string s = PfeField(line, "session");
				if (!s.empty())
					session = s;
			}
		}
		if (session.empty())
		{
			probe.log("VW User Log.txt から今のセッションを読めなかった: " + PfeU8(cur));
			return;
		}
		probe.log("今のセッション: " + session);
		const char* needles[] = {
			"p277",			 "vw2026",	  "hooks",			 "bridge.lock", "build.json",
			".req.json",	 ".res.json", "spool",			 "/cli/",		"\\\\cli\\\\",
			"cli.vwlibrary", "cli.vlb",	  "UnknownDeveloper"};
		for (const fs::path& f : {sent, cur})
		{
			std::ifstream in(f, std::ios::binary);
			if (!in)
			{
				probe.log("  （" + PfeU8(f.filename()) + " が無い）");
				continue;
			}
			std::string line;
			int total = 0, shown = 0, dialogs = 0, notInfo = 0;
			std::map<std::string, int> types;
			while (std::getline(in, line))
			{
				if (PfeField(line, "session") != session)
					continue;
				++total;
				std::string type = PfeField(line, "type");
				types[type.empty() ? "(なし)" : type]++;
				bool hit = line.find("Dialog") != std::string::npos;
				if (hit)
					++dialogs;
				bool odd = !type.empty() && type != "INFO";
				if (odd)
					++notInfo;
				for (const char* nd : needles)
					if (line.find(nd) != std::string::npos)
						hit = true;
				if ((hit || odd) && shown < 40)
				{
					++shown;
					size_t c = line.find("\"cat\"");
					probe.log("  " + PfeU8(f.filename()) + ": " +
							  PfeClip(c == std::string::npos ? line : line.substr(c), 260));
				}
			}
			std::string ts;
			for (const auto& [t, k] : types)
				ts += " " + t + "=" + std::to_string(k);
			probe.log(PfeU8(f.filename()) + ": このセッションの行 " + std::to_string(total) +
					  "（type 別:" + ts + "）・Dialog を含む行 " + std::to_string(dialogs) +
					  "・INFO 以外 " + std::to_string(notInfo));
		}
	}
} // namespace

VW_PROBE("plugin-folder-extras", "プラグインのフォルダの付属物", "試料の変化と読み込み元を見る")
{
	// 1. 素性
	int64_t start = PfeProcessStart();
	probe.log("pid " + std::to_string(PfePid()) + "・プロセスの起動 " + PfeTime(start));

	// 2. 読み込み元
	probe.log("== 読み込み元 ==");
	std::string plugInsU8;
	std::vector<PfeModule> mods = PfeModules(plugInsU8, probe);
	if (plugInsU8.empty())
	{
		probe.fail("殻（VwSdkProbes）の場所から Plug-ins を求められなかった");
		return;
	}
	probe.log("利用者の Plug-ins: \"" + plugInsU8 + "\"");
	for (const auto& m : mods)
	{
		probe.log("● \"" + m.path + "\"");
		for (const auto& n : m.notes)
			probe.log("    " + n);
	}

#if GS_WIN
	fs::path plugIns;
	{
		int n =
			MultiByteToWideChar(CP_UTF8, 0, plugInsU8.c_str(), (int)plugInsU8.size(), nullptr, 0);
		std::wstring w((size_t)n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, plugInsU8.c_str(), (int)plugInsU8.size(), w.data(), n);
		plugIns = fs::path(w);
	}
#else
	fs::path plugIns(plugInsU8);
#endif
	fs::path cli = plugIns / "cli";
	fs::path fixture = cli / kPfeFixtureName;
	std::error_code ec;
	if (!fs::is_directory(cli, ec))
	{
		probe.fail("cli のフォルダが無い: " + PfeU8(cli) + "（試料はその中に置く）");
		return;
	}

	// 1'. 台帳（これまでに走らせたプロセス）
	fs::path runsPath = PfeTempFile("VwSdkProbes-plugin-folder-extras.runs.txt");
	fs::path manifestPath = PfeTempFile("VwSdkProbes-plugin-folder-extras.manifest.txt");
	bool haveFixture = fs::is_directory(fixture, ec);
	bool haveManifest = fs::exists(manifestPath, ec);
	if (!haveFixture || !haveManifest)
	{
		probe.log("== 試料を作る ==");
		if (!haveFixture && !PfeMakeFixture(fixture, probe))
		{
			probe.fail("試料を作れなかった");
			return;
		}
		if (haveFixture)
			probe.log("試料はあったが台帳が無い——今の状態で台帳を作り直す");
		std::vector<PfeEntry> snap = PfeSnapshot(plugIns, cli, fixture);
		std::ofstream o(manifestPath, std::ios::binary | std::ios::trunc);
		for (const auto& e : snap)
		{
			o << PfeLine(e) << "\n";
			probe.log("  " + PfeLine(e));
		}
		std::ofstream r(runsPath, std::ios::binary | std::ios::trunc);
		r << "setup\t" << PfePid() << "\t" << start << "\n";
		probe.log("台帳: " + PfeU8(manifestPath) + "（" + std::to_string(snap.size()) +
				  " 件）。VW を再起動してから、もう一度走らせる");
	}
	else
	{
		std::vector<PfeEntry> before;
		{
			std::ifstream in(manifestPath, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				PfeEntry e;
				if (PfeParse(line, e))
					before.push_back(e);
			}
		}
		{
			std::ofstream r(runsPath, std::ios::binary | std::ios::app);
			r << "verify\t" << PfePid() << "\t" << start << "\n";
		}
		std::set<std::string> procs;
		std::string setupProc;
		{
			std::ifstream in(runsPath, std::ios::binary);
			std::string line;
			probe.log("== 台帳の走行 ==");
			while (std::getline(in, line))
			{
				std::istringstream ss(line);
				std::string what, pid, st;
				std::getline(ss, what, '\t');
				std::getline(ss, pid, '\t');
				std::getline(ss, st, '\t');
				procs.insert(pid + "@" + st);
				if (what == "setup")
					setupProc = pid + "@" + st;
				probe.log("  " + what + " pid " + pid + "・起動 " +
						  PfeTime(std::strtoll(st.c_str(), nullptr, 10)));
			}
		}
		probe.log("試料を作ってから跨いだ起動: " +
				  std::to_string(procs.size() - (setupProc.empty() ? 0 : 1)) + " 回");
		probe.log("== 試料と cli の実物を台帳と比べる（" + std::to_string(before.size()) +
				  " 件）==");
		PfeCompare(before, PfeSnapshot(plugIns, cli, fixture), probe);
	}

	// 4. 開いているもの
	probe.log("== 開いているもの ==");
#if GS_WIN
	for (const auto& e : PfeSnapshot(plugIns, cli, fixture))
		PfeExclusiveOpen(plugIns, plugIns / fs::u8path(e.rel), probe);
	PfeExclusiveOpen(plugIns, cli / "spool", probe);
	PfeExclusiveOpen(plugIns, cli, probe);
	PfeRenameBack(plugIns, fixture / "spool", probe);
	PfeRenameBack(plugIns, fixture / "bin", probe);
	PfeRenameBack(plugIns, fixture / "hooks", probe);
	PfeRenameBack(plugIns, fixture, probe);
	if (fs::is_directory(cli / "bin", ec))
		PfeRenameBack(plugIns, cli / "bin", probe);
	PfeRenameBack(plugIns, cli, probe);
#else
	PfeOpenFiles(plugInsU8, probe);
#endif

	// 5. 警告
	probe.log("== VW User Log ==");
	PfeUserLog(plugIns.parent_path(), probe);
}
