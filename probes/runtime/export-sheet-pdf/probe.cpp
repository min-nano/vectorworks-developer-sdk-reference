//
//	probes/runtime/export-sheet-pdf/probe.cpp
//
//	[issue #332] Filing::IExportPDF でシートレイヤを PDF に書き出す。
//	ExportPDFPages(name) の name が何を受け付けるか（空・シートレイヤの名前・保存したビューの名前）、
//	現在のレイヤを切り替えて 1 つの PDF に何枚入るか、SExportPDFOptions の既定値と SetOptions の効き、
//	呼び出しの中で書き終わるか・ダイアログで止まるか（所要で見分ける）、同じパスへの 2 回目、
//	失敗（無い名前・開かずに書く・無いフォルダ・書けないフォルダ）、ISDK の同じ並びの呼び出し、
//	undo と前面の図面を測る。
//
//	前半はブリッジの空図面（new_document=true の前面）にシートレイヤを 2 枚作って測る。後半は、
//	後ろに開いている図面のうちシートレイヤのある最初の 1 つを前面にし、シートごとと全部をまとめた
//	PDF を、シートの名前で指定して書き出す（GUI のパブリッシュの PDF と見比べるため）。後半の図面には
//	図形を作らず、前面を元に戻す。
//	出力は一時ディレクトリの VwSdkProbes-export-pdf/<時刻>/ の下。消さずに残す。
//

#include "Probe.h"

