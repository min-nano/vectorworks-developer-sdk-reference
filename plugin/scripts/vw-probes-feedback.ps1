<#
    vw-probes-feedback.ps1 — プローブの結果を、そのプローブが来た PR へ投稿する（Windows）。
    macOS 版 vw-probes-feedback.sh の相方で、仕組みの全体はあちらのヘッダと
    plugin/src/Feedback.h にある。

    プラグイン本体（plugin/src/Feedback.cpp）から**非対話**で呼ばれる裏方。ダイアログは
    すべてプラグイン側が出すので、こちらは機械可読な行を標準出力へ出すだけ。

      token-status                  トークンの出どころと使えるかどうか
      login <token-file>            ファイルのトークンを保存し、ファイルを消す
      logout                        保存したトークンを消す
      find-pr <repo> <branch>       そのブランチの open な PR 番号を引く
      issue-state <repo> <issue>    その issue が open か closed かを引く
      post <repo> <pr> <body-file>  PR（issue も同じ口）へコメントを 1 通投稿する

    成功は**素の `ok` 1 行**（post は先に url= を出す）。失敗は `error=<理由>` で、
    終了コードは 0 のまま——何を見せるかはプラグイン側が決める。

    **トークンをコマンドラインに乗せない。** `login` が受け取るのは*ファイルのパス*で、
    中身は読んだ直後に消す（引数はプロセス一覧から見えるため）。保存は DPAPI
    （ConvertFrom-SecureString）で、**同じ Windows ユーザーだけが復号できる**形にする。

    トークンの探索順:
      1. 環境変数 VW_PROBE_FEEDBACK_TOKEN
      2. %LOCALAPPDATA%\VwSdkProbes\feedback-token.dat（login で保存したもの）
      3. gh CLI の認証（入っていれば）

    必要なもの: Windows PowerShell 5.1+（Windows 同梱）または PowerShell 7。

    環境変数で上書きできる:
      VW_REPO                       owner/repo（引数の repo が空のときの既定）
      VW_PROBE_FEEDBACK_TOKEN       トークン（探索順 1）
      VW_PROBE_FEEDBACK_TOKEN_FILE  保存先の差し替え（試験用）
#>

#requires -version 5
$ErrorActionPreference = 'Stop'

# TLS 1.2 を優先し、UTF-8 で出す（プラグインが日本語の理由をそのまま読めるように。
# vw-probes-update.ps1 と同じ最善努力で、古いホストでは拒まれても構わない）。
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false } catch {}

$VW_REPO = if ($env:VW_REPO) { $env:VW_REPO } else { 'min-nano/vectorworks-developer-sdk-reference' }
$VW_API = 'https://api.github.com'

# 保存先のフォルダ名は**識別子なので据え置く**（付け替えると、入っているトークンが
# 行方不明になり、もう一度貼り付けさせることになる）。
function Get-TokenFilePath {
    if ($env:VW_PROBE_FEEDBACK_TOKEN_FILE) { return $env:VW_PROBE_FEEDBACK_TOKEN_FILE }
    $dir = Join-Path $env:LOCALAPPDATA 'VwSdkProbes'
    return (Join-Path $dir 'feedback-token.dat')
}

# 保存の暗号化。**DPAPI（ConvertFrom-SecureString の既定）**なので、復号できるのは
# 保存した Windows ユーザー本人だけ——他人のプロファイルへ持ち出しても読めない。
function Protect-TokenText {
    # PSAvoidUsingConvertToSecureStringWithPlainText は「平文から SecureString を作るな」
    # という規則だが、ここでの SecureString は**保管を暗号化するための通り道**であって、
    # 平文をメモリから隠すためのものではない（トークンは呼び出し元が平文で持っている）。
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute(
        'PSAvoidUsingConvertToSecureStringWithPlainText', '',
        Justification = 'SecureString is only the route to DPAPI-at-rest; the token is already plaintext here.')]
    param([string] $PlainText)
    $secure = ConvertTo-SecureString -String $PlainText -AsPlainText -Force
    return (ConvertFrom-SecureString -SecureString $secure)
}

