//
//	example.cpp — compile モードの雛形（このファイル自体が smoke test を兼ねる）。
//
//	「この API はこの引数で呼べるか」を確かめたいときは、このファイルを写して
//	調べたい呼び出しに置き換える。実行はしない（-fsyntax-only）ので、関数の中に
//	呼び出しを書くだけでよい。probes/README.md も参照。
//

#include "VectorworksSDK.h"

// 例: ISDK の呼び出しが期待するシグネチャで通るかを見る。
void probe_example()
{
	MCObjectHandle layer = gSDK->GetNamedLayer(TXString("example"));
	if (layer != nullptr)
	{
		WorldPt3 origin(0.0, 0.0, 0.0);
		(void)origin;
	}
}
