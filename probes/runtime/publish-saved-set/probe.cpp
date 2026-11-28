//
//	probes/runtime/publish-saved-set/probe.cpp
//
//	[issue #332] Filing::IPublish で、保存したパブリッシュセットを PDF に書き出す。
//	GetPublishSavedSets の中身、PublishSavedSet（4 引数。fDoPDFOnly）が呼び出しの中で
//	書き終わるか・ダイアログで止まるか（所要で見分ける）・出力のファイル名・同じ出力先へ
//	2 回目を書いたとき・無いセットと書けない出力先・前面でない図面のセット・undo を測る。
//
//	パブリッシュセットは SDK から作れないので、セットを登録した図面を開いておき、既定の
//	new_document=true で走らせる（前面がブリッジの空図面、その後ろにセットのある図面）。
//	セットのある図面を前面にして new_document=false で走らせてもよい（前面でない図面の段は
//	飛ぶ）。セットのある図面のうち最初に見つけたものの、名前で並べて最初のセットを使う。
//	出力は一時ディレクトリの VwSdkProbes-publish/<時刻>/ の下。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IFolderIdentifier.h"
#include "Interfaces/VectorWorks/Filing/IPublish.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
	using namespace VectorWorks;
	namespace psfs = std::filesystem;

	std::string PsFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string PsUtf8(const psfs::path& p)
	{
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	std::string PsStr(const TXString& s)
	{
		return static_cast<const char*>(s);
	}

	struct PsDoc
	{
		Sint32 ref = -1;
		bool active = false;
		bool inMemoryOnly = false;
		std::string path;
	};

	std::vector<PsDoc> PsDocs()
	{
		std::vector<PsDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			PsDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			d.inMemoryOnly = files[i].fIsInMemoryOnly;
			TXString path;
			Filing::IFileIdentifier* pID = files[i].fpFileID;
			if (pID != nullptr)
				pID->GetFileFullPath(path);
			d.path = PsStr(path);
			out.push_back(d);
		}
		return out;
	}

	Sint32 PsActiveRef()
	{
		for (const PsDoc& d : PsDocs())
		{
			if (d.active)
				return d.ref;
		}
		return -1;
	}

	void PsLogDocs(vwprobe::Report& probe, const std::string& tag)
	{
		const std::vector<PsDoc> docs = PsDocs();
		probe.log(tag + ": 図面の数=" + std::to_string(docs.size()));
		for (const PsDoc& d : docs)
		{
			probe.log(tag + ":   fileRef=" + std::to_string(d.ref) + " active=" + PsFlag(d.active) +
					  " inMemoryOnly=" + PsFlag(d.inMemoryOnly) + " path=" + d.path);
		}
	}

	long long PsMs(std::chrono::steady_clock::time_point t0)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - t0)
			.count();
	}

	// 名前で並べたセットの一覧。
	struct PsSet
	{
		std::string name;
		Filing::TPublishSetInfoArr items;
	};

	std::vector<PsSet> PsReadSets(vwprobe::Report& probe, Filing::IPublishPtr& pub,
								  const std::string& tag)
	{
		std::vector<PsSet> out;
		Filing::TStrToPublishSetInfoUMap map;
		const auto t0 = std::chrono::steady_clock::now();
		const VCOMError err = pub->GetPublishSavedSets(map);
		probe.log(tag + ": GetPublishSavedSets err=" + std::to_string(err) + " セットの数=" +
				  std::to_string(map.size()) + " 所要=" + std::to_string(PsMs(t0)) + "ms");
		for (const auto& kv : map)
			out.push_back({PsStr(kv.first), kv.second});
		std::sort(out.begin(), out.end(),
				  [](const PsSet& a, const PsSet& b) { return a.name < b.name; });
		for (const PsSet& s : out)
		{
			probe.log(tag + ":   セット「" + s.name +
					  "」項目の数=" + std::to_string(s.items.size()));
			for (size_t i = 0; i < s.items.size(); ++i)
			{
				probe.log(tag + ":     [" + std::to_string(i) + "] fItemName=「" +
						  PsStr(s.items[i].fItemName) + "」 fFilePath=「" +
						  PsStr(s.items[i].fFilePath) + "」");
			}
		}
		return out;
	}

	// PDF の頁の数の目安。圧縮されたオブジェクトストリームの中の頁は数えられない。
	int PsCountPages(const std::string& bytes)
	{
		int n = 0;
		for (const char* key : {"/Type/Page", "/Type /Page"})
		{
			const std::string k = key;
			for (size_t pos = bytes.find(k); pos != std::string::npos;
				 pos = bytes.find(k, pos + k.size()))
			{
				const size_t next = pos + k.size();
				if (next < bytes.size() && bytes[next] == 's')
					continue;
				++n;
			}
		}
		return n;
	}

	// 最初の /MediaBox の後ろ 40 文字（用紙の大きさの目安。単位は pt）。
	std::string PsFirstMediaBox(const std::string& bytes)
	{
		const size_t pos = bytes.find("/MediaBox");
		if (pos == std::string::npos)
			return "（見つからない）";
		std::string s = bytes.substr(pos, 48);
		std::replace(s.begin(), s.end(), '\n', ' ');
		std::replace(s.begin(), s.end(), '\r', ' ');
		return s;
	}

	void PsLogFolder(vwprobe::Report& probe, const psfs::path& dir, const std::string& tag)
	{
		std::error_code ec;
		if (!psfs::exists(dir, ec))
		{
			probe.log(tag + ": 出力先は無い " + PsUtf8(dir));
			return;
		}
		int count = 0;
		for (psfs::recursive_directory_iterator it(dir, ec), end; !ec && it != end;
			 it.increment(ec))
		{
			const psfs::path p = it->path();
			const std::string rel = PsUtf8(psfs::relative(p, dir, ec));
			if (it->is_directory(ec))
			{
				probe.log(tag + ":   [dir] " + rel);
				continue;
			}
			++count;
			const auto size = psfs::file_size(p, ec);
			const auto mtime = psfs::last_write_time(p, ec).time_since_epoch().count();
			std::ifstream in(p, std::ios::binary);
			const std::string bytes((std::istreambuf_iterator<char>(in)),
									std::istreambuf_iterator<char>());
			const bool pdf = bytes.rfind("%PDF-", 0) == 0;
			probe.log(tag + ":   " + rel + " size=" + std::to_string(size) +
					  " mtime=" + std::to_string(mtime) + " pdf=" + PsFlag(pdf) +
					  (pdf ? " pages≒" + std::to_string(PsCountPages(bytes)) + " " +
								 PsFirstMediaBox(bytes) + " head=" + bytes.substr(0, 8)
						   : std::string()));
		}
		probe.log(tag + ": ファイルの数=" + std::to_string(count) +
				  (ec ? " 列挙の失敗=" + ec.message() : std::string()));
	}

	void PsLogUndo(vwprobe::Report& probe, const std::string& tag)
	{
		probe.log(tag + ": IsCurrentlyBuildingAnUndoEvent=" +
				  PsFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()));
	}

	// 4 引数の PublishSavedSet を 1 回呼ぶ。所要が長ければダイアログで人を待った見込み。
	void PsPublish(vwprobe::Report& probe, Filing::IPublishPtr& pub, const std::string& tag,
				   const std::string& setName, const psfs::path& dir)
	{
		Filing::IFolderIdentifierPtr folder(Filing::IID_FolderIdentifier);
		const VCOMError setErr = folder->Set(TXString(PsUtf8(dir).c_str()));
		TXString full;
		folder->GetFullPath(full);
		bool exists = false;
		folder->ExistsOnDisk(exists);
		probe.log(tag + ": 出力先 Set err=" + std::to_string(setErr) + " GetFullPath=「" +
				  PsStr(full) + "」 ExistsOnDisk=" + PsFlag(exists));

		Filing::SPublishOptions opts;
		opts.fDoPDFOnly = true;
		opts.fOpenDestinationFolder = false;
		opts.fCreateFolderForeachFiletype = false;
		TXStringArray results;
		bool doingBefore = false;
		pub->DoingPublish(doingBefore);
		PsLogUndo(probe, tag + " 前");
		const Sint32 refBefore = PsActiveRef();
		probe.log(tag + ": PublishSavedSet(「" + setName +
				  "」) を呼ぶ DoingPublish=" + PsFlag(doingBefore));
		const auto t0 = std::chrono::steady_clock::now();
		const VCOMError err =
			pub->PublishSavedSet(TXString(setName.c_str()), folder, opts, results);
		const long long ms = PsMs(t0);
		bool doingAfter = false;
		pub->DoingPublish(doingAfter);
		probe.log(tag + ": 戻った err=" + std::to_string(err) + " 所要=" + std::to_string(ms) +
				  "ms DoingPublish=" + PsFlag(doingAfter) +
				  " 結果の数=" + std::to_string(results.GetSize()) + " 前面 fileRef " +
				  std::to_string(refBefore) + "→" + std::to_string(PsActiveRef()));
		for (size_t i = 0; i < results.GetSize(); ++i)
			probe.log(tag + ":   結果[" + std::to_string(i) + "]=「" + PsStr(results.GetAt(i)) +
					  "」");
		PsLogUndo(probe, tag + " 後");
		PsLogFolder(probe, dir, tag + " 直後");
		// 別のスレッドが書き続けていないかを見るため、少し置いてもう一度読む。
		std::this_thread::sleep_for(std::chrono::seconds(2));
		PsLogFolder(probe, dir, tag + " 2 秒後");
	}
} // namespace

