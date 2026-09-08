<#
    vw-probes-update.ps1 — 実機確認プラグイン（VwSdkProbes）を入れ替える（Windows）。

    plugin/scripts/vw-probes-update.sh の Windows 版。Windows のプラグインは
    "<name>.vlb"（DLL）と隣に置く "<name>.vwr" なので、バンドルではなくその平たい
    ファイル群を入れ替える。

    プラグイン本体（plugin/src/Update.cpp）から**非対話**で呼ばれる裏方で、ダイアログは
    すべてプラグイン側が出す。こちらは機械可読な行を標準出力へ出すだけ。

      q                        installed= / latest= / installedShell= / latestShell= /
                               url= / title= / probes=（または error=）
                               **error= の理由は具体的に書く**——HTTP のコードか
                               WebException の種別と、それが何を意味するか（呼び出し
                               上限・DNS・証明書・プロキシ）。「ネットワークを確認して
                               ください」だけでは、網が生きているときに手掛かりが無い。
      do-install <url>         まるごと入れ替える（殻＋本体）。"ok" か error=<理由>。
      do-install-payload <url> **本体一式だけ**入れ替える。"ok" か error=<理由>。

    【なぜ 2 通りあるか】このプラグインは「殻（Vectorworks が起動時に読み込む .vlb）」と
    「本体（殻が自分で読み込む .vwpayload）」に割れている。**本体だけなら Vectorworks を
    動かしたまま置き換えられ**、次にメニューを開いたときから新しいプローブが動く
    （再起動が要らない）。殻まで変わったときだけ、まるごと入れ替えて再起動する。
    判断はプラグイン側（plugin/src/UpdateParse.h の Evaluate）が 2 つの ID を見て行う。

    Windows は読み込み中の DLL を消せないが、**本体は読み込まれていない**——殻は
    一時ディレクトリへ写した複製を読んでいる（plugin/src/PayloadHost.h）。だから本体の
    置き換えはいつでも通る。

    **zip は 1 つしか無い**（殻＋本体）。本体だけの入れ替えも同じ zip を落として、中から
    本体のファイルだけを取り出して置く——配る zip を分けると、人が手で入れるときの展開の
    手間が増えるだけ。

    【新旧はビルド ID で比べる】コミットではない（同じ sha から、同居させる PR を変えて
    何度もビルドされるため）。公開側はリリース本文の隠しメタデータの build= と shell=、
    入っている側は本体がカタログ "VwSdkProbes.probes.txt" の build=、殻が
    "<name>.build-info.txt" の shell=。

    【API が使えないときの逃げ道】q は最初に GitHub の API（api.github.com）を叩くが、
    **そこが駄目でも諦めない**。資産の URL はタグと名前から決まる
    （https://github.com/<repo>/releases/download/<タグ>/<名前>）ので、リリースの素性だけを
    書いた小さなテキスト <名前>.release-info.txt を直接落として読む（中身はリリース本文の
    隠しメタデータと同じ key=value で、scripts/probe-release-notes.sh --info が作る）。
    これで **API の呼び出し上限（未認証は 1 時間あたり 60 回）や、api.github.com だけが
    塞がれている網でも入れ替えられる**。両方駄目なときだけ、両方の理由を並べて返す。

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
# 本体のファイル名の頭（実体は "<この頭>-<群>.vwpayload"）と、殻が読む索引。
# **本体は群ごとに 1 本**（main のプローブが 1 本、PR のプローブが PR ごとに 1 本）。
# plugin/src/PayloadHost.h の payload::FileNameFor / CatalogFileName と対。
$VW_PAYLOAD_PREFIX = 'VwSdkProbesPayload-'
$VW_CATALOG = 'VwSdkProbes.probes.txt'
$VW_PLUGINS_DIR = if ($env:VW_PLUGINS_DIR) { $env:VW_PLUGINS_DIR } else { Join-Path $env:APPDATA 'Nemetschek\Vectorworks\2026\Plug-Ins' }
# **API を通さない取得先。** 資産の URL はタグと名前から決まるので、api.github.com が
# 使えないときはこちらから引く（下の Invoke-Query）。
$VW_DL = "https://github.com/$VW_REPO/releases/download/$VW_TAG"
$VW_INFO = "$VW_NAME.release-info.txt"

