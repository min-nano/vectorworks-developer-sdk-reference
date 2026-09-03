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

### SDK の静的ライブラリをリンクするモジュールは、この 2 つを定義しなければならない

**Vectorworks にプラグインとして登録されないモジュールでも要る**（CI の実ビルドで確認。
無いとリンクが通らない）。

```cpp
// ① GS_InitializeVCOM がこれを呼ぶ（Include/VectorworksSDK.h:56-61 の注記どおり）。
extern "C" Sint32 GS_EXTERNAL_ENTRY plugin_module_ver() { return SDK_VERSION; }

// ② リソース（.vwr）の識別子。**リソースを 1 つも持たず、引きもしないモジュールでも要る。**
const char* DefaultPluginVWRIdentifier() { return "<vwr の基底名>"; }
```

② を落とすと出るのはこの形の未解決で、**呼んでいる覚えのない場所から参照される**ので
理由が分かりにくい（`TXString` を使えば `TXResStr` 経由で入ってくる）。

```
mac : Undefined symbols: "DefaultPluginVWRIdentifier()", referenced from:
        _GS_GetLayoutFromRsrc in libVWSDK.a(APIBase.Legacy.Defs.o)
        TXResStr::TXResStr(char const*, char const*, EEmptyHandling) in libVWSDK.a(TypesString.o)
win : VWSDK.lib(TypesString.obj) : error LNK2019: unresolved external symbol
        "char const * __cdecl DefaultPluginVWRIdentifier(void)"
```

`DefaultPluginVWRIdentifier` は **C++ リンケージ**（`extern "C"` を付けない）。

## 本体を外部モジュールへ出せば、再起動なしで入れ替えられる

**できる。macOS で実測した**（VW 2026 / Apple Silicon）。上の 2 つを合わせた道:

```
Vectorworks ──読み込む──▶ 殻（メニュー・ダイアログ・更新）      … 起動時に 1 度きり
                              │ dlopen / LoadLibrary
                              ▼
                          本体（調査コード）                     … 好きなときに降ろして読み直せる
```

Vectorworks は本体モジュールの存在を知らないので、**その入れ替えに Vectorworks の
再起動は要らない**。実機確認プラグイン（`plugin/`）は**この形で作られている**
（[`plugin/README.md`](../plugin/README.md)「殻と本体」）。境界は次の 3 つ:

| 置き場所 | 役割 |
| --- | --- |
| [`plugin/src/PayloadAbi.h`](../plugin/src/PayloadAbi.h) | 殻と本体の間の **C の ABI**（例外・C++ の型・vtable を跨がせない） |
| [`plugin/src/PayloadHost.{h,cpp}`](../plugin/src/PayloadHost.h) | 殻の側（読み込み・解決・アンロード・複製） |
| [`plugin/src/payload/PayloadMain.cpp`](../plugin/src/payload/PayloadMain.cpp) | 本体の側（`GS_InitializeVCOM` を自分で呼ぶ） |

境界を C にしてあるのは**降ろすため**である。降ろした瞬間にそのモジュールのコードと
静的データは消えるので、殻の側に残ってよいのは「値を写したもの」だけになる
（返した `const char*` はその場で `std::string` へ写す。例外は境界の手前で受ける）。

### 実測（macOS。中身の違う 2 つのモジュールを順に読んだ）

`.vwlibrary` の `Contents/Resources` に置いた**アドホック署名済み**の `MH_BUNDLE`
（拡張子 `.vwpayload`）を、一時ディレクトリへ複製してから読み込んだ。

| 確かめたこと | 結果 |
| --- | --- |
| Vectorworks のプロセス内で `dlopen` できるか | **できた。** アドホック署名で足りる（ホストの library validation に弾かれない） |
| そのモジュールから SDK が使えるか | **使えた。** 殻の `gCBP` を渡して `GS_InitializeVCOM` → `gSDK` が埋まり、レイヤを読めた |
| `dlclose` で本当に消えるか | **消えた**（`dlopen(RTLD_NOLOAD)` が nullptr を返す） |
| 読み込み中のファイルを消せるか | **消せた**（macOS の作法どおり。Windows は消せないはず＝未確認） |
| 降ろした後に置き換えて読み直せるか | **できた。変種 A → B で、走ったコードが実際に入れ替わった** |

かかった時間（プローブ全体で 0.76 秒）:

| | 世代 1（変種 A） | 世代 2（変種 B） |
| --- | --- | --- |
| 読み込み（`dlopen` ＋ シンボル解決） | 373 ms | 342 ms |
| アンロード（`dlclose`） | 19 ms | 14 ms |

**読み込みが 0.3〜0.4 秒かかる**のは、モジュールが SDK の静的ライブラリを丸ごと
抱えているため。Vectorworks の再起動（数十秒）とは比べるまでもない。

**降ろした後、次のモジュールが同じ番地へ載った**（両世代とも、モジュール内の関数の
番地が `0xb1f798760`）。dyld が解放された空間を再利用しただけで、アンロードが本当に
起きている傍証になる。

### 読み違えやすいところ: `gSDK` の**値**は殻と同じになる

実測では殻とペイロードの `gSDK` が同じ値（`0x1069a2818`）だった。これは
**共有している証拠ではない**——`gSDK` は VW 側の同じ `ISDK` 実装を指すポインタなので、
どのモジュールから初期化しても同じ値が入る。**複製かどうかを見るなら `&gSDK`**
（変数そのものの番地）を比べること。ペイロードは静的ライブラリを自分でリンクし、
`RTLD_LOCAL`（二階層名前空間）で読み込んでいるので、変数はモジュールごとに別である。

### 実運用でこうした（この形にするときの決めごと）

| 決めたこと | なぜ |
| --- | --- |
| 境界は **C の ABI**（`VW_PAYLOAD_ABI_VERSION` 付き） | 降ろす前提なので、例外・C++ のオブジェクト・vtable を跨がせない。殻と本体は別々にビルドされ別々に配られるので、版の食い違いは実行時にしか気付けない |
| 本体は**必ず一時ディレクトリへ複製してから読む** | Windows は読み込み中の DLL を置き換えられない。同梱物を直接読むと、動かしたまま入れ替えられなくなる＝仕組みが死ぬ |
| 本体は**バンドルの中に置かない**（mac も殻の隣） | バンドルの署名はリソースまで封をするので、中を差し替えると署名が壊れる |
| **メニューを開くたびに読み、終わったら降ろす** | 入れ替えが自動で効く（「読み直す」を押させない）。降ろすときにコードがスタックに無いことが自明になる |
| 再起動の要否は**殻の ID の一致**で決める | 本体だけなら要らない。判断できないときは必ず「要る」へ倒す——版が食い違ったまま動かすほうが危ない |

### まだ確かめていないこと

- **Windows**（`LoadLibraryEx` / `FreeLibrary`）。設計上は同じはずだが未実測。
- 本体側から**ダイアログを開く**・**イベントを登録する**など、VW にモジュール内の
  番地を持たせる操作。持たせたまま降ろすと落ちるはずで、降ろす前に必ず手放す設計が要る
  （いまはプローブが走り終えてから降ろすので、走っている間の登録だけが危ない）。
- 長時間・多数回の読み直しでリークしないか。

## 参考

- [`plugin/README.md`](../plugin/README.md) — 実機確認プラグインの全体像と自動アップデート
- [Progress and Diagnostics](Progress%20and%20Diagnostics.md) — 例外境界・VW のバージョンは取れない
