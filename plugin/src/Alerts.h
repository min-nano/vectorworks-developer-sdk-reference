//
//	Alerts.h
//
//	Vectorworks のネイティブなアラート 2 種（知らせる／尋ねる）の薄い包み。
//
//	**同じ呼び出しを 2 か所に書かないためだけのもの。** 自動アップデート（Update.cpp）・
//	結果の投稿（Feedback.cpp）・メニュー（ProbeMenu.cpp）のどれもがこの 2 つを使うので、
//	`AlertInform` の第 3 引数（false = 最小アラートではなくダイアログ）や
//	`AlertQuestion` の戻り値の意味（1 = 肯定）を、各所で覚え直さずに済ませる。
//
//	【SDK 依存】gSDK を使うので、include する側は先に PluginPrefix.h を読んでいること。
//

#pragma once

#include <string>

namespace vwprobe
{
	// モーダルの通知（false = 最小アラートではなくダイアログ。advice 行も出る）。
	inline void Inform(const std::string& text, const std::string& advice)
	{
		gSDK->AlertInform(text.c_str(), advice.c_str(), false);
	}

	// はい／いいえ。肯定側を選んだら true。
	inline bool Ask(const std::string& text, const std::string& advice, const std::string& okText,
					const std::string& cancelText)
	{
		// 戻り値 0 = 否定／キャンセル、1 = 肯定。defaultButton 1 = 肯定側が既定。
		const short r = gSDK->AlertQuestion(text.c_str(), advice.c_str(),
											/*defaultButton*/ 1, okText.c_str(), cancelText.c_str(),
											/*customButtonA*/ "", /*customButtonB*/ "");
		return r == 1;
	}
} // namespace vwprobe