#include "Interfaces/VectorWorks/Filing/IExportPDF.h"
#include "Interfaces/VectorWorks/Filing/IFileIdentifier.h"

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
	namespace epfs = std::filesystem;

	std::string EpFlag(bool b)
	{
		return b ? "yes" : "no";
	}

	std::string EpUtf8(const epfs::path& p)
	{
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	std::string EpStr(const TXString& s)
	{
		return static_cast<const char*>(s);
	}

	long long EpMs(std::chrono::steady_clock::time_point t0)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - t0)
			.count();
	}

	struct EpDoc
	{
		Sint32 ref = -1;
		bool active = false;
		std::string path;
	};

	std::vector<EpDoc> EpDocs()
	{
		std::vector<EpDoc> out;
		TVWArray_OpenFileInformation files;
		gSDK->GetOpenFilesList(files);
		for (size_t i = 0, cnt = files.GetSize(); i < cnt; ++i)
		{
			EpDoc d;
			d.ref = files[i].fFileRef;
			d.active = files[i].fIsActive;
			TXString path;
			Filing::IFileIdentifier* pID = files[i].fpFileID;
			if (pID != nullptr)
				pID->GetFileFullPath(path);
			d.path = EpStr(path);
			out.push_back(d);
		}
		return out;
	}

	Sint32 EpActiveRef()
	{
		for (const EpDoc& d : EpDocs())
		{
			if (d.active)
				return d.ref;
		}
		return -1;
	}

	void EpLogDocs(vwprobe::Report& probe, const std::string& tag)
	{
		const std::vector<EpDoc> docs = EpDocs();
		probe.log(tag + ": 図面の数=" + std::to_string(docs.size()));
		for (const EpDoc& d : docs)
		{
			probe.log(tag + ":   fileRef=" + std::to_string(d.ref) + " active=" + EpFlag(d.active) +
					  " path=" + d.path);
		}
	}

	std::string EpName(MCObjectHandle h)
	{
		if (h == nullptr)
			return "（nullptr）";
		TXString name;
		gSDK->GetObjectName(h, name);
		return EpStr(name);
	}

	std::string EpReal(MCObjectHandle h, short selector)
	{
		TVariableBlock v;
		Real64 r = 0.0;
		if (!gSDK->GetObjectVariable(h, selector, v) || !v.GetReal64(r))
			return "（読めない）";
		return std::to_string(r);
	}

	// 前面の図面のシートレイヤを列の順に。
	std::vector<MCObjectHandle> EpSheets()
	{
		std::vector<MCObjectHandle> out;
		for (MCObjectHandle h = gSDK->FirstMemberObj(gSDK->GetDrawingHeader()); h != nullptr;
			 h = gSDK->NextObject(h))
		{
			if (gSDK->GetObjectTypeN(h) != kLayerNode)
				continue;
			TVariableBlock v;
			Sint16 type = 0;
			if (gSDK->GetObjectVariable(h, ovLayerType, v) && v.GetSint16(type) &&
				type == kLayerSheet)
				out.push_back(h);
		}
		return out;
	}

	void EpLogSheets(vwprobe::Report& probe, const std::vector<MCObjectHandle>& sheets,
					 const std::string& tag)
	{
		probe.log(tag + ": シートレイヤの数=" + std::to_string(sheets.size()));
		for (MCObjectHandle h : sheets)
		{
			probe.log(tag + ":   「" + EpName(h) +
					  "」 用紙(167/168, in)=" + EpReal(h, ovLayerSheetPaperWidth) + " x " +
					  EpReal(h, ovLayerSheetPaperHeight) + " シート(165/166, in)=" +
					  EpReal(h, ovLayerSheetWidth) + " x " + EpReal(h, ovLayerSheetHeight));
		}
	}

	void EpLogSavedViews(vwprobe::Report& probe, const std::string& tag)
	{
		const short n = gSDK->GetNumSavedViews();
		probe.log(tag + ": GetNumSavedViews=" + std::to_string(n));
		// 0 始まりか 1 始まりか分からないので、両端を 1 つずつ広げて読む。
		for (short i = 0; i <= n; ++i)
		{
			TXString view;
			const bool ok = gSDK->GetSavedView(i, view);
			probe.log(tag + ":   GetSavedView(" + std::to_string(i) + ")=" + EpFlag(ok) + " 「" +
					  EpStr(view) + "」");
		}
	}

	std::string EpOptions(const Filing::SExportPDFOptions& o)
	{
		return "fSizeOf=" + std::to_string(o.fSizeOf) +
			   " fStandardFormat=" + std::to_string(static_cast<int>(o.fStandardFormat)) +
			   " fExportDesignLayersAsPDFLayers=" + EpFlag(o.fExportDesignLayersAsPDFLayers) +
			   " fExportClassesAsPDFLayers=" + EpFlag(o.fExportClassesAsPDFLayers) +
			   " fGrayedLayersInitiallyInvisible=" + EpFlag(o.fGrayedLayersInitiallyInvisible) +
			   " fGrayedLayersWithNormalAttributes=" + EpFlag(o.fGrayedLayersWithNormalAttributes) +
			   " fResolution=" + std::to_string(o.fResolution) +
			   " fExportPatternsAtOnScreenResolution=" +
			   EpFlag(o.fExportPatternsAtOnScreenResolution) +
			   " fDownsampleImages=" + EpFlag(o.fDownsampleImages) +
			   " fDownsampleResolution=" + std::to_string(o.fDownsampleResolution) +
			   " fApplyJPEG=" + EpFlag(o.fApplyJPEG) +
			   " fRasterizeText=" + EpFlag(o.fRasterizeText) +
			   " fSubsetFonts=" + EpFlag(o.fSubsetFonts) +
			   " fOpacityForTransferModes=" + std::to_string(o.fOpacityForTransferModes) +
			   " fOpenInViewer=" + EpFlag(o.fOpenInViewer) +
			   " fExportRangeKind=" + std::to_string(static_cast<int>(o.fExportRangeKind)) +
			   " fExportRangeFromPage=" + std::to_string(o.fExportRangeFromPage) +
			   " fExportRangeToPage=" + std::to_string(o.fExportRangeToPage) +
			   " fUpdateViewports=" + EpFlag(o.fUpdateViewports) +
			   " fResetPluginObjects=" + EpFlag(o.fResetPluginObjects) +
			   " fRecalculateWorksheets=" + EpFlag(o.fRecalculateWorksheets);
	}

	// PDF の頁の数の目安。圧縮されたオブジェクトストリームの中の頁は数えられない。
	int EpCountPages(const std::string& bytes)
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

	// /MediaBox を全部（頁ごとの用紙の大きさの目安。単位は pt）。多ければ先頭の 8 つ。
	std::string EpMediaBoxes(const std::string& bytes)
	{
		std::string out;
		int n = 0;
		for (size_t pos = bytes.find("/MediaBox"); pos != std::string::npos && n < 8;
			 pos = bytes.find("/MediaBox", pos + 9), ++n)
		{
			const size_t close = bytes.find(']', pos);
			std::string s =
				bytes.substr(pos + 9, close == std::string::npos ? 40 : close - pos - 8);
			std::replace(s.begin(), s.end(), '\n', ' ');
			std::replace(s.begin(), s.end(), '\r', ' ');
			out += (out.empty() ? "" : " ") + s;
		}
		return out.empty() ? "（見つからない）" : out;
	}

	void EpLogFile(vwprobe::Report& probe, const epfs::path& p, const std::string& tag)
	{
		std::error_code ec;
		if (!epfs::exists(p, ec))
		{
			probe.log(tag + ": ファイルは無い " + EpUtf8(p.filename()));
			return;
		}
		const auto size = epfs::file_size(p, ec);
		// mac の file_clock の数は 128 ビットなので、to_string に渡せる型へ揃える。
		const auto mtime =
			static_cast<long long>(epfs::last_write_time(p, ec).time_since_epoch().count());
		std::ifstream in(p, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)),
								std::istreambuf_iterator<char>());
		const bool pdf = bytes.rfind("%PDF-", 0) == 0;
		probe.log(tag + ": " + EpUtf8(p.filename()) + " size=" + std::to_string(size) +
				  " mtime=" + std::to_string(mtime) + " pdf=" + EpFlag(pdf) +
				  (pdf ? " head=" + bytes.substr(0, 8) + " pages≒" +
							 std::to_string(EpCountPages(bytes)) +
							 " MediaBox=" + EpMediaBoxes(bytes)
					   : std::string()));
	}

	// 同じフォルダに思わぬファイルが増えていないかを見る。
	void EpLogFolder(vwprobe::Report& probe, const epfs::path& dir, const std::string& tag)
	{
		std::error_code ec;
		std::vector<std::string> names;
		for (epfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
			names.push_back(EpUtf8(it->path().filename()));
		std::sort(names.begin(), names.end());
		std::string joined;
		for (const std::string& n : names)
			joined += (joined.empty() ? "" : ", ") + n;
		probe.log(tag + ": フォルダの中身(" + std::to_string(names.size()) + ")=" + joined +
				  (ec ? " 列挙の失敗=" + ec.message() : std::string()));
	}

	void EpLogState(vwprobe::Report& probe, const std::string& tag)
	{
		probe.log(tag + ": undo=" + EpFlag(gSDK->IsCurrentlyBuildingAnUndoEvent()) +
				  " 前面 fileRef=" + std::to_string(EpActiveRef()) + " 現在のレイヤ「" +
				  EpName(gSDK->GetCurrentLayer()) + "」");
	}

	// 1 回の書き出しの手順。layers が空でなければ、各要素の前に現在のレイヤをそれにする。
	struct EpStep
	{
		MCObjectHandle layer = nullptr;
		std::string name;
	};

	void EpExport(vwprobe::Report& probe, Filing::IExportPDFPtr& pdf, const std::string& tag,
				  const epfs::path& file, const std::vector<EpStep>& steps)
	{
		probe.log("--- " + tag + " → " + EpUtf8(file));
		EpLogState(probe, tag + " 前");
		Filing::IFileIdentifierPtr id(Filing::IID_FileIdentifier);
		const VCOMError setErr = id->Set(TXString(EpUtf8(file).c_str()));
		const auto t0 = std::chrono::steady_clock::now();
		const VCOMError openErr = pdf->OpenPDFDocument(id);
		probe.log(tag + ": Set err=" + std::to_string(setErr) + " OpenPDFDocument err=" +
				  std::to_string(openErr) + " 所要=" + std::to_string(EpMs(t0)) + "ms");
		for (const EpStep& s : steps)
		{
			if (s.layer != nullptr)
			{
				gSDK->SetCurrentLayer(s.layer);
				probe.log(tag + ": SetCurrentLayer(「" + EpName(s.layer) + "」) → 現在「" +
						  EpName(gSDK->GetCurrentLayer()) + "」");
			}
			const auto t1 = std::chrono::steady_clock::now();
			const VCOMError err = pdf->ExportPDFPages(TXString(s.name.c_str()));
			probe.log(tag + ": ExportPDFPages(「" + s.name + "」) err=" + std::to_string(err) +
					  " 所要=" + std::to_string(EpMs(t1)) + "ms");
		}
		const auto t2 = std::chrono::steady_clock::now();
		const VCOMError closeErr = pdf->ClosePDFDocument();
		probe.log(tag + ": ClosePDFDocument err=" + std::to_string(closeErr) + " 所要=" +
				  std::to_string(EpMs(t2)) + "ms 合計=" + std::to_string(EpMs(t0)) + "ms");
		EpLogState(probe, tag + " 後");
		EpLogFile(probe, file, tag + " 直後");
		// 別のスレッドが書き続けていないかを見るため、少し置いてもう一度読む。
		std::this_thread::sleep_for(std::chrono::seconds(2));
		EpLogFile(probe, file, tag + " 2 秒後");
	}

	void EpSetOptions(vwprobe::Report& probe, Filing::IExportPDFPtr& pdf,
					  const Filing::SExportPDFOptions& o, const std::string& tag)
	{
		const VCOMError err = pdf->SetOptions(o);
		Filing::SExportPDFOptions back;
		const VCOMError getErr = pdf->GetOptions(back);
		probe.log(tag + ": SetOptions err=" + std::to_string(err) +
				  " → GetOptions err=" + std::to_string(getErr) + " " + EpOptions(back));
	}

	std::string EpSafe(std::string s)
	{
		for (char& c : s)
		{
			if (c == '/' || c == '\\' || c == ':')
				c = '_';
		}
		return s;
	}
} // namespace