$script:LastError = ''
# 直近の取得が失敗した理由（Get-FetchText が入れる。**取れたら空**）。
$script:FetchReason = ''

# 失敗した理由を**1 行**にする。「ネットワークを確認してください」しか出さないと、網は
# 生きているのに落ちたとき（API の呼び出し上限・プロキシ・証明書）に手掛かりが残らない。
# HTTP のコードと WebException の種別は、**そのまま原因の名前**になるので捨てない。
function Get-FetchReason($err) {
    $resp = $null
    try { $resp = $err.Exception.Response } catch {}
    if ($resp) {
        $code = 0
        try { $code = [int] $resp.StatusCode } catch {}
        if ($code -eq 401 -or $code -eq 403 -or $code -eq 429) {
            $remaining = ''
            try { $remaining = [string] $resp.Headers['X-RateLimit-Remaining'] } catch {}
            if ($remaining -eq '0') {
                $hint = ''
                try {
                    $reset = [int] $resp.Headers['X-RateLimit-Reset']
                    $now = [int] [math]::Floor(([datetime]::UtcNow - [datetime] '1970-01-01').TotalSeconds)
                    $mins = [int] [math]::Ceiling(($reset - $now) / 60.0)
                    if ($mins -gt 0) { $hint = "。あと $mins 分で戻ります" }
                }
                catch {}
                return "HTTP ${code}: GitHub API の呼び出し上限に達しました（未認証は 1 時間あたり 60 回）$hint"
            }
            return "HTTP ${code}: 拒否されました（プロキシや社内フィルタの可能性）"
        }
        if ($code -eq 404) { return 'HTTP 404: 見つかりません（リリースか資産がまだありません）' }
        if ($code -ge 500) { return "HTTP ${code}: GitHub 側の一時的な障害" }
        if ($code -gt 0) { return "HTTP $code" }
    }

    $msg = ''
    try { $msg = ([string] $err.Exception.Message -replace '\s+', ' ').Trim() } catch {}
    $status = ''
    try { $status = [string] $err.Exception.Status } catch {}
    switch ($status) {
        'NameResolutionFailure' { return "名前解決に失敗しました（DNS）: $msg" }
        'ProxyNameResolutionFailure' { return "プロキシの名前解決に失敗しました: $msg" }
        'ConnectFailure' { return "接続できません（遮断・プロキシ設定の可能性）: $msg" }
        'TrustFailure' { return "TLS の検証に失敗しました（証明書）: $msg" }
        'SecureChannelFailure' { return "TLS の接続に失敗しました（SSL の設定）: $msg" }
        'Timeout' { return '時間内に応答がありません（20 秒で打ち切り）' }
    }
    if ($status) { return "${status}: $msg" }
    if ($msg) { return $msg }
    return '理由が分かりません'
}

