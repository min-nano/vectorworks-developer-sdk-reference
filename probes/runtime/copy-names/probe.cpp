//
//	probes/runtime/copy-names/probe.cpp
//
//	[issue #326] コピーとして開く図面（古い版・読み取り専用・使用中）と無題の図面について、
//	同じ名前の 2 枚で GetOpenFilesList のパスが重なるか、fileRef で見分けて前面にできるかを測る。
//
//	古い版の見本は、一時ディレクトリの VwSdkProbes-copy-names-old.vwx があればそれを、
//	無ければアプリのフォルダの Libraries から版の古い小さな .vwx を探してコピーする。
//	使用中の段は、Windows ではプローブが FileShare.Read で握る。mac では外のプロセスに
//	握らせたファイルを、一時ディレクトリの VwSdkProbes-copy-names-held.txt に 1 行ずつ
//	書いておけば開く（無ければ飛ばす）。
//
//	どの図面も、開いた直後に固有の名前のレイヤを足して目印にする。後から SwitchToOpenFile で
//	前へ出し、目印のレイヤがあるかで「どの図面が前へ出たか」を確かめる。
//

#include "Probe.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#if GS_WIN
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#endif

namespace
{
	using namespace VectorWorks;
	namespace cnfs = std::filesystem;

	std::string CnFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	cnfs::path CnPath(const std::string& s)
	{
		return cnfs::path(std::u8string(s.begin(), s.end()));
	}

	std::string CnUtf8(const cnfs::path& p)
	{
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	struct CnDoc
	{
		Sint32 ref = -1;
		bool active = false;
		bool inMemoryOnly = false;
		std::string path;
	};

	std::vector<CnDoc> CnDocs()
	{
		std::vector<CnDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			CnDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			d.inMemoryOnly = files[i].fIsInMemoryOnly;
			TXString path;
			Filing::IFileIdentifier* pID = files[i].fpFileID;
			if (pID != nullptr)
				pID->GetFileFullPath(path);
			d.path = static_cast<const char*>(path);
			out.push_back(d);
		}
		return out;
	}

	Sint32 CnActiveRef()
	{
		for (const CnDoc& d : CnDocs())
		{
			if (d.active)
				return d.ref;
		}
		return -1;
	}

	void CnLogDocs(vwprobe::Report& probe, const std::string& tag)
	{
		const std::vector<CnDoc> docs = CnDocs();
		probe.log(tag + ": 件数=" + std::to_string(docs.size()));
		for (const CnDoc& d : docs)
		{
			int same = 0;
			for (const CnDoc& e : docs)
				same += e.path == d.path ? 1 : 0;
			probe.log(tag + ":   fileRef=" + std::to_string(d.ref) + " active=" + CnFlag(d.active) +
					  " inMemoryOnly=" + CnFlag(d.inMemoryOnly) + " path=" + d.path +
					  (same > 1 ? "  ★重なり" + std::to_string(same) : std::string()));
		}
	}

	// 開いた図面。ref は開いた直後の前面のもの、mark は足したレイヤの名前。
	struct CnOpened
	{
		Sint32 ref = -1;
		std::string mark;
		bool ok = false;
	};

	int gCnSerial = 0;

