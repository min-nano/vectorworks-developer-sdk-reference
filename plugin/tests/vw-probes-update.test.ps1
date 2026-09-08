<#
    vw-probes-update.test.ps1

    同梱スクリプト（plugin/scripts/vw-probes-update.ps1）の単体テスト——Windows 側の
    入れ替えの裏方のうち、**取得と、失敗したときの理由**を押さえる。

    ここを試験にした理由: 「ネットワークは生きているのにリリースを取得できませんでした」と
    だけ出て、原因が分からない状態が実際に起きた。**理由を具体的に言うこと**と、
    **API が駄目でも決まった URL から引けること**は、この道具が自分で直せるかどうかを
    決めるので、振る舞いとして固定する。Windows 側は実機でしか動かないぶん、
    ここが唯一の門になる（mac 側は plugin/tests/vw-probes-update.test.sh と対）。

    スクリプトは**ドットソースして**（本物の入口は末尾で守られている）、いちばん外側の
    入出力だけを差し替える:

      * Invoke-WebRequest … 網の境目。関数はコマンドレットより先に解決されるので、
                            同名の関数を置くだけで差し替わる。
      * 例外              … Get-FetchReason が見るのは Exception の Response /
                            Status / Message だけなので、その形の**作り物**を渡す
                            （HttpWebResponse は組み立てられないため）。

    走らせ方（CI の lint ワークフローも同じ。ランナーには pwsh が入っている）:
        pwsh -File plugin/tests/vw-probes-update.test.ps1
#>

$ErrorActionPreference = 'Stop'

$HERE = Split-Path -Parent $MyInvocation.MyCommand.Path
$SCRIPT = Join-Path $HERE '../scripts/vw-probes-update.ps1'
if (-not (Test-Path -LiteralPath $SCRIPT)) {
    Write-Host "ERROR: $SCRIPT がありません。"
    exit 1
}

# ---------------------------------------------------------------------------
# 小さなハーネス。
# ---------------------------------------------------------------------------
$script:Failures = 0
$script:Checks = 0

function Check([string] $desc, $actual, $expected) {
    $script:Checks++
    if ("$actual" -ceq "$expected") { Write-Host "[ PASS ] $desc" }
    else {
        Write-Host "[ FAIL ] $desc"
        Write-Host "         expected: $expected"
        Write-Host "         actual:   $actual"
        $script:Failures++
    }
}

function CheckContains([string] $desc, $haystack, [string] $needle) {
    $script:Checks++
    if ("$haystack".Contains($needle)) { Write-Host "[ PASS ] $desc" }
    else {
        Write-Host "[ FAIL ] $desc"
        Write-Host "         expected to contain: $needle"
        Write-Host "         actual:              $haystack"
        $script:Failures++
    }
}

function CheckNotContains([string] $desc, $haystack, [string] $needle) {
    $script:Checks++
    if ("$haystack".Contains($needle)) {
        Write-Host "[ FAIL ] $desc"
        Write-Host "         must NOT contain: $needle"
        Write-Host "         actual:           $haystack"
        $script:Failures++
    }
    else { Write-Host "[ PASS ] $desc" }
}

