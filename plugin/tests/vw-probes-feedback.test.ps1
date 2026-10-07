<#
    vw-probes-feedback.test.ps1

    同梱スクリプト（plugin/scripts/vw-probes-feedback.ps1）の単体テスト——Windows 側の
    結果投稿のうち、**GitHub へ送る JSON の形**を押さえる。

    ここを試験にした理由: Windows 実機（Windows PowerShell 5.1）で投稿が
    「(422) Unprocessable Entity」で失敗した。5.1 の Get-Content -Raw が返す文字列には
    PSPath などの拡張プロパティが付いていて、ConvertTo-Json が `body` を**オブジェクト**に
    してしまっていた（ついでに BOM の無い本文を CP932 で読んで日本語も化けていた）。
    ランナーの pwsh 7 ではどちらも起きないので、ここでは「本文は UTF-8 のバイトどおり、
    `body` は**文字列**」という結果を固定し、本文の読み方を Get-Content に戻させない。

    走らせ方（CI の lint ワークフローも同じ）:
        pwsh -File plugin/tests/vw-probes-feedback.test.ps1
#>

$ErrorActionPreference = 'Stop'

$HERE = Split-Path -Parent $MyInvocation.MyCommand.Path
$SCRIPT = Join-Path $HERE '../scripts/vw-probes-feedback.ps1'
if (-not (Test-Path -LiteralPath $SCRIPT)) {
    Write-Host "ERROR: $SCRIPT がありません。"
    exit 1
}

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

$WORK = Join-Path ([System.IO.Path]::GetTempPath()) ("vwprobes-fb-test-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Force -Path $WORK | Out-Null
$env:VW_PROBE_FEEDBACK_TOKEN = 'test-token'

. $SCRIPT

# プラグインと同じく **BOM 無しの UTF-8** で本文を書く（日本語・改行・引用符・バックスラッシュ入り）。
$expected = "<!-- vw-probes-result v1 probe=x -->`n=== 結果: 成功 ===`n""引用"" と C:\Users\x`n"
$bodyFile = Join-Path $WORK 'body.txt'
[IO.File]::WriteAllBytes($bodyFile, [Text.Encoding]::UTF8.GetBytes($expected))

# ---------------------------------------------------------------------------
# New-CommentPayload: 本文はバイトどおり、body は文字列。
# ---------------------------------------------------------------------------
$bytes = New-CommentPayload -BodyFile $bodyFile
Check 'バイト列で返す' ($bytes -is [byte[]]) 'True'
$json = [Text.Encoding]::UTF8.GetString($bytes)
$parsed = ConvertFrom-Json $json
Check 'body は文字列（オブジェクトにしない）' ($parsed.body -is [string]) 'True'
Check '本文は UTF-8 のまま往復する' $parsed.body $expected
Check '鍵は body だけ' (($parsed.PSObject.Properties | ForEach-Object Name) -join ',') 'body'

# ---------------------------------------------------------------------------
# Invoke-Post: 送るのはそのバイト列で、成功なら url= と ok。
# ---------------------------------------------------------------------------
$script:Sent = $null
$script:SentUri = $null
function Invoke-RestMethod {
    param($Uri, $Method, $Headers, $ContentType, $Body, $TimeoutSec)
    $script:SentUri = $Uri
    $script:Sent = $Body
    return [pscustomobject]@{ html_url = 'https://github.com/o/r/pull/9#issuecomment-1' }
}
$out = (Invoke-Post -Repo 'o/r' -Number '9' -BodyFile $bodyFile) -join "`n"
Check '宛先は issues/<番号>/comments' $script:SentUri 'https://api.github.com/repos/o/r/issues/9/comments'
$sentBody = (ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([byte[]] $script:Sent))).body
Check '送った本文はファイルどおり' $sentBody $expected
Check '成功は url= と ok' $out "url=https://github.com/o/r/pull/9#issuecomment-1`nok"

Remove-Item -LiteralPath $WORK -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($script:Failures -eq 0) {
    Write-Host "すべて通りました（$script:Checks 件）。"
    exit 0
}
Write-Host "$script:Failures 件失敗しました（$script:Checks 件中）。"
exit 1