	CnOpened CnOpen(vwprobe::Report& probe, const std::string& label, const cnfs::path* file)
	{
		CnOpened o;
		const std::vector<CnDoc> before = CnDocs();
		bool ret = false;
		const auto t0 = std::chrono::steady_clock::now();
		if (file == nullptr)
		{
			ret = gSDK->OpenDocumentPath(nullptr, false);
		}
		else
		{
			Filing::IFileIdentifierPtr id(IID_FileIdentifier);
			id->Set(TXString(CnUtf8(*file).c_str()));
			ret = gSDK->OpenDocumentPath(id, false);
		}
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - t0)
							.count();
		std::string path;
		for (const CnDoc& d : CnDocs())
		{
			bool known = false;
			for (const CnDoc& e : before)
				known = known || e.ref == d.ref;
			if (!known && d.active)
			{
				o.ref = d.ref;
				o.ok = true;
				path = d.path + " inMemoryOnly=" + CnFlag(d.inMemoryOnly);
			}
		}
		probe.log("開く " + label + (file != nullptr ? " <- " + CnUtf8(*file) : std::string()) +
				  ": 戻り値=" + CnFlag(ret) + " (" + std::to_string(ms) + " ms) " +
				  (o.ok ? "fileRef=" + std::to_string(o.ref) + " path=" + path
						: std::string("新しい fileRef なし")));
		if (o.ok)
		{
			o.mark = "cn-" + std::to_string(++gCnSerial) + "-" + label;
			gSDK->CreateLayer(TXString(o.mark.c_str()), 1);
			gSDK->EndUndoEvent();
		}
		return o;
	}

	bool CnHasMark(const CnOpened& o)
	{
		return gSDK->GetNamedLayer(TXString(o.mark.c_str())) != nullptr;
	}

	void CnSwitchAll(vwprobe::Report& probe, const std::string& tag,
					 const std::vector<CnOpened>& docs)
	{
		// 開いた順と逆順。どちらでも「指した図面」が前へ出るか。
		for (int pass = 0; pass < 2; ++pass)
		{
			for (std::size_t k = 0; k < docs.size(); ++k)
			{
				const CnOpened& o = docs[pass == 0 ? k : docs.size() - 1 - k];
				if (!o.ok)
					continue;
				const bool ret = gSDK->SwitchToOpenFile(o.ref);
				std::string seen;
				for (const CnOpened& p : docs)
				{
					if (p.ok && CnHasMark(p))
						seen += (seen.empty() ? "" : ",") + p.mark;
				}
				const bool right = CnActiveRef() == o.ref && seen == o.mark;
				probe.log(tag + ": SwitchToOpenFile(" + std::to_string(o.ref) +
						  ") 戻り値=" + CnFlag(ret) + " 前面=" + std::to_string(CnActiveRef()) +
						  " 見えた目印=" + seen +
						  (right ? " 一致" : " ★不一致(期待 " + o.mark + ")"));
			}
		}
	}

	// ref の図面を前へ出し、目印を確かめてから CloseDocument()。
	void CnClose(vwprobe::Report& probe, CnOpened& o)
	{
		if (!o.ok)
			return;
		gSDK->SwitchToOpenFile(o.ref);
		if (CnActiveRef() != o.ref || !CnHasMark(o))
		{
			probe.fail("閉じる図面を前へ出せなかった: " + o.mark);
			return;
		}
		gSDK->CloseDocument();
		bool gone = true;
		for (const CnDoc& d : CnDocs())
			gone = gone && d.ref != o.ref;
		probe.log("閉じた " + o.mark + " fileRef=" + std::to_string(o.ref) +
				  " 消えた=" + CnFlag(gone));
		o.ok = !gone;
	}

	void CnCloseAll(vwprobe::Report& probe, std::vector<CnOpened>& docs)
	{
		for (std::size_t k = docs.size(); k > 0; --k)
			CnClose(probe, docs[k - 1]);
		docs.clear();
	}

	GSError CnSaveTo(const cnfs::path& to)
	{
		Filing::IFileIdentifierPtr id(IID_FileIdentifier);
		id->Set(TXString(CnUtf8(to).c_str()));
		return gSDK->SaveActiveDocumentPath(id);
	}

	// 先頭の 400 バイトから VW<版> を読む。0 = 読めない。
	int CnVersionOf(const cnfs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		std::string head(400, '\0');
		in.read(&head[0], 400);
		for (std::size_t at = head.find("VW"); at != std::string::npos;
			 at = head.find("VW", at + 1))
		{
			const int v = std::atoi(head.c_str() + at + 2);
			if (v >= 10 && v < 60)
				return v;
		}
		return 0;
	}

	cnfs::path CnFindOld(vwprobe::Report& probe, const cnfs::path& temp)
	{
		std::error_code ec;
		const cnfs::path given = temp / "VwSdkProbes-copy-names-old.vwx";
		if (cnfs::exists(given, ec))
			return given;
		Filing::IFolderIdentifierPtr folder(IID_FolderIdentifier);
		folder->Set(kApplicationFolder, false);
		TXString app;
		folder->GetFullPath(app);
		const cnfs::path root = CnPath(static_cast<const char*>(app)) / "Libraries";
		probe.log("古い版の見本を探す: " + CnUtf8(root));
		cnfs::path best;
		int bestVersion = 0;
		std::set<int> seen;
		cnfs::recursive_directory_iterator it(root, cnfs::directory_options::skip_permission_denied,
											  ec);
		for (const cnfs::recursive_directory_iterator end; !ec && it != end; it.increment(ec))
		{
			if (it->path().extension() != ".vwx")
				continue;
			std::error_code ec2;
			if (cnfs::file_size(it->path(), ec2) > 200000 || ec2)
				continue;
			const int v = CnVersionOf(it->path());
			seen.insert(v);
			if (v > bestVersion && v < 30)
			{
				best = it->path();
				bestVersion = v;
			}
		}
		std::string all;
		for (int v : seen)
			all += " " + std::to_string(v);
		probe.log("見た版:" + all + " / 選んだ版=" + std::to_string(bestVersion) + " " +
				  CnUtf8(best));
		return best;
	}

	void CnReadOnly(const cnfs::path& p, bool on)
	{
		std::error_code ec;
		cnfs::permissions(
			p, cnfs::perms::owner_write | cnfs::perms::group_write | cnfs::perms::others_write,
			on ? cnfs::perm_options::remove : cnfs::perm_options::add, ec);
	}

	// 同じ名前の 2 枚・別の名前・同じファイルをもう一度・先に開いた 1 枚を閉じて開き直す。
	void CnKind(vwprobe::Report& probe, const std::string& kind, const cnfs::path& a,
				const cnfs::path& b, const cnfs::path& other, const cnfs::path& saveTo)
	{
		probe.log("==== " + kind + " ====");
		std::vector<CnOpened> docs;
		docs.push_back(CnOpen(probe, "A", &a));
		docs.push_back(CnOpen(probe, "B", &b));
		if (!other.empty())
			docs.push_back(CnOpen(probe, "X", &other));
		docs.push_back(CnOpen(probe, "A2", &a));
		CnLogDocs(probe, kind + " 開いた後");
		CnSwitchAll(probe, kind, docs);

		// 先に開いた A を fileRef で指して閉じる。残りが期待どおりか。
		CnClose(probe, docs[0]);
		CnLogDocs(probe, kind + " A を閉じた後");
		CnSwitchAll(probe, kind + " A を閉じた後", docs);
		docs.push_back(CnOpen(probe, "A3", &a));
		CnLogDocs(probe, kind + " A を開き直した後");

		// B だけを新しいパスへ保存する（fileRef で指せるかの本番）。
		if (docs[1].ok)
		{
			gSDK->SwitchToOpenFile(docs[1].ref);
			if (CnActiveRef() == docs[1].ref && CnHasMark(docs[1]))
			{
				const GSError err = CnSaveTo(saveTo);
				probe.log(kind + ": B を保存 " + CnUtf8(saveTo) +
						  " GSError=" + std::to_string(static_cast<long>(err)) + " 前面=" +
						  std::to_string(CnActiveRef()) + " 目印=" + CnFlag(CnHasMark(docs[1])));
				CnLogDocs(probe, kind + " B を保存した後");
			}
		}
		CnCloseAll(probe, docs);
	}
} // namespace