function Unprotect-TokenText {
    param([string] $Protected)
    $secure = ConvertTo-SecureString -String $Protected
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

function Get-StoredToken {
    $path = Get-TokenFilePath
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try {
        $encrypted = Get-Content -LiteralPath $path -Raw
        if (-not $encrypted) { return $null }
        return (Unprotect-TokenText -Protected $encrypted.Trim())
    } catch {
        return $null
    }
}

function Get-GhToken {
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) { return $null }
    try {
        $token = & $gh.Source auth token 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        if ($token) { return ([string]$token).Trim() }
    } catch {
        return $null
    }
    return $null
}

# どこから取れるか（取れなければ 'none'）。**トークン自体は返さない。**
function Get-TokenSource {
    if ($env:VW_PROBE_FEEDBACK_TOKEN) { return 'env' }
    if (Get-StoredToken) { return 'stored' }
    if (Get-GhToken) { return 'gh' }
    return 'none'
}

function Resolve-Token {
    if ($env:VW_PROBE_FEEDBACK_TOKEN) { return $env:VW_PROBE_FEEDBACK_TOKEN }
    $stored = Get-StoredToken
    if ($stored) { return $stored }
    return (Get-GhToken)
}

# ---------------------------------------------------------------------------
# モード
# ---------------------------------------------------------------------------

function Invoke-TokenStatus {
    $source = Get-TokenSource
    Write-Output "source=$source"
    if ($source -eq 'none') { Write-Output 'ok=no' } else { Write-Output 'ok=yes' }
}

function Invoke-Login {
    param([string] $TokenFile)

    if (-not $TokenFile -or -not (Test-Path -LiteralPath $TokenFile)) {
        Write-Output 'error=トークンのファイルが見つかりません。'
        return
    }
    $token = (Get-Content -LiteralPath $TokenFile -Raw)
    Remove-Item -LiteralPath $TokenFile -Force -ErrorAction SilentlyContinue
    if ($token) { $token = $token.Trim() }
    if (-not $token) {
        Write-Output 'error=トークンが空です。'
        return
    }
    try {
        $path = Get-TokenFilePath
        $dir = Split-Path -Parent $path
        if ($dir -and -not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        Set-Content -LiteralPath $path -Value (Protect-TokenText -PlainText $token) -NoNewline
        Write-Output 'ok'
    } catch {
        Write-Output 'error=トークンを保存できませんでした。'
    }
}

function Invoke-Logout {
    $path = Get-TokenFilePath
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    Write-Output 'ok'
}

# find-pr <repo> <branch>: そのブランチの open な PR。出所に PR 番号が無いビルド
# （PR のブランチでビルドしたものを手で入れたとき）の逃げ道で、**トークンは要らない**
# （公開リポジトリの open な PR を引くだけ）。
function Invoke-FindPr {
    param([string] $Repo, [string] $Branch)

    if (-not $Repo) { $Repo = $VW_REPO }
    if (-not $Branch) {
        Write-Output 'error=ブランチが指定されていません。'
        return
    }
    $owner = $Repo.Split('/')[0]
    $headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'VwSdkProbes' }
    $token = Resolve-Token
    if ($token) { $headers['Authorization'] = "Bearer $token" }
    try {
        $url = "$VW_API/repos/$Repo/pulls?state=open&head=$owner`:$Branch"
        $pulls = Invoke-RestMethod -Uri $url -Headers $headers -TimeoutSec 20
    } catch {
        Write-Output 'error=PR を検索できませんでした（ネットワークか権限）。'
        return
    }
    if (-not $pulls -or $pulls.Count -eq 0) {
        Write-Output "error=ブランチ $Branch に open な PR がありません。"
        return
    }
    Write-Output "pr=$($pulls[0].number)"
    if ($pulls[0].title) { Write-Output "title=$($pulls[0].title)" }
    Write-Output 'ok'
}

