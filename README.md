# The SDK

Vectorworks http://www.vectorworks.net provides an open architecture that allows developers to supplement or replace existing Vectorworks functionality. From the user’s perspective, these new tools, menu commands, and objects are indistinguishable from those built into Vectorworks. As such, they are first-class solutions for the user.

The SDK https://www.vectorworks.net/en-US/support/custom/sdk/sdkdown provides OS-independent functionality for accessing the application routines. Also, it provides functions for creating OS-independent User Interfaces for your plug-ins.

## このフォークについて（実測知見）

このリポジトリは公式リファレンスのフォークに、実際のプラグイン開発で判明した
**Vectorworks SDK の実測知見**と、リモート環境から SDK を調べるための**調査用 CI** を
足したものです。公式リファレンス（`Info/` / `Versions/`）だけでは分からない
「実機でしか判明しない挙動」「SDK に無い／効かない API」はこちらにあります。

- **[実測知見の索引 — `Findings/`](Findings/README.md)** … PIO・シンボル・壁・スラブ・
  屋根面・レイヤ・ビューポート・用紙・データタグ・グラフィック凡例・タグ付きデータ・
  Undo・ダイアログ・調査の作法
- [`CLAUDE.md`](CLAUDE.md) … 調査のフロー（issue → CI 調査 → PR → 実機確認 → 反映）と
  作業時の規約

## Basics

The Vectorworks SDK uses the C++ language to develop extension functionality. To develop SDK plug-ins for Vectorworks, you will need the following tools:

Before you begin, you should review [the Vectorworks environment](Info/The%20Vectorworks%20Environment.md).

The Vectorworks SDK provides [some specific types](Info/Types.md). A developer should use the recommended types. Here is a short list of the most commonly used types:
- [MCObjectHandle](Info/Type%20MCObjectHandle.md)
- [TXString](Info/Type%20TXString.md)
- [TXStringArray](Info/Type%20TXStringArray.md)
- [WorldPt](Info/Type%20WorldPt.md)
- [WorldPt3](Info/Type%20WorldPt3.md)
- [TransformMatrix](Info/Type%20TransformMatrix.md)

## Working with the SDK

- [Using the SDK](Info/Using%20the%20SDK.md)
- [Installing Plug-ins](https://github.com/Vectorworks/developer-scripting/blob/main/Common/Partner%20Install/README.md)

## Tasks

- [Common Tasks](Info/Using%20the%20SDK.md#common-tasks)
- [Parametric](Info/Using%20the%20SDK.md#parametric)
- [Tool](Info/Using%20the%20SDK.md#tool)

- [Working with files](Info/Using%20the%20SDK.md#working-with-files)

- [Dialogs](Info/Using%20the%20SDK.md#dialogs)

- [Working with Dialogs (Layout Manager)](Info/Using%20the%20SDK.md#working-with-dialogs-layout-manager)

- [Materials](Info/Using%20the%20SDK.md#materials)
- [Textures](Info/Using%20the%20SDK.md#textures)
- [Spotlight](Info/Using%20the%20SDK.md#spotlight)

## Plug-ins Modules

Vectorworks plug-in is a dynamic library on Windows and a specific type of bundle on Mac.

On launch, Vectorworks enumerates the 'Plug-ins' folder next to the application for dynamic libraries (or bundles). Also, Vectorworks traverses shortcuts (aliases) placed in the folder hierarchy.

Each Plug-in module is a [VCOM (Vectorworks Component Object Model)](Info/VCOM%20(Vectorworks%20Component%20Object%20Model).md) provider library. This library provides one or many VCOM implementations of standard interfaces for the different extensions.

[More information on the topic see here](Info/Plug-in%20Module.md).

## VWFC

[VectorWorks Foundation Classes](Info/VectorWorks%20Foundation%20Classes.md)

## Plugin Credential files

Vectorworks 2026 introduces the requirement for script plugins that are encrypted/obfuscated and SDK plugins to have a satellite credentials file that defines who is the developer of the plugins is.

More information can be found here: [Plugin Credentials](https://github.com/Vectorworks/developer-scripting/blob/main/Common/Tasks/Info/PluginCredentials.md)

## Version Information

- [Vectorworks 2026](Versions/Vectorworks%202026%20Development.md)
- [Vectorworks 2025](Versions/Vectorworks%202025%20Development.md)
- [Vectorworks 2024](Versions/Vectorworks%202024%20Development.md)
- [Vectorworks 2023](Versions/Vectorworks%202023%20Development.md)
- [Vectorworks 2022](Versions/Vectorworks%202022%20Development.md)
- [Vectorworks 2021](Versions/Vectorworks%202021%20Development.md)
- [Vectorworks 2020](Versions/Vectorworks%202020%20Development.md)
- [Vectorworks 2019](Versions/Vectorworks%202019%20Development.md)
- [Vectorworks 2018](Versions/Vectorworks%202018%20Development.md)