VW_PROBE("copy-names", "コピーの図面のパスの重なり",
		 "同じ名前の 2 枚を開いてパスと fileRef を並べる")
{
	std::error_code ec;
	const cnfs::path temp = cnfs::temp_directory_path(ec);
	const long long stamp = static_cast<long long>(
		std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
	const cnfs::path base = temp / ("VwSdkProbes-copy-names-" + std::to_string(stamp));
	for (const char* d : {"a", "b", "out"})
		cnfs::create_directories(base / d, ec);
	probe.log("作業用: " + CnUtf8(base));
	CnLogDocs(probe, "走り出し");
	const Sint32 origin = CnActiveRef();

	// ---- 無題
	{
		probe.log("==== 無題 ====");
		std::vector<CnOpened> docs;
		for (const char* n : {"U1", "U2", "U3"})
			docs.push_back(CnOpen(probe, n, nullptr));
		CnLogDocs(probe, "無題 3 枚");
		CnSwitchAll(probe, "無題", docs);
		CnClose(probe, docs[1]);
		docs.push_back(CnOpen(probe, "U4", nullptr));
		CnLogDocs(probe, "無題 U2 を閉じて U4");
		CnClose(probe, docs[3]);
		docs.push_back(CnOpen(probe, "U5", nullptr));
		CnLogDocs(probe, "無題 U4 を閉じて U5");
		CnCloseAll(probe, docs);
		CnOpened again = CnOpen(probe, "U6", nullptr);
		CnLogDocs(probe, "無題 全部閉じて U6");
		// U6 を保存して閉じたものを、以後の見本にする。
		if (again.ok)
		{
			const GSError err = CnSaveTo(base / "a" / "ro.vwx");
			probe.log("見本を保存 GSError=" + std::to_string(static_cast<long>(err)));
		}
		CnClose(probe, again);
	}

	// ---- 読み取り専用・使用中の見本
	const cnfs::path ro = base / "a" / "ro.vwx";
	if (!cnfs::exists(ro, ec))
	{
		probe.fail("見本を保存できなかった");
		return;
	}
	cnfs::copy_file(ro, base / "b" / "ro.vwx", ec);
	cnfs::copy_file(ro, base / "a" / "other.vwx", ec);
	for (const char* d : {"a", "b"})
		cnfs::copy_file(ro, base / d / "held.vwx", ec);
	cnfs::copy_file(ro, base / "a" / "held2.vwx", ec);
	for (const cnfs::path& p : {ro, base / "b" / "ro.vwx", base / "a" / "other.vwx"})
		CnReadOnly(p, true);
	CnKind(probe, "読み取り専用", ro, base / "b" / "ro.vwx", base / "a" / "other.vwx",
		   base / "out" / "ro-saved.vwx");

	// ---- 使用中
#if GS_WIN
	{
		std::vector<HANDLE> held;
		for (const cnfs::path& p :
			 {base / "a" / "held.vwx", base / "b" / "held.vwx", base / "a" / "held2.vwx"})
		{
			HANDLE h = ::CreateFileW(p.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
									 FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
			probe.log("握る " + CnUtf8(p) + ": " + CnFlag(h != INVALID_HANDLE_VALUE));
			if (h != INVALID_HANDLE_VALUE)
				held.push_back(h);
		}
		CnKind(probe, "使用中", base / "a" / "held.vwx", base / "b" / "held.vwx",
			   base / "a" / "held2.vwx", base / "out" / "held-saved.vwx");
		for (HANDLE h : held)
			::CloseHandle(h);
	}
#else
	{
		std::ifstream in(temp / "VwSdkProbes-copy-names-held.txt");
		std::vector<cnfs::path> paths;
		for (std::string line; std::getline(in, line);)
		{
			if (!line.empty())
				paths.push_back(CnPath(line));
		}
		if (paths.size() >= 3)
			CnKind(probe, "使用中", paths[0], paths[1], paths[2], base / "out" / "held-saved.vwx");
		else
			probe.log("==== 使用中 ==== 見本なし（飛ばす）");
	}
#endif

	// ---- 古い版
	const cnfs::path old = CnFindOld(probe, temp);
	if (old.empty())
	{
		probe.fail("古い版の見本が無い");
	}
	else
	{
		for (const char* d : {"a", "b"})
			cnfs::copy_file(old, base / d / "old.vwx", ec);
		cnfs::copy_file(old, base / "a" / "old2.vwx", ec);
		for (const cnfs::path& p :
			 {base / "a" / "old.vwx", base / "b" / "old.vwx", base / "a" / "old2.vwx"})
			CnReadOnly(p, false);
		CnKind(probe, "古い版", base / "a" / "old.vwx", base / "b" / "old.vwx",
			   base / "a" / "old2.vwx", base / "out" / "old-saved.vwx");
		// 古い版で、しかも読み取り専用
		for (const char* d : {"a", "b"})
		{
			cnfs::copy_file(old, base / d / "oldro.vwx", ec);
			CnReadOnly(base / d / "oldro.vwx", true);
		}
		CnKind(probe, "古い版・読み取り専用", base / "a" / "oldro.vwx", base / "b" / "oldro.vwx",
			   cnfs::path(), base / "out" / "oldro-saved.vwx");
	}

	if (origin >= 0)
		gSDK->SwitchToOpenFile(origin);
	CnLogDocs(probe, "終わり");
}
