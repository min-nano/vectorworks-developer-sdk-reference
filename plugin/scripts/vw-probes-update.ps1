<#
    vw-probes-update.ps1 — 実機確認プラグイン（VwSdkProbes）を入れ替える（Windows）。

    plugin/scripts/vw-probes-update.sh の Windows 版。Windows のプラグインは
    "<name>.vlb"（DLL）と隣に置く "<name>.vwr" なので、バンドルではなくその平たい
    ファイル群を入れ替える。

    プラグイン本体（plugin/src/Update.cpp）から**非対話**で呼ばれる裏方で、ダイアログは
    すべてプラグイン側が出す。こちらは機械可読な行を標準出力へ出すだけ。

      q                  installed= / latest= / url= / title= / probes=（または error=）
      do-install <url>   その zip を落として入れ替える。"ok" か error=<理由>。

    【新旧はビルド ID で比べる】コミットではない（同じ sha から、同居させる PR を変えて
    何度もビルドされるため）。公開側はリリース本文の隠しメタデータの build=、
    入っている側は "<name>.build-info.txt" の build=。

    必要なもの: Windows PowerShell 5.1 以上（Windows に最初から入っている）。
    リポジトリは public なので認証も要らない。

    環境変数で上書きできる:
      VW_REPO         owner/repo         （既定は下）
      VW_PLUGINS_DIR  Plug-Ins フォルダ  （既定は VW 2026 のユーザフォルダ。プラグインは
                                          実際に読み込まれたフォルダを必ず渡す）
#>

#requires -version 5
$ErrorActionPreference = 'Stop'

# TLS 1.2 を優先し（古い Windows PowerShell の既定はもっと低い）、UTF-8 で出す
# （プラグインが日本語のエラーを文字化けせずに読めるように）。
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false } catch {}

$VW_REPO = if ($env:VW_REPO) { $env:VW_REPO } else { 'min-nano/vectorworks-developer-sdk-reference' }
$VW_API = "https://api.github.com/repos/$VW_REPO"
$VW_TAG = if ($env:VW_TAG) { $env:VW_TAG } else { 'probes' }
$VW_NAME = 'VwSdkProbes'
$VW_PLUGINS_DIR = if ($env:VW_PLUGINS_DIR) { $env:VW_PLUGINS_DIR } else { Join-Path $env:APPDATA 'Nemetschek\Vectorworks\2026\Plug-Ins' }

$script:LastError = ''

# GitHub API を 1 つ叩く。-TimeoutSec で頭打ちにする（起動時チェックが Vectorworks を
# 止めないために必須）。
function Invoke-GH([string] $subpath) {
    return Invoke-RestMethod -Uri "$VW_API/$subpath" `
        -Headers @{ 'Accept' = 'application/vnd.github+json' } `
        -UserAgent 'vw-probes-update' -TimeoutSec 20 -Method Get
}

# 資産名から browser_download_url を引く（無ければ $null）。
function Get-AssetUrl($release, [string] $want) {
    foreach ($a in $release.assets) {
        if ($a.name -eq $want) { return $a.browser_download_url }
    }
    return $null
}

# リリース本文の隠しメタデータから "キー=値" の値を取り出す（無ければ ''）。
function Get-Meta([string] $body, [string] $key) {
    if (-not $body) { return '' }
    foreach ($line in ($body -split "`r?`n")) {
        if ($line.StartsWith("$key=")) { return $line.Substring($key.Length + 1).Trim() }
    }
    return ''
}

# 入っているビルドの ID。ビルドが .vlb の隣へ書き出す "<name>.build-info.txt" の
# build= 行から読む（mac 側はバンドルの Info.plist の VWBuildId）。
function Get-InstalledBuild {
    $f = Join-Path $VW_PLUGINS_DIR "$VW_NAME.build-info.txt"
    if (Test-Path -LiteralPath $f) {
        $c = Get-Content -LiteralPath $f -Raw -ErrorAction SilentlyContinue
        $v = Get-Meta $c 'build'
        if ($v) { return $v }
    }
    return 'none'
}