VW_PROBE("export-sheet-pdf", "シートレイヤを PDF に", "IExportPDF の指定・設定・出力を測る")
{
	EpLogDocs(probe, "始め");
	const Sint32 startRef = EpActiveRef();

	Filing::IExportPDFPtr pdf(Filing::IID_ExportPDF);
	if (!pdf)
	{
		probe.fail("IExportPDFPtr(IID_ExportPDF) が nullptr");
		return;
	}
	Filing::SExportPDFOptions defaults;
	const VCOMError getErr = pdf->GetOptions(defaults);
	probe.log("既定の GetOptions err=" + std::to_string(getErr) + " " + EpOptions(defaults));

	std::error_code ec;
	const epfs::path base = epfs::temp_directory_path(ec) / "VwSdkProbes-export-pdf" /
							std::to_string(std::time(nullptr));
	epfs::create_directories(base / "front", ec);
	probe.log("出力の根 " + EpUtf8(base) + (ec ? " 作成の失敗=" + ec.message() : std::string()));

	// 1. 設定。ビューアを開かないようにする（fOpenInViewer は保存される設定なので、最後に戻す）。
	Filing::SExportPDFOptions opts = defaults;
	opts.fOpenInViewer = false;
	EpSetOptions(probe, pdf, opts, "fOpenInViewer=no");
	Filing::SExportPDFOptions trial = opts;
	trial.fResolution = opts.fResolution == 150 ? 300 : 150;
	EpSetOptions(probe, pdf, trial, "fResolution を変える");
	EpSetOptions(probe, pdf, opts, "fResolution を戻す");

	// 2. 前面（ブリッジの空図面）にシートレイヤを 2 枚作る。CreateLayer は現在のレイヤを切り替える
	//    ので、文字はそのシートに乗る。
	EpLogState(probe, "作る前");
	EpLogSavedViews(probe, "前面");
	MCObjectHandle s1 = gSDK->CreateLayer("VwSdkProbes S1", kLayerSheet);
	gSDK->CreateTextBlock("VwSdkProbes S1", WorldPt(0, 0), false, 0);
	MCObjectHandle s2 = gSDK->CreateLayer("VwSdkProbes S2", kLayerSheet);
	gSDK->CreateTextBlock("VwSdkProbes S2", WorldPt(0, 0), false, 0);
	if (s1 == nullptr || s2 == nullptr)
	{
		probe.fail("CreateLayer(kLayerSheet) が nullptr を返した");
		return;
	}
	EpLogSheets(probe, EpSheets(), "前面");
	EpLogState(probe, "作った後");

	const epfs::path front = base / "front";
	// 3. 名前の受け付け方。
	EpExport(probe, pdf, "S1 を現在にして空の名前", front / "current-s1-empty.pdf", {{s1, ""}});
	EpExport(probe, pdf, "S1 が現在のまま S2 の名前", front / "name-s2.pdf",
			 {{s1, "VwSdkProbes S2"}});
	EpExport(probe, pdf, "S2 を現在にして S2 の名前", front / "current-s2-name-s2.pdf",
			 {{s2, "VwSdkProbes S2"}});
	EpExport(probe, pdf, "無い名前", front / "no-such-name.pdf", {{s1, "VwSdkProbes 無い 332"}});

	// 4. 1 つの PDF に 2 枚。
	EpExport(probe, pdf, "S1 と S2 を 1 つに", front / "both.pdf", {{s1, ""}, {s2, ""}});

	// 5. 同じパスへの 2 回目（上書きか・確認か）。
	EpExport(probe, pdf, "同じパスへ 2 回目", front / "current-s1-empty.pdf", {{s1, ""}});

	// 6. 書き出す範囲の種類。
	for (const auto kind :
		 {Filing::SExportPDFOptions::eWholeAreaAsOnePage, Filing::SExportPDFOptions::eAllPages,
		  Filing::SExportPDFOptions::eCurrentView})
	{
		Filing::SExportPDFOptions r = opts;
		r.fExportRangeKind = kind;
		const std::string k = std::to_string(static_cast<int>(kind));
		EpSetOptions(probe, pdf, r, "範囲 " + k);
		EpExport(probe, pdf, "範囲 " + k, front / ("range-" + k + ".pdf"), {{s1, ""}});
	}
	EpSetOptions(probe, pdf, opts, "範囲を戻す");

	// 7. 失敗。
	{
		probe.log("--- 開かずに ExportPDFPages");
		const auto t0 = std::chrono::steady_clock::now();
		const VCOMError err = pdf->ExportPDFPages("");
		probe.log("開かずに: ExportPDFPages err=" + std::to_string(err) +
				  " 所要=" + std::to_string(EpMs(t0)) + "ms");
		const VCOMError closeErr = pdf->ClosePDFDocument();
		probe.log("開かずに: ClosePDFDocument err=" + std::to_string(closeErr));
	}
	EpExport(probe, pdf, "無いフォルダ", base / "missing" / "deeper" / "x.pdf", {{s1, ""}});
	const epfs::path locked = base / "read-only";
	epfs::create_directories(locked, ec);
	epfs::permissions(locked, epfs::perms::owner_read | epfs::perms::owner_exec,
					  epfs::perm_options::replace, ec);
	probe.log("read-only の権限を絞った" + (ec ? " 失敗=" + ec.message() : std::string()));
	EpExport(probe, pdf, "書けないフォルダ", locked / "x.pdf", {{s1, ""}});
	epfs::permissions(locked, epfs::perms::owner_all, epfs::perm_options::replace, ec);

	// 8. ISDK の同じ並び（VectorScript の説明では、先に設定のダイアログを出す前提）。
	{
		const epfs::path f = front / "isdk.pdf";
		probe.log("--- ISDK の並び → " + EpUtf8(f));
		gSDK->SetCurrentLayer(s1);
		const auto t0 = std::chrono::steady_clock::now();
		const bool opened = gSDK->OpenPDFDocument(TXString(EpUtf8(f).c_str()));
		probe.log("ISDK: OpenPDFDocument=" + EpFlag(opened) + " 所要=" + std::to_string(EpMs(t0)) +
				  "ms");
		if (opened)
		{
			const auto t1 = std::chrono::steady_clock::now();
			const short n = gSDK->ExportPDFPages("");
			probe.log("ISDK: ExportPDFPages(「」)=" + std::to_string(n) +
					  " 所要=" + std::to_string(EpMs(t1)) + "ms");
			gSDK->ClosePDFDocument();
		}
		EpLogState(probe, "ISDK 後");
		EpLogFile(probe, f, "ISDK");
	}
	EpLogFolder(probe, front, "前面の出力");

	// 9. 後ろの図面のうち、シートレイヤのある最初の 1 つ。シートは名前で指定する（前半で、現在の
	//    レイヤに依らず名前のシートが書き出されると分かったため）。図形は作らず、前面を戻す。
	for (const EpDoc& d : EpDocs())
	{
		if (d.ref == startRef)
			continue;
		const bool sw = gSDK->SwitchToOpenFile(d.ref);
		probe.log("SwitchToOpenFile(" + std::to_string(d.ref) + ")=" + EpFlag(sw) +
				  " 前面 fileRef=" + std::to_string(EpActiveRef()) + " path=" + d.path);
		const std::vector<MCObjectHandle> sheets = EpSheets();
		EpLogSheets(probe, sheets, "fileRef " + std::to_string(d.ref));
		if (sheets.empty())
			continue;
		std::vector<std::string> names;
		const size_t limit = std::min<size_t>(sheets.size(), 20);
		for (size_t i = 0; i < limit; ++i)
			names.push_back(EpName(sheets[i]));
		const epfs::path dir = base / ("doc-" + std::to_string(d.ref));
		epfs::create_directories(dir, ec);

		// 前面でない図面のシートの名前を、前面（シートの名前が無い図面）から渡す。
		probe.log("SwitchToOpenFile(" + std::to_string(startRef) +
				  ")=" + EpFlag(gSDK->SwitchToOpenFile(startRef)));
		EpExport(probe, pdf, "前面でない図面のシート「" + names.front() + "」",
				 dir / "back-name-from-front.pdf", {{nullptr, names.front()}});
		probe.log("SwitchToOpenFile(" + std::to_string(d.ref) +
				  ")=" + EpFlag(gSDK->SwitchToOpenFile(d.ref)));
		EpLogSavedViews(probe, "fileRef " + std::to_string(d.ref));
		EpLogState(probe, "名前で書き出す前");

		std::vector<EpStep> all;
		for (const std::string& name : names)
		{
			EpExport(probe, pdf, "シート「" + name + "」", dir / (EpSafe(name) + ".pdf"),
					 {{nullptr, name}});
			all.push_back({nullptr, name});
		}
		EpExport(probe, pdf, "シートを全部 1 つに", dir / "all-sheets.pdf", all);
		if (gSDK->GetNumSavedViews() > 0)
		{
			TXString view;
			gSDK->GetSavedView(0, view);
			if (view.IsEmpty())
				gSDK->GetSavedView(1, view);
			EpExport(probe, pdf, "保存したビュー「" + EpStr(view) + "」", dir / "saved-view.pdf",
					 {{nullptr, EpStr(view)}});
		}
		EpLogState(probe, "名前で書き出した後");
		EpLogFolder(probe, dir, "fileRef " + std::to_string(d.ref) + " の出力");
		break;
	}

	// 10. 後始末と終わりの様子。設定を既定に戻し、自動で開いた undo の記録が残っていれば閉じる。
	EpSetOptions(probe, pdf, defaults, "設定を既定に戻す");
	if (EpActiveRef() != startRef)
		probe.log("SwitchToOpenFile(" + std::to_string(startRef) +
				  ")=" + EpFlag(gSDK->SwitchToOpenFile(startRef)));
	EpLogState(probe, "終わり");
	if (gSDK->IsCurrentlyBuildingAnUndoEvent())
		probe.log("EndUndoEvent=" + EpFlag(gSDK->EndUndoEvent()));
	EpLogDocs(probe, "終わり");
	probe.log("出力は消さずに残す: " + EpUtf8(base));
}