VW_PROBE("publish-saved-set", "パブリッシュセットを PDF に", "IPublish の一覧と書き出しを測る")
{
	PsLogDocs(probe, "始め");
	const Sint32 startRef = PsActiveRef();

	Filing::IPublishPtr pub(Filing::IID_Publish);
	if (!pub)
	{
		probe.fail("IPublishPtr(IID_Publish) が nullptr");
		return;
	}
	bool doing = false;
	bool cloud = false;
	bool preparing = false;
	probe.log("DoingPublish err=" + std::to_string(pub->DoingPublish(doing)) + " =" +
			  PsFlag(doing));
	probe.log("DoingCloudPublish err=" + std::to_string(pub->DoingCloudPublish(cloud)) + " =" +
			  PsFlag(cloud));
	probe.log("PreparingCloudPublish err=" + std::to_string(pub->PreparingCloudPublish(preparing)) +
			  " =" + PsFlag(preparing));

	// 1. 前面の図面のセット。無ければ他の図面を前面にして探す。
	std::vector<PsSet> sets = PsReadSets(probe, pub, "前面");
	Sint32 targetRef = startRef;
	if (sets.empty())
	{
		for (const PsDoc& d : PsDocs())
		{
			if (d.ref == startRef)
				continue;
			const bool sw = gSDK->SwitchToOpenFile(d.ref);
			probe.log("SwitchToOpenFile(" + std::to_string(d.ref) + ")=" + PsFlag(sw) +
					  " 前面 fileRef=" + std::to_string(PsActiveRef()) + " path=" + d.path);
			sets = PsReadSets(probe, pub, "fileRef " + std::to_string(d.ref));
			if (!sets.empty())
			{
				targetRef = d.ref;
				break;
			}
		}
	}
	if (sets.empty())
	{
		gSDK->SwitchToOpenFile(startRef);
		probe.fail(
			"パブリッシュセットのある図面が開いていない。セットを登録した図面を開いて走らせる");
		return;
	}
	const std::string setName = sets.front().name;
	probe.log("使うセット「" + setName + "」 図面 fileRef=" + std::to_string(targetRef));

	std::error_code ec;
	const auto stamp = std::to_string(std::time(nullptr));
	const psfs::path base = psfs::temp_directory_path(ec) / "VwSdkProbes-publish" / stamp;
	psfs::create_directories(base, ec);
	probe.log("出力の根 " + PsUtf8(base) + (ec ? " 作成の失敗=" + ec.message() : std::string()));

	// 2. 前面でない図面のセットの名前で、前面（セットの無い図面）から呼ぶ。
	if (targetRef != startRef)
	{
		probe.log("SwitchToOpenFile(" + std::to_string(startRef) +
				  ")=" + PsFlag(gSDK->SwitchToOpenFile(startRef)));
		psfs::create_directories(base / "front-without-set", ec);
		PsPublish(probe, pub, "前面にセット無し", setName, base / "front-without-set");
		probe.log("SwitchToOpenFile(" + std::to_string(targetRef) +
				  ")=" + PsFlag(gSDK->SwitchToOpenFile(targetRef)));
	}

	// 3. 空のフォルダへ 1 回目、同じフォルダへ 2 回目。
	psfs::create_directories(base / "run", ec);
	PsPublish(probe, pub, "1 回目", setName, base / "run");
	PsPublish(probe, pub, "2 回目（同じ出力先）", setName, base / "run");

	// 4. 無いセット。
	psfs::create_directories(base / "no-such-set", ec);
	PsPublish(probe, pub, "無いセット", "VwSdkProbes 無いセット 332", base / "no-such-set");

	// 5. 無いフォルダ（作らずに渡す）。
	PsPublish(probe, pub, "無いフォルダ", setName, base / "missing" / "deeper");

	// 6. 書き込みを拒むフォルダ。
	const psfs::path locked = base / "read-only";
	psfs::create_directories(locked, ec);
	psfs::permissions(locked, psfs::perms::owner_read | psfs::perms::owner_exec,
					  psfs::perm_options::replace, ec);
	probe.log("read-only の権限を絞った" + (ec ? " 失敗=" + ec.message() : std::string()));
	PsPublish(probe, pub, "書けないフォルダ", setName, locked);
	psfs::permissions(locked, psfs::perms::owner_all, psfs::perm_options::replace, ec);

	// 7. 後始末と終わりの様子。自動で開いた undo の記録が残っていれば閉じる。
	PsLogUndo(probe, "終わり");
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		probe.log("EndUndoEvent=" + PsFlag(gSDK->EndUndoEvent()));
	if (PsActiveRef() != startRef)
		probe.log("SwitchToOpenFile(" + std::to_string(startRef) +
				  ")=" + PsFlag(gSDK->SwitchToOpenFile(startRef)));
	PsLogDocs(probe, "終わり");
	probe.log("出力は消さずに残す: " + PsUtf8(base));
}
