# 文字列型 `TXString`

SDK の文字列型と、自分のコード（`std::string` / 文字列リテラル）との境界で踏んだこと。
宣言は `SDKLib/Include/Kernel/Base/TypesString.h`。

## 入り口: `const char*` からも `std::string` からも暗黙変換される

```cpp
// Kernel/Base/TypesString.h（抜粋）
TXString(const std::string& src, ETXEncoding e = ETXEncoding::eUTF8);
TXString(const std::string_view& sv, ETXEncoding e = ETXEncoding::eUTF8);
TXString(const char* src, ETXEncoding e = ETXEncoding::eUTF8);
TXString(const char* src, size_t len, ETXEncoding e = ETXEncoding::eUTF8);
explicit TXString(char ch, size_t count = 1);
```

- **`explicit` が付いているのは「`char` 1 文字から作る」ものだけ**なので、`const char*`
  も `std::string` も、`TXString` を取る SDK の API へそのまま渡せる。
- **既定のエンコーディングは UTF-8。** ソースを UTF-8 で書いていれば、日本語のリテラルを
  そのままダイアログへ渡して正しく出る（実機確認済み: メニューコマンドのダイアログ文言は
  すべて UTF-8 の日本語リテラルをそのまま渡している）。

## **`std::string` と `TXString` の多重定義は、文字列リテラルで曖昧になる**

「どちらでも受けられる」ように次のような API を書くと、

```cpp
void log(const std::string& line);
void log(const TXString& line);   // SDK から読んだ値をそのまま渡せるように
```

**`log("...")` が曖昧になってコンパイルが通らない。**

```
error: call to member function 'log' is ambiguous
```

`const char*` から `std::string` へも `TXString` へも**ユーザー定義変換 1 段**で行けて
しまい、優劣が付かないため。**`std::string` の変数を渡している間は完全一致が勝つので
気付かず、リテラルを書いた瞬間に出る**。

実測は mac（`ci-debug` の `compile` と、CI の mac 実ビルドの両方で同じエラー）。C++ の
多重定義解決そのものの話なので、処理系に依らず同じになるはず。

直し方は 2 つ。

1. **`const TXString&` だけで受ける（多重定義しない）。** 上のとおり `std::string` も
   リテラルも暗黙変換で入るので、**そもそも 2 つ要らない**ことが多い。
2. **`const char*` の多重定義を足す。** 変換の要らない完全一致が勝つので、リテラルは
   そちらへ確定する。関数の中で `std::string` として扱いたい（連結する・溜める）なら、
   こちらのほうが素直。

```cpp
void log(const std::string& line);
void log(const TXString& line);
void log(const char* line);   // ← これでリテラルの曖昧さが消える
```

実装例は [`plugin/src/Probe.h`](../plugin/src/Probe.h) の `vwprobe::Report::log`。

## 出口: 文字列を取り出す

```cpp
// Kernel/Base/TypesString.h（抜粋）
operator const char*() const;                                    // UTF-8
std::string GetStdString(ETXEncoding e = ETXEncoding::eUTF8) const;
```

- **`operator const char*()` は UTF-8 を返す**（ヘッダのコメントがそう書いており、実機でも
  そのとおりだった——図面のレイヤ名「共通」が化けずにログへ出た）。`std::string` へ写す
  ときは `static_cast<const char*>(tx)` を通す。
- **`GetStdString()` なら 1 呼び出しで `std::string` になる**【ヘッダ根拠】。エンコーディングを
  明示できるぶん意図も残る（このリポジトリのコードはまだ `static_cast` 経由で書いている）。
- **返る `const char*` の寿命は元の `TXString` のもの。** 一時オブジェクトから受けた
  ポインタを変数へ溜めない（式の中で `std::string` へ写しきる）。

## 関連

- 上流の型の説明: [`Info/Type TXString.md`](../Info/Type%20TXString.md)
- ダイアログへ文字列を渡す側の作法: [Layout Dialogs](Layout%20Dialogs.md)