# 読み込み中の .vlb は削除できないが、**名前を変えてどかすことはできる**。どかして
# から新しいものを置く。残った ".old-*" は次の入れ替えのときに掃除する（そのころには
# Vectorworks が手放している）。
function Install-File([string] $src, [string] $dst) {
    if (Test-Path -LiteralPath $dst) {
        $bak = "$([System.IO.Path]::GetFileName($dst)).old-$([System.IO.Path]::GetRandomFileName())"
        try { Rename-Item -LiteralPath $dst -NewName $bak -ErrorAction Stop }
        catch { try { Remove-Item -LiteralPath $dst -Force -ErrorAction Stop } catch {} }
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

# zip を落として展開し、Plug-Ins フォルダへ入れ替える。成功なら $true。
function Install-Build([string] $url) {
    $script:LastError = ''
    if (-not $url) { $script:LastError = '引数が不足しています。'; return $false }

    $tmp = New-Item -ItemType Directory -Force -Path (Join-Path ([System.IO.Path]::GetTempPath()) ("vwprobes-" + [System.IO.Path]::GetRandomFileName()))
    try {
        $zip = Join-Path $tmp.FullName "$VW_NAME.vlb.zip"
        try { Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300 }
        catch { $script:LastError = 'ダウンロードに失敗しました。'; return $false }

        $work = Join-Path $tmp.FullName 'x'
        try { Expand-Archive -LiteralPath $zip -DestinationPath $work -Force }
        catch { $script:LastError = 'アーカイブの展開に失敗しました。'; return $false }

        if (-not (Test-Path -LiteralPath (Join-Path $work "$VW_NAME.vlb"))) {
            $script:LastError = "$VW_NAME.vlb が zip 内に見つかりません。"; return $false
        }

        if (-not (Test-Path -LiteralPath $VW_PLUGINS_DIR)) {
            New-Item -ItemType Directory -Force -Path $VW_PLUGINS_DIR | Out-Null
        }

        # 前回の入れ替えが残した控えを掃除する。
        Get-ChildItem -LiteralPath $VW_PLUGINS_DIR -Filter '*.old-*' -ErrorAction SilentlyContinue |
            ForEach-Object { try { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction Stop } catch {} }

        foreach ($f in @("$VW_NAME.vlb", "$VW_NAME.vwr", "$VW_NAME.build-info.txt", 'vw-probes-update.ps1')) {
            $s = Join-Path $work $f
            if (Test-Path -LiteralPath $s) {
                try { Install-File $s (Join-Path $VW_PLUGINS_DIR $f) }
                catch { $script:LastError = 'インストール先へのコピーに失敗しました。'; return $false }
            }
        }
        return $true
    }
    finally {
        try { Remove-Item -LiteralPath $tmp.FullName -Recurse -Force -ErrorAction SilentlyContinue } catch {}
    }
}

function Invoke-Query {
    try { $rel = Invoke-GH "releases/tags/$VW_TAG" }
    catch { Write-Output "error=リリース（$VW_TAG）を取得できませんでした。ネットワークを確認してください。"; return }

    $url = Get-AssetUrl $rel "$VW_NAME.vlb.zip"
    $latest = Get-Meta $rel.body 'build'
    $probes = Get-Meta $rel.body 'probes'
    if (-not $latest -or -not $url) {
        Write-Output 'error=リリースの情報が不完全です（ビルド ID か資産が見つかりません）。'
        return
    }

    Write-Output ("installed=" + (Get-InstalledBuild))
    Write-Output ("latest=" + $latest)
    Write-Output ("url=" + $url)
    if ($rel.name) { Write-Output ("title=" + $rel.name) }
    if ($probes) { Write-Output ("probes=" + $probes) }
}

function Invoke-DoInstall([string] $url) {
    if (Install-Build $url) {
        Write-Output 'ok'
    }
    else {
        $e = if ($script:LastError) { $script:LastError } else { 'インストールに失敗しました。' }
        Write-Output "error=$e"
    }
}

# 実行されたときだけ動かす（ドットソースされたときは動かさない）。
if ($MyInvocation.InvocationName -ne '.') {
    $mode = if ($args.Count -ge 1) { [string] $args[0] } else { '' }
    switch ($mode) {
        'q' { Invoke-Query }
        'do-install' { Invoke-DoInstall ([string] $args[1]) }
        default { Write-Output "error=不明なモード: '$mode'（q / do-install）。" }
    }
}
