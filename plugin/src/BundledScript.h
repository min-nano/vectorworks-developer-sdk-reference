//
//	BundledScript.h
//
//	**同梱スクリプトを非対話で走らせる。** プラグインが「ネットワークを触ること」は
//	すべてスクリプトへ出してある（自動アップデート＝vw-probes-update、結果の投稿＝
//	vw-probes-feedback）ので、殻の側に要るのはこの 1 本の道だけになる。
//
//	【なぜスクリプトへ出すか】curl も TLS も認証も、C++ でやれば SDK と一緒にコンパイル
//	される＝**直すたびに再起動が要る**。スクリプトはプロセスへ読み込まれず、呼ぶたびに
//	ディスクから読み直されるので、置き換えれば次の呼び出しから効く（実プラグイン側の
//	規約と同じ。vectorworks-plugin-import-ifc-homeskz の CLAUDE.md）。
//
//	【出力は機械可読】どのスクリプトも `key=value` の行（と成功を表す素の `ok`）だけを
//	標準出力へ出し、自分ではダイアログを出さない。見せるのは必ず呼び出し側で、
//	Vectorworks のネイティブダイアログを使う——スクリプト側で何かを尋ねると、こちらは
//	出力を読み終わるまで止まるので、画面が固まる。
//
//	【拡張子は呼び出し側で付けない】mac は `.sh`、Windows は `.ps1` で、渡すのは
//	**拡張子の無い基底名**（"vw-probes-update" / "vw-probes-feedback"）。両方の綴りを
//	呼び出し側に持たせない。
//
//	【置き場所】mac はバンドルの Contents/Resources、Windows は .vlb の隣（plugin/
//	CMakeLists.txt が置く）。**自分が実際に読み込まれた場所**から辿るので、ユーザ
//	フォルダを変えていても正しく見つかる。
//

#pragma once

#include <string>
#include <vector>

namespace vwprobe
{
	// 同梱スクリプトの絶対パス（見つからなければ空）。
	std::string BundledScriptPath(const std::string& baseName);

	// そのスクリプトが実際に置かれているか。**古い版から入れ替えた直後は、新しい
	// スクリプトがまだ無いことがある**（Windows は入れ替えを古い版のスクリプトが
	// 行うため）ので、呼び出し側はこれで確かめてから機能を出す。
	bool BundledScriptExists(const std::string& baseName);

	// このビルドが**実際に読み込まれた** Plug-Ins フォルダ（既定パスの決め打ちでは
	// ない）。スクリプトへ VW_PLUGINS_DIR として渡す。
	std::string BundlePluginsDir();

	// 走らせて標準出力を out に取り込む（終わるまで待つ）。起動できなければ false。
	// **引数に秘密を乗せない**——プロセス一覧から見えるので、トークンや本文は
	// ファイル経由で渡す（Feedback.cpp）。
	bool RunBundledScript(const std::string& baseName, const std::vector<std::string>& args,
						  std::string& out);
} // namespace vwprobe
