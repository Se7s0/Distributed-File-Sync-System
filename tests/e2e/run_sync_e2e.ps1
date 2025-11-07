param(
    [string]$Server      = "http://localhost:8080",
    [string]$LocalFile   = "${PSScriptRoot}\sample\note.txt",
    [string]$RemotePath  = "docs/note.txt",
    [string]$OutputFile  = "${PSScriptRoot}\sample\note_downloaded.txt",
    [int]   $ChunkSize   = 16384
)

if (-not ([System.Management.Automation.PSTypeName]'SyncE2E.HashUtils').Type) {
    Add-Type @"
    using System;

    namespace SyncE2E {
        public static class HashUtils {
            public static ulong Fnv1a(byte[] data) {
                const ulong offset = 14695981039346656037UL;
                const ulong prime = 1099511628211UL;
                ulong hash = offset;
                foreach (var b in data) {
                    hash ^= b;
                    hash *= prime;
                }
                return hash;
            }
        }
    }
"@
}

function Get-Fnv1aHex {
    param([byte[]]$Bytes)
    return [SyncE2E.HashUtils]::Fnv1a($Bytes).ToString('x16')
}

function To-HexString {
    param([byte[]]$Bytes)
    return ([System.BitConverter]::ToString($Bytes) -replace '-', '')
}

if (-not (Test-Path $LocalFile)) {
    throw "Local file '$LocalFile' not found."
}

$bytes = [System.IO.File]::ReadAllBytes($LocalFile)
$expectedHash = Get-Fnv1aHex $bytes

$metadataItem = @{
    file_path     = $RemotePath
    hash          = $expectedHash
    size          = $bytes.Length
    modified_time = [int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    created_time  = [int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    sync_state    = 1
}

Write-Host "Registering client..."
$register = Invoke-RestMethod -Uri "$Server/api/register" -Method POST -ContentType 'application/json' -Body (@{ preferred_id = 'e2e-client' } | ConvertTo-Json)
$clientId = $register.client_id
Write-Host " client_id = $clientId"

Write-Host "Starting session..."
$start = Invoke-RestMethod -Uri "$Server/api/sync/start" -Method POST -ContentType 'application/json' -Body (@{ client_id = $clientId } | ConvertTo-Json)
$sessionId = $start.session.session_id
Write-Host " session_id = $sessionId"

Write-Host "Computing diff..."
Invoke-RestMethod -Uri "$Server/api/sync/diff" -Method POST -ContentType 'application/json' -Body (@{ session_id = $sessionId; snapshot = @($metadataItem) } | ConvertTo-Json -Depth 5) | Out-Null
$diffResponse = Invoke-RestMethod -Uri "$Server/api/sync/diff" -Method POST -ContentType 'application/json' -Body (@{ session_id = $sessionId; snapshot = @($metadataItem) } | ConvertTo-Json -Depth 5)
$uploadList = $diffResponse.files_to_upload
if (-not $uploadList -or $uploadList.Count -eq 0) {
    throw "Server did not request any uploads."
}

$totalChunks = [Math]::Ceiling($bytes.Length / $ChunkSize)
Write-Host "Uploading $($bytes.Length) bytes in $totalChunks chunk(s)..."
for ($i = 0; $i -lt $totalChunks; $i++) {
    $startIndex = $i * $ChunkSize
    $length = [Math]::Min($ChunkSize, $bytes.Length - $startIndex)
    $slice = New-Object byte[] $length
    [Array]::Copy($bytes, $startIndex, $slice, 0, $length)
    $hexData = To-HexString $slice
    $chunkHash = Get-Fnv1aHex $slice

    Invoke-RestMethod -Uri "$Server/api/file/upload_chunk" -Method POST -ContentType 'application/json' -Body (@{
        session_id   = $sessionId
        file_path    = $RemotePath
        chunk_index  = $i
        total_chunks = $totalChunks
        chunk_size   = $ChunkSize
        data         = $hexData
        chunk_hash   = $chunkHash
    } | ConvertTo-Json) | Out-Null
}

Write-Host "Finalizing upload..."
Invoke-RestMethod -Uri "$Server/api/file/upload_complete" -Method POST -ContentType 'application/json' -Body (@{
    session_id    = $sessionId
    file_path     = $RemotePath
    expected_hash = $expectedHash
} | ConvertTo-Json) | Out-Null

$status = Invoke-RestMethod -Uri "$Server/api/sync/status" -Method POST -ContentType 'application/json' -Body (@{ session_id = $sessionId } | ConvertTo-Json)
$download = Invoke-RestMethod -Uri "$Server/api/file/download" -Method POST -ContentType 'application/json' -Body (@{ file_path = $RemotePath } | ConvertTo-Json)

$downloadBytes = [byte[]]::new($download.data.Length / 2)
for ($i = 0; $i -lt $downloadBytes.Length; $i++) {
    $downloadBytes[$i] = [Convert]::ToByte($download.data.Substring($i * 2, 2), 16)
}

if (-not ($download.hash -eq $expectedHash)) {
    throw "Hash mismatch. Expected $expectedHash but server reported $($download.hash)."
}

if (-not ([System.Linq.Enumerable]::SequenceEqual($downloadBytes, $bytes))) {
    throw "Downloaded data does not match original file."
}

[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($OutputFile)) | Out-Null
[System.IO.File]::WriteAllBytes($OutputFile, $downloadBytes)

Write-Host "Session state: $($status.state)"
Write-Host "Downloaded hash: $($download.hash)"
Write-Host "Downloaded file saved to $OutputFile"
Write-Host "E2E sync succeeded."