# 1 つ取ってくる（本文を文字列で返す。取れなければ $null と $script:FetchReason）。
# -TimeoutSec で頭打ちにする（メニューから同期に叩かれる＝その間 Vectorworks は止まる
# ので、応答が返らないネットワークで待ち続けさせないために必須）。-UseBasicParsing は
# 古い Windows PowerShell で IE の初期設定に引きずられないため。
function Get-FetchText([string] $uri, [string] $accept) {
    try {
        $r = Invoke-WebRequest -Uri $uri -Headers @{ 'Accept' = $accept } `
            -UserAgent 'vw-probes-update' -TimeoutSec 20 -UseBasicParsing -Method Get
        $script:FetchReason = ''
        return [string] $r.Content
    }
    catch {
        $script:FetchReason = (Get-FetchReason $_) -replace '[\r\n]+', ' '
        return $null
    }
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

# 控えのテキストから 1 つ読む（無ければ 'none'）。
function Get-StampValue([string] $file, [string] $key) {
    $f = Join-Path $VW_PLUGINS_DIR $file
    if (Test-Path -LiteralPath $f) {
        $c = Get-Content -LiteralPath $f -Raw -ErrorAction SilentlyContinue
        $v = Get-Meta $c $key
        if ($v) { return $v }
    }
    return 'none'
}

# 入っている**本体一式**のビルド ID（カタログ "VwSdkProbes.probes.txt" の build=）。
# 本体は群ごとに分かれているが、**どれも同じビルドから出る**ので、ビルド ID は
# カタログが持っていれば足りる（plugin/cmake/ProbeCatalog.cmake）。
function Get-InstalledBuild {
    return Get-StampValue $VW_CATALOG 'build'
}

# 入っている**殻**の ID（"VwSdkProbes.build-info.txt" の shell=）。
# これが公開側と同じなら、本体だけ入れ替えれば済む＝再起動が要らない。
function Get-InstalledShell {
    return Get-StampValue "$VW_NAME.build-info.txt" 'shell'
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

# **本体一式**（群ごとの .vwpayload）とカタログを入れ替える。新しい版に無い本体は消す
# ——残すと、カタログに載っていないファイルだけが古いまま居座る（実機で「消したはずの
# PR のプローブが動く」の元になる）。失敗したら $false（理由は $script:LastError）。
function Install-Payloads([string] $work) {
    foreach ($f in Get-ChildItem -LiteralPath $work -Filter "$VW_PAYLOAD_PREFIX*.vwpayload" -ErrorAction SilentlyContinue) {
        try { Install-File $f.FullName (Join-Path $VW_PLUGINS_DIR $f.Name) }
        catch { $script:LastError = "本体（$($f.Name)）のコピーに失敗しました。"; return $false }
    }

    # **カタログは本体の後**。先に置くと、本体のコピーが途中で落ちたときに
    # 「カタログには載っているのにファイルが無い」状態が残る。
    $cat = Join-Path $work $VW_CATALOG
    if (Test-Path -LiteralPath $cat) {
        try { Install-File $cat (Join-Path $VW_PLUGINS_DIR $VW_CATALOG) }
        catch { $script:LastError = 'カタログのコピーに失敗しました。'; return $false }
    }

    # 今度の版に無い本体を消す。
    foreach ($f in Get-ChildItem -LiteralPath $VW_PLUGINS_DIR -Filter "$VW_PAYLOAD_PREFIX*.vwpayload" -ErrorAction SilentlyContinue) {
        if (-not (Test-Path -LiteralPath (Join-Path $work $f.Name))) {
            try { Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop } catch {}
        }
    }

    # 旧版（本体が 1 本だった頃）の置き土産も片付ける。
    foreach ($f in @('VwSdkProbesPayload.vwpayload', 'VwSdkProbesPayload.build-info.txt')) {
        $old = Join-Path $VW_PLUGINS_DIR $f
        if (Test-Path -LiteralPath $old) { try { Remove-Item -LiteralPath $old -Force -ErrorAction Stop } catch {} }
    }
    return $true
}

# zip を落として展開し、Plug-Ins フォルダへ入れ替える。成功なら $true。
function Install-Build([string] $url) {
    $script:LastError = ''
    if (-not $url) { $script:LastError = '引数が不足しています。'; return $false }

    $tmp = New-Item -ItemType Directory -Force -Path (Join-Path ([System.IO.Path]::GetTempPath()) ("vwprobes-" + [System.IO.Path]::GetRandomFileName()))
    try {
        $zip = Join-Path $tmp.FullName "$VW_NAME.vlb.zip"
        try { Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300 }
        catch {
            # **理由は取得のときと同じ形で残す**（HTTP のコードか WebException の種別）。
            $why = (Get-FetchReason $_) -replace '[\r\n]+', ' '
            $script:LastError = "ダウンロードに失敗しました（$why）。"
            return $false
        }

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

        # **zip の直下にあるものを、そのまま置く**（ファイル名を列挙しない）。列挙すると、
        # 配布物にファイルを足したときに**古い版のこのスクリプトが新しいファイルを
        # 置いてくれない**——入れ替えを行うのは常に「いま入っている＝古い」版だからで、
        # 実際に同梱スクリプトを 1 本足したときにそれを踏んだ。本体（.vwpayload）と
        # カタログだけは Install-Payloads が別に扱うので、ここでは飛ばす。
        foreach ($item in Get-ChildItem -LiteralPath $work -File -ErrorAction SilentlyContinue) {
            if ($item.Name -like "$VW_PAYLOAD_PREFIX*.vwpayload" -or $item.Name -eq $VW_CATALOG) { continue }
            try { Install-File $item.FullName (Join-Path $VW_PLUGINS_DIR $item.Name) }
            catch { $script:LastError = 'インストール先へのコピーに失敗しました。'; return $false }
        }
        # 本体一式とカタログも一緒に（殻と本体の版は揃っていなければならない）。
        return (Install-Payloads $work)
    }
    finally {
        try { Remove-Item -LiteralPath $tmp.FullName -Recurse -Force -ErrorAction SilentlyContinue } catch {}
    }
}

# **本体一式だけ**置き換える（Vectorworks を動かしたままでよい）。落とすのは Install-Build
# と同じ zip で、中から本体とカタログだけを取り出す。
function Install-Payload([string] $url) {
    $script:LastError = ''
    if (-not $url) { $script:LastError = '引数が不足しています。'; return $false }

    $tmp = New-Item -ItemType Directory -Force -Path (Join-Path ([System.IO.Path]::GetTempPath()) ("vwprobes-" + [System.IO.Path]::GetRandomFileName()))
    try {
        $zip = Join-Path $tmp.FullName "$VW_NAME.vlb.zip"
        try { Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300 }
        catch {
            # **理由は取得のときと同じ形で残す**（HTTP のコードか WebException の種別）。
            $why = (Get-FetchReason $_) -replace '[\r\n]+', ' '
            $script:LastError = "ダウンロードに失敗しました（$why）。"
            return $false
        }

        $work = Join-Path $tmp.FullName 'x'
        try { Expand-Archive -LiteralPath $zip -DestinationPath $work -Force }
        catch { $script:LastError = 'アーカイブの展開に失敗しました。'; return $false }

        if (-not (Test-Path -LiteralPath (Join-Path $work $VW_CATALOG))) {
            $script:LastError = "$VW_CATALOG が zip 内に見つかりません。"; return $false
        }

        if (-not (Test-Path -LiteralPath $VW_PLUGINS_DIR)) {
            New-Item -ItemType Directory -Force -Path $VW_PLUGINS_DIR | Out-Null
        }

        return (Install-Payloads $work)
    }
    finally {
        try { Remove-Item -LiteralPath $tmp.FullName -Recurse -Force -ErrorAction SilentlyContinue } catch {}
    }
}

function Invoke-Query {
    $body = ''
    $name = ''
    $url = $null

    $json = Get-FetchText "$VW_API/releases/tags/$VW_TAG" 'application/vnd.github+json'
    if ($json) {
        $rel = $null
        try { $rel = $json | ConvertFrom-Json } catch { $rel = $null }
        if ($rel) {
            $body = [string] $rel.body
            $name = [string] $rel.name
            $url = Get-AssetUrl $rel "$VW_NAME.vlb.zip"
        }
    }
    else {
        # **API が使えなくても更新はできる。** 資産の URL はタグと名前から決まるので、
        # リリースの素性だけを書いた小さなテキスト（$VW_INFO）を直接落として読む。
        # API の呼び出し上限（未認証は 1 時間あたり 60 回）や、api.github.com だけが
        # 塞がれている網でもここで通る。**両方の理由を持ったまま**進む。
        $apiReason = $script:FetchReason
        $info = Get-FetchText "$VW_DL/$VW_INFO" '*/*'
        if (-not $info) {
            Write-Output "error=リリース（$VW_TAG）を取得できませんでした。GitHub API: $apiReason／直接取得: $($script:FetchReason)"
            return
        }
        $body = $info
        $name = Get-Meta $body 'title'
        $url = "$VW_DL/$VW_NAME.vlb.zip"
    }

    $latest = Get-Meta $body 'build'
    $latestShell = Get-Meta $body 'shell'
    $probes = Get-Meta $body 'probes'
    if (-not $latest -or -not $url) {
        Write-Output 'error=リリースの情報が不完全です（ビルド ID か資産が見つかりません）。'
        return
    }

    Write-Output ("installed=" + (Get-InstalledBuild))
    Write-Output ("latest=" + $latest)
    Write-Output ("installedShell=" + (Get-InstalledShell))
    if ($latestShell) { Write-Output ("latestShell=" + $latestShell) }
    Write-Output ("url=" + $url)
    if ($name) { Write-Output ("title=" + $name) }
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

function Invoke-DoInstallPayload([string] $url) {
    if (Install-Payload $url) {
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
        'do-install-payload' { Invoke-DoInstallPayload ([string] $args[1]) }
        default { Write-Output "error=不明なモード: '$mode'（q / do-install / do-install-payload）。" }
    }
}
