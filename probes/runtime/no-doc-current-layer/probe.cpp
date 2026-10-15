//
//	probes/runtime/no-doc-current-layer/probe.cpp
//
//	[issue #224] **図面を開かずに起動した直後**、OS タイマーの刻みから
//	`gSDK->GetCurrentLayer()` を呼ぶと何が返るかを実測する。
//
//	「図面を全部閉じた後」は、このプローブの前の版（20 ms の刻みを自分で仕掛けたもの）で
//	mac / Windows とも測り終えた（Findings「Timers and Notifications」4 の 8）。残ったのは
//	起動直後で、**プローブは図面が無いと走らせられない**（メニューは DocIsActive、ブリッジは
//	mac にしか無い）ので、刻みは殻に任せる——殻の時計は起動時から動いていて、一時ディレクトリに
//	頼み札があるときだけ、最初の刻みから件数と `GetCurrentLayer()` を記録する
//	（plugin/src/StartupRecord.h）。
//
//	このプローブがすること:
//	  * 記録（`VwSdkProbes-startup-record.txt`）があれば、それを報告して消す。
//	  * 無ければ頼み札（`VwSdkProbes-startup-record.request`）を置き、手順を出す:
//	    Vectorworks を終了 → 図面を開かずに起動して 20 秒ほど待つ → 図面を開く → もう一度走らせる。
//
//	札と記録の名前は殻（StartupRecord.cpp）と同じものを書き写してある（殻のヘッダは include
//	しない。probes/runtime/README.md「同じ PR で足した殻のヘッダを include しない」）。
//

#include "Probe.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
	std::filesystem::path NdclDir()
	{
		std::error_code ec;
		std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
		return ec ? std::filesystem::path(".") : dir;
	}

	std::string NdclDescribeNow()
	{
		VectorWorks::TVWArray_OpenFileInformation list;
		gSDK->GetOpenFilesList(list);
		const int docs = (int)list.GetSize();
		MCObjectHandle h = gSDK->GetCurrentLayer();
		std::string out = "docs=" + std::to_string(docs) +
						  " layer=" + (h == nullptr ? std::string("nullptr") : std::string("あり"));
		if (h != nullptr)
		{
			TXString name;
			gSDK->GetObjectName(h, name);
			out += " name=\"" + std::string(static_cast<const char*>(name)) +
				   "\" type=" + std::to_string(gSDK->GetObjectTypeN(h));
		}
		return out;
	}
} // namespace

VW_PROBE("no-doc-current-layer", "図面が無いときの GetCurrentLayer",
		 "起動直後の刻みの記録を頼む／報告する")
{
	probe.log("コマンドの中で読む: " + NdclDescribeNow());
	probe.log("");

	const std::filesystem::path record = NdclDir() / "VwSdkProbes-startup-record.txt";
	const std::filesystem::path request = NdclDir() / "VwSdkProbes-startup-record.request";
	std::error_code ec;

	if (std::filesystem::exists(record, ec))
	{
		probe.log("■ 殻が起動直後の刻みから書いた記録");
		std::ifstream in(record.string());
		std::string line;
		int n = 0;
		bool ended = false;
		while (std::getline(in, line) && n < 400)
		{
			probe.log("  " + line);
			if (line.rfind("end ", 0) == 0)
				ended = true;
			++n;
		}
		in.close();
		if (!ended)
		{
			// **消さない**——殻はまだ書いている（図面が開いてから 20 刻み＝5 秒ほどで閉じる）。
			// 消すと、残りの行だけで記録が作り直される（実際に踏んだ）。
			probe.log("");
			probe.log("end の行がまだ無い——殻は記録中（図面を開いてから 5 秒ほどで閉じる）。");
			probe.log("数秒待ってから、もう一度走らせる。（落ちていた場合は、このまま残る）");
			return;
		}
		std::filesystem::remove(record, ec);
		probe.log("");
		probe.log("記録を消した。");
		return;
	}

	if (std::filesystem::exists(request, ec))
		probe.log("頼み札はもう置いてある（まだ起動し直していない）。");
	else
	{
		std::ofstream(request.string()) << "issue #224\n";
		if (!std::filesystem::exists(request, ec))
		{
			probe.fail("頼み札を置けなかった: " + request.string());
			return;
		}
		probe.log("頼み札を置いた: " + request.string());
	}
	probe.log("");
	probe.log("このあと:");
	probe.log("  1. Vectorworks を終了する（開いている図面は閉じてよい）");
	probe.log("  2. **図面を開かずに**起動し、20 秒ほどそのまま待つ");
	probe.log(
		"  3. 図面を新規に開き、5 秒ほど待ってから、もう一度このプローブを走らせる（それが報告）");
	probe.log("※ 殻が StartupRecord を持つ版（main に入った後のビルド）でないと記録されない。");
}