# issue-state <repo> <issue>: その issue が open か closed か。**PR が見つからないとき
# の宛先候補**（プローブ本体の `[issue #N]`）が使える状態かを確かめる口で、
# **閉じた issue へは投稿しない**（読まれない）ための判定に使う。トークンは要らない
# （find-pr と同じ理由）。
function Invoke-IssueState {
    param([string] $Repo, [string] $Number)

    if (-not $Repo) { $Repo = $VW_REPO }
    if (-not $Number) {
        Write-Output 'error=issue 番号が指定されていません。'
        return
    }
    $headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'VwSdkProbes' }
    $token = Resolve-Token
    if ($token) { $headers['Authorization'] = "Bearer $token" }
    try {
        $issue = Invoke-RestMethod -Uri "$VW_API/repos/$Repo/issues/$Number" -Headers $headers -TimeoutSec 20
    } catch {
        Write-Output 'error=issue を確認できませんでした（ネットワークか権限）。'
        return
    }
    if (-not $issue.state) {
        Write-Output "error=issue #$Number が見つかりません。"
        return
    }
    Write-Output "state=$($issue.state)"
    Write-Output 'ok'
}

# post <repo> <pr> <body-file>: PR へコメントを 1 通。本文は UTF-8 のまま送る
# （ConvertTo-Json が JSON のエスケープを引き受けるので、自前の文字列連結はしない）。
function Invoke-Post {
    param([string] $Repo, [string] $Number, [string] $BodyFile)

    if (-not $Repo) { $Repo = $VW_REPO }
    if (-not $Number -or -not $BodyFile -or -not (Test-Path -LiteralPath $BodyFile)) {
        Write-Output 'error=引数が不足しています。'
        return
    }
    $token = Resolve-Token
    if (-not $token) {
        Write-Output 'error=GitHub のトークンがありません（先に登録してください）。'
        return
    }

    $body = Get-Content -LiteralPath $BodyFile -Raw
    $payload = @{ body = $body } | ConvertTo-Json -Depth 3 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
    $headers = @{
        Accept        = 'application/vnd.github+json'
        Authorization = "Bearer $token"
        'User-Agent'  = 'VwSdkProbes'
    }
    try {
        $result = Invoke-RestMethod -Uri "$VW_API/repos/$Repo/issues/$Number/comments" `
            -Method Post -Headers $headers -ContentType 'application/json; charset=utf-8' `
            -Body $bytes -TimeoutSec 60
    } catch {
        # GitHub の言い分をそのまま渡す（権限不足か PR 違いかが、これで切り分けられる）。
        $reason = $_.Exception.Message
        Write-Output "error=コメントを投稿できませんでした（$reason）。"
        return
    }
    if ($result.html_url) { Write-Output "url=$($result.html_url)" }
    Write-Output 'ok'
}

# ---------------------------------------------------------------------------
# 引数を 1 つ取り出す（無ければ空文字）。範囲外の添字で落ちないようにするだけの道具。
function Get-Argument {
    param([string[]] $Arguments, [int] $Index)
    if ($null -eq $Arguments -or $Index -ge $Arguments.Count) { return '' }
    return [string] $Arguments[$Index]
}

function Invoke-Main {
    param([string[]] $Arguments)

    $mode = Get-Argument $Arguments 0
    switch ($mode) {
        'token-status' { Invoke-TokenStatus }
        'login'        { Invoke-Login -TokenFile (Get-Argument $Arguments 1) }
        'logout'       { Invoke-Logout }
        'find-pr'      { Invoke-FindPr -Repo (Get-Argument $Arguments 1) -Branch (Get-Argument $Arguments 2) }
        'issue-state'  { Invoke-IssueState -Repo (Get-Argument $Arguments 1) -Number (Get-Argument $Arguments 2) }
        'post'         {
            Invoke-Post -Repo (Get-Argument $Arguments 1) -Number (Get-Argument $Arguments 2) `
                -BodyFile (Get-Argument $Arguments 3)
        }
        default        { Write-Output "error=不明なモード: '$mode'（token-status / login / logout / find-pr / issue-state / post）。" }
    }
}

# 実行されたときだけ走らせる（dot-source では走らせない。テストが差し替えられるように）。
if ($MyInvocation.InvocationName -ne '.') {
    Invoke-Main -Arguments $args
}