# ---------------------------------------------------------------------------
# 環境: 入れ替え先は作業ディレクトリ（本物の Plug-Ins には触らない）。
# ---------------------------------------------------------------------------
$WORK = Join-Path ([System.IO.Path]::GetTempPath()) ("vwprobes-test-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Force -Path $WORK | Out-Null
$env:VW_REPO = 'min-nano/vectorworks-developer-sdk-reference'
$env:VW_PLUGINS_DIR = $WORK
$env:VW_TAG = 'probes'
Set-Content -LiteralPath (Join-Path $WORK 'VwSdkProbes.probes.txt') -Value 'build=INSTALLED1'

. $SCRIPT

$DL_BASE = 'https://github.com/min-nano/vectorworks-developer-sdk-reference/releases/download/probes'

# Invoke-WebRequest の代用。宛先ごとに「答える／例外を投げる」を切り替える。
$script:ApiThrow = $null
$script:DlThrow = $null
$script:ApiBody = '{"name":"Probes (abc1234)","body":"<!-- vw-probes\nbuild=BUILD123\nshell=SHELL123\nprobes=alpha,beta\n-->","assets":[{"name":"VwSdkProbes.vlb.zip","browser_download_url":"https://example.invalid/vlb.zip"}]}'
$script:DlBody = "build=BUILD777`nshell=SHELL777`nprobes=gamma`ntitle=Probes (zzz + PR 9)`n"

function Invoke-WebRequest {
    param([string] $Uri, $Headers, $UserAgent, $TimeoutSec, [switch] $UseBasicParsing, $Method, $OutFile)
    if ($Uri -like 'https://api.github.com/*') {
        if ($script:ApiThrow) { throw $script:ApiThrow }
        return [pscustomobject]@{ Content = $script:ApiBody }
    }
    if ($script:DlThrow) { throw $script:DlThrow }
    return [pscustomobject]@{ Content = $script:DlBody }
}

# 例外の作り物（Get-FetchReason が見るのは Response / Status / Message だけ）。
function New-WebErr($status, [string] $msg) {
    return [System.Net.WebException]::new($msg, $status)
}
function New-HttpErr([int] $code, $headers) {
    return [pscustomobject]@{
        Exception = [pscustomobject]@{
            Response = [pscustomobject]@{ StatusCode = $code; Headers = $headers }
            Status   = 'ProtocolError'
            Message  = "The remote server returned an error: ($code)."
        }
    }
}

# ---------------------------------------------------------------------------
# q: API が答えるとき（ふだんの道）。
# ---------------------------------------------------------------------------
$out = (Invoke-Query) -join "`n"
Check 'q: API から本体・殻・資産を読む' $out @'
installed=INSTALLED1
latest=BUILD123
installedShell=none
latestShell=SHELL123
url=https://example.invalid/vlb.zip
title=Probes (abc1234)
probes=alpha,beta
'@.Trim()

# ---------------------------------------------------------------------------
# q: API が駄目なとき——**決まった URL から引き直して先へ進む**。
# ---------------------------------------------------------------------------
$script:ApiThrow = New-WebErr ([System.Net.WebExceptionStatus]::NameResolutionFailure) '模擬エラー'
$out = (Invoke-Query) -join "`n"
Check 'q: API が駄目でも資産の URL から素性を読む' $out @"
installed=INSTALLED1
latest=BUILD777
installedShell=none
latestShell=SHELL777
url=$DL_BASE/VwSdkProbes.vlb.zip
title=Probes (zzz + PR 9)
probes=gamma
"@.Trim()
CheckNotContains '逃げ道が通ったときは error を出さない' $out 'error='

# ---------------------------------------------------------------------------
# q: 両方駄目なとき——**両方の理由を 1 行で**言う（error= は 1 行しか読まれない）。
# ---------------------------------------------------------------------------
$script:DlThrow = New-WebErr ([System.Net.WebExceptionStatus]::ConnectFailure) '模擬エラー2'
$lines = @(Invoke-Query)
Check '両方駄目なら error= の 1 行だけ' $lines.Count 1
CheckContains 'API 側の理由を言う' $lines[0] 'GitHub API: 名前解決に失敗しました（DNS）'
CheckContains '直接取得側の理由も言う' $lines[0] '直接取得: 接続できません'
CheckNotContains '「ネットワークを確認してください」で終わらせない' $lines[0] 'ネットワークを確認してください'

$script:ApiThrow = $null
$script:DlThrow = $null

# ---------------------------------------------------------------------------
# 理由の文言（HTTP のコードから）。
# ---------------------------------------------------------------------------
$reset = [int] [math]::Floor(([datetime]::UtcNow.AddMinutes(10) - [datetime] '1970-01-01').TotalSeconds)
$why = Get-FetchReason (New-HttpErr 403 @{ 'X-RateLimit-Remaining' = '0'; 'X-RateLimit-Reset' = "$reset" })
CheckContains '上限に達した 403 はそう言う' $why 'GitHub API の呼び出し上限に達しました'
CheckContains 'いつ戻るかも言う' $why 'あと 10 分で戻ります'

$why = Get-FetchReason (New-HttpErr 403 @{ 'X-RateLimit-Remaining' = '57' })
Check '上限でない 403 は拒否として言う' $why 'HTTP 403: 拒否されました（プロキシや社内フィルタの可能性）'

$why = Get-FetchReason (New-HttpErr 404 @{})
Check '404 は「見つかりません」' $why 'HTTP 404: 見つかりません（リリースか資産がまだありません）'

$why = Get-FetchReason (New-HttpErr 503 @{})
Check '5xx は GitHub 側の障害' $why 'HTTP 503: GitHub 側の一時的な障害'

$why = Get-FetchReason ([pscustomobject]@{
        Exception = [pscustomobject]@{
            Response = $null
            Status   = 'TrustFailure'
            Message  = "証明書が`n検証できません"
        }
    })
CheckContains '証明書の失敗は TLS だと言う' $why 'TLS の検証に失敗しました（証明書）'
CheckNotContains '理由は 1 行に畳む' $why "`n"

Remove-Item -LiteralPath $WORK -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($script:Failures -eq 0) {
    Write-Host "すべて通りました（$script:Checks 件）。"
    exit 0
}
Write-Host "$script:Failures 件失敗しました（$script:Checks 件中）。"
exit 1
