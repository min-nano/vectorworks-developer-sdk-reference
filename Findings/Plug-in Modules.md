# プラグインモジュールの読み込みと入れ替え

コンパイル済みプラグイン（`.vwlibrary` / `.vlb`）が**いつ読み込まれ、いつなら入れ替えられるか**。
そして「Vectorworks を起動したまま中身を差し替えられないか」という問い（issue #15）の調査。

## 読み込みは起動時の 1 度きり

Vectorworks はコンパイル済みプラグインを**起動時に走査して読み込む**。読み込み済みの
モジュールは、そのプロセスが生きている間は差し替えられない——ファイルを新しいものへ
置き換えても、**次に起動するまで反映されない**。

実機確認プラグイン（`plugin/`）の自動アップデートが、入れ替えのあとに必ず
「再起動しますか？」と尋ねるのはこのため（[`plugin/README.md`](../plugin/README.md)
「自動アップデート」）。

- macOS では**読み込み中のバンドルのファイルを置き換えること自体はできる**（Unix の
  ファイル置き換えの作法どおり）。それでも動いているコードは古いままである。
- Windows は**読み込み中の DLL を消すことも上書きすることもできない**（共有違反）。
  入れ替えは Vectorworks を終了させてからになる。

## VCOM の初期化はモジュールごとに閉じている【ソース根拠】

`gSDK` / `gCBP` / `gVWMM` / `gVCOMCallback` は、SDK の静的ライブラリ（`libVWSDK.a` /
`VWSDK.lib`）が持つ**そのモジュール内のグローバル変数**である。

```
Include/Kernel/API/MiniCadHookIntf.h:2032  extern CallBackPtr        gCBP;
Include/Kernel/API/MiniCadHookIntf.h:2041  extern VectorWorks::ISDK* gSDK;
Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:150  CallBackPtr gCBP = nil;
Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:54   VectorWorks::ISDK* gSDK = NULL;
```

それらを埋めるのが `GS_InitializeVCOM(void* cbpParam)`
（`Source/VWSDK/Kernel/API/MiniCadCallBacks.cpp:219`）で、**やっていることは
`cbpParam` を材料にした自モジュール内の代入だけ**である:

| すること | 何を埋めるか |
| --- | --- |
| `GS_Kludge(cbp, 499, &ver, &gVCOMCallback)` | VCOM のコールバック |
| `VWMM::Partition::GetPartitionByIdentifier("VWMainPartition", true)` | `gVWMM` |
| `gCBP = (CallBackPtr)cbpParam` | 旧 `GS_` 系の呼び出し口 |
| `VCOMPtr<ISDK>(IID_SDK)` | `gSDK` |

**cbp 以外の外部状態を見ていない**。したがって——

- **同じプロセスに SDK の複製を持つモジュールが 2 つあってよい**（はず）。片方が
  受け取った `CallBackPtr` をもう片方へ渡して `GS_InitializeVCOM` を呼べば、
  そちらの `gSDK` も埋まる。【推定】実機での確認は下記。
- `GS_InitializeVCOM` は内部で `plugin_module_ver()` を呼ぶので、**プラグインとして
  登録されないモジュールでもこの関数の定義が要る**（無いとリンクで未解決になる。
  `Include/VectorworksSDK.h:56-61` の注記どおり）。

```cpp
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver() { return SDK_VERSION; }
```

## 本体を外部モジュールへ出す（検証中）

上の 2 つを合わせると、**再起動なしの入れ替え**への道が 1 本見える:

```
Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）      … 起動時に 1 度きり
                              │ dlopen / LoadLibrary
                              ▼
                          本体（調査コード）                     … 好きなときに降ろして読み直せる
```

Vectorworks は本体モジュールの存在を知らないので、**その入れ替えに Vectorworks の
再起動は要らない**——というのが確かめたいこと。実装は次の 3 つ:

| 置き場所 | 役割 |
| --- | --- |
| [`plugin/src/PayloadAbi.h`](../plugin/src/PayloadAbi.h) | 殻と本体の間の **C の ABI**（例外・C++ の型・vtable を跨がせない） |
| [`plugin/src/PayloadHost.{h,cpp}`](../plugin/src/PayloadHost.h) | 殻の側（読み込み・解決・アンロード・複製） |
| [`plugin/src/payload/PayloadMain.cpp`](../plugin/src/payload/PayloadMain.cpp) | 本体の側（`GS_InitializeVCOM` を自分で呼ぶ） |

境界を C にしてあるのは**降ろすため**である。降ろした瞬間にそのモジュールのコードと
静的データは消えるので、殻の側に残ってよいのは「値を写したもの」だけになる
（返した `const char*` はその場で `std::string` へ写す。例外は境界の手前で受ける）。

### 実機で確かめること（未確認）

[`probes/runtime/hot-reload/`](../probes/runtime/hot-reload/) のプローブが、**中身の違う
2 つのモジュール（変種 A / B）**を順に読み込んで次を測る。どれか 1 つでも駄目なら
この道は成り立たない。

1. Vectorworks のプロセス内で自前のモジュールを読み込めるか（**macOS のアドホック署名で
   弾かれないか**。ホストアプリの hardened runtime に library validation が掛かっていると
   dlopen が失敗しうる）。
2. そのモジュールから SDK が使えるか（上の【推定】の実測）。
3. `dlclose` / `FreeLibrary` で**本当にプロセスから消えるか**（0 が返っても消えていない
   ことがある。`dlopen(RTLD_NOLOAD)` / `GetModuleHandleW` で別の目で見る）。
4. 降ろした後にファイルを置き換えて読み直せるか（＝再起動が要らないか）。

結果が出たらここへ実測値ごと書く。

## 参考

- [`plugin/README.md`](../plugin/README.md) — 実機確認プラグインの全体像と自動アップデート
- [Progress and Diagnostics](Progress%20and%20Diagnostics.md) — 例外境界・VW のバージョンは取れない
