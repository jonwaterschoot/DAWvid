#Requires -Version 5.1
<#
  DAWvid installer — Windows Forms GUI
  Re-launches itself elevated when writing to Program Files.
  Pass -AutoInstall to skip the button click (used by the re-launched elevated process).
#>
param(
    [string]$InstallPath    = "",
    [string]$ControllerPath = "",
    [switch]$AutoInstall
)

$ErrorLog   = Join-Path $env:TEMP "dawvid_install_error.log"
$ScriptDir  = if ($PSScriptRoot)  { $PSScriptRoot }  else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ScriptFile = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Path }

try {

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ── Helpers ───────────────────────────────────────────────────────────────────

function Test-Admin {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

$isAdmin = Test-Admin

function Invoke-AsAdmin ([string]$ClapDest, [string]$CtrlDest) {
    $ec      = $ClapDest.Replace('"', '\"')
    $ct      = $CtrlDest.Replace('"', '\"')
    $argList = "-ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden " +
               "-File `"$ScriptFile`" -InstallPath `"$ec`" " +
               "-ControllerPath `"$ct`" -AutoInstall"
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
}

# ── Locate files (everything lives next to this script) ───────────────────────

$here         = $ScriptDir
$clap         = Join-Path $here "DAWvid.clap"
$dlls         = @(Get-ChildItem $here -Filter "*.dll" |
                  Where-Object { $_.Name -match "^(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll$" } |
                  Select-Object -ExpandProperty FullName)
$controllerJs = Join-Path $here "DAWvid.control.js"
$hasCtrl      = Test-Path $controllerJs

if (-not (Test-Path $clap)) {
    [System.Windows.Forms.MessageBox]::Show(
        "DAWvid.clap was not found in the installer folder.`n`nPlease re-download the installer package.",
        "DAWvid Installer",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}

$allFiles = @($clap) + $dlls

# ── Default paths ─────────────────────────────────────────────────────────────

$defaultDest     = Join-Path $env:CommonProgramFiles "CLAP\DAWvid"
$defaultCtrlDest = Join-Path $env:USERPROFILE "Documents\Bitwig Studio\Controller Scripts\DAWvid"

if ($InstallPath    -eq "") { $InstallPath    = $defaultDest }
if ($ControllerPath -eq "") { $ControllerPath = $defaultCtrlDest }

# ── Colours & fonts ───────────────────────────────────────────────────────────

$C_BG      = [Drawing.Color]::FromArgb( 28,  28,  28)
$C_PANEL   = [Drawing.Color]::FromArgb( 38,  38,  38)
$C_BORDER  = [Drawing.Color]::FromArgb( 58,  58,  58)
$C_ACCENT  = [Drawing.Color]::FromArgb(  0, 120, 212)
$C_TEXT    = [Drawing.Color]::FromArgb(220, 220, 220)
$C_DIM     = [Drawing.Color]::FromArgb(130, 130, 130)
$C_GREEN   = [Drawing.Color]::FromArgb( 76, 195, 112)
$C_RED     = [Drawing.Color]::FromArgb(225,  80,  75)
$C_WARN_BG = [Drawing.Color]::FromArgb( 70,  52,   8)
$C_WARN    = [Drawing.Color]::FromArgb(255, 196,  50)

$FNT_TITLE = New-Object Drawing.Font("Segoe UI", 15)
$FNT_BODY  = New-Object Drawing.Font("Segoe UI",  9)
$FNT_SMALL = New-Object Drawing.Font("Segoe UI",  8)
$FNT_BTN   = New-Object Drawing.Font("Segoe UI", 10)

# ── Form ──────────────────────────────────────────────────────────────────────

$formH = if ($hasCtrl) { 608 } else { 460 }

$form                 = New-Object Windows.Forms.Form
$form.Text            = "DAWvid Installer"
$form.ClientSize      = New-Object Drawing.Size(500, $formH)
$form.StartPosition   = "CenterScreen"
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox     = $false
$form.BackColor       = $C_BG
$form.Font            = $FNT_BODY

# ── Header ────────────────────────────────────────────────────────────────────

$pnlHeader           = New-Object Windows.Forms.Panel
$pnlHeader.Location  = New-Object Drawing.Point(0, 0)
$pnlHeader.Size      = New-Object Drawing.Size(500, 80)
$pnlHeader.BackColor = $C_PANEL
$form.Controls.Add($pnlHeader)

$lblTitle            = New-Object Windows.Forms.Label
$lblTitle.Text       = "DAWvid"
$lblTitle.Font       = $FNT_TITLE
$lblTitle.ForeColor  = $C_TEXT
$lblTitle.Location   = New-Object Drawing.Point(20, 14)
$lblTitle.AutoSize   = $true
$pnlHeader.Controls.Add($lblTitle)

$lblVersion          = New-Object Windows.Forms.Label
$lblVersion.Text     = "v1.0.0  -  CLAP plugin"
$lblVersion.Font     = $FNT_SMALL
$lblVersion.ForeColor = $C_DIM
$lblVersion.Location = New-Object Drawing.Point(22, 46)
$lblVersion.AutoSize = $true
$pnlHeader.Controls.Add($lblVersion)

# ── Admin notice (visible only when not already elevated) ─────────────────────

$pnlNotice           = New-Object Windows.Forms.Panel
$pnlNotice.Location  = New-Object Drawing.Point(0, 80)
$pnlNotice.Size      = New-Object Drawing.Size(500, 24)
$pnlNotice.BackColor = $C_WARN_BG
$pnlNotice.Visible   = (-not $isAdmin)
$form.Controls.Add($pnlNotice)

$lblNotice           = New-Object Windows.Forms.Label
$lblNotice.Text      = "  Administrator permission will be requested when you click Install"
$lblNotice.Font      = $FNT_SMALL
$lblNotice.ForeColor = $C_WARN
$lblNotice.Location  = New-Object Drawing.Point(0, 5)
$lblNotice.Size      = New-Object Drawing.Size(500, 16)
$pnlNotice.Controls.Add($lblNotice)

# ── CLAP plugin section ───────────────────────────────────────────────────────
# Y=110: below header(80) + notice(24) + gap(6)

$y = 110

$lblLocTitle          = New-Object Windows.Forms.Label
$lblLocTitle.Text     = "CLAP PLUGIN LOCATION"
$lblLocTitle.Font     = $FNT_SMALL
$lblLocTitle.ForeColor = $C_DIM
$lblLocTitle.Location = New-Object Drawing.Point(20, $y)
$lblLocTitle.AutoSize = $true
$form.Controls.Add($lblLocTitle)

$txtPath              = New-Object Windows.Forms.TextBox
$txtPath.Text         = $InstallPath
$txtPath.Location     = New-Object Drawing.Point(20, ($y + 20))
$txtPath.Size         = New-Object Drawing.Size(370, 26)
$txtPath.BackColor    = $C_PANEL
$txtPath.ForeColor    = $C_TEXT
$txtPath.BorderStyle  = "FixedSingle"
$form.Controls.Add($txtPath)

$btnBrowse            = New-Object Windows.Forms.Button
$btnBrowse.Text       = "Browse…"
$btnBrowse.Location   = New-Object Drawing.Point(398, ($y + 19))
$btnBrowse.Size       = New-Object Drawing.Size(82, 28)
$btnBrowse.FlatStyle  = "Flat"
$btnBrowse.BackColor  = $C_PANEL
$btnBrowse.ForeColor  = $C_TEXT
$btnBrowse.FlatAppearance.BorderColor = $C_BORDER
$btnBrowse.Add_Click({
    $dlg              = New-Object Windows.Forms.FolderBrowserDialog
    $dlg.Description  = "Choose the CLAP plugin folder"
    $dlg.SelectedPath = $txtPath.Text
    if ($dlg.ShowDialog() -eq "OK") { $txtPath.Text = $dlg.SelectedPath }
})
$form.Controls.Add($btnBrowse)

$lblHint             = New-Object Windows.Forms.Label
$lblHint.Text        = "Default: C:\Program Files\Common Files\CLAP  (standard location all DAWs scan)"
$lblHint.Font        = $FNT_SMALL
$lblHint.ForeColor   = $C_DIM
$lblHint.Location    = New-Object Drawing.Point(20, ($y + 50))
$lblHint.Size        = New-Object Drawing.Size(460, 18)
$form.Controls.Add($lblHint)

$lblFilesTitle        = New-Object Windows.Forms.Label
$lblFilesTitle.Text   = "FILES TO INSTALL"
$lblFilesTitle.Font   = $FNT_SMALL
$lblFilesTitle.ForeColor = $C_DIM
$lblFilesTitle.Location  = New-Object Drawing.Point(20, ($y + 76))
$lblFilesTitle.AutoSize  = $true
$form.Controls.Add($lblFilesTitle)

$pnlFiles             = New-Object Windows.Forms.Panel
$pnlFiles.Location    = New-Object Drawing.Point(20, ($y + 96))
$pnlFiles.Size        = New-Object Drawing.Size(460, 144)
$pnlFiles.BackColor   = $C_PANEL
$pnlFiles.BorderStyle = "FixedSingle"
$form.Controls.Add($pnlFiles)

$yOff = 8
foreach ($f in $allFiles) {
    $name = Split-Path -Leaf $f
    $icon = if ($name -like "*.clap") { "◈" } else { "⬡" }
    $lbl             = New-Object Windows.Forms.Label
    $lbl.Text        = "  $icon  $name"
    $lbl.ForeColor   = $C_TEXT
    $lbl.Location    = New-Object Drawing.Point(4, $yOff)
    $lbl.Size        = New-Object Drawing.Size(448, 20)
    $pnlFiles.Controls.Add($lbl)
    $yOff += 22
}

# y2: first position below the file panel
$y2 = $y + 96 + 144 + 8   # = 358

$lblDllNote          = New-Object Windows.Forms.Label
$lblDllNote.Text     = "FFmpeg DLLs are placed next to the plugin so any DAW can find them."
$lblDllNote.Font     = $FNT_SMALL
$lblDllNote.ForeColor = $C_DIM
$lblDllNote.Location = New-Object Drawing.Point(20, $y2)
$lblDllNote.Size     = New-Object Drawing.Size(460, 18)
$form.Controls.Add($lblDllNote)

$lblStatus           = New-Object Windows.Forms.Label
$lblStatus.Text      = "Ready to install."
$lblStatus.ForeColor = $C_DIM
$lblStatus.Location  = New-Object Drawing.Point(20, ($y2 + 22))
$lblStatus.Size      = New-Object Drawing.Size(460, 20)
$form.Controls.Add($lblStatus)

$installBtnText = if ($isAdmin) { "Install" } else { "Install  (will request admin rights)" }

$btnInstall          = New-Object Windows.Forms.Button
$btnInstall.Text     = $installBtnText
$btnInstall.Font     = $FNT_BTN
$btnInstall.Location = New-Object Drawing.Point(20, ($y2 + 46))
$btnInstall.Size     = New-Object Drawing.Size(460, 40)
$btnInstall.FlatStyle = "Flat"
$btnInstall.BackColor = $C_ACCENT
$btnInstall.ForeColor = [Drawing.Color]::White
$btnInstall.FlatAppearance.BorderSize = 0

$btnInstall.Add_Click({
    $dest = $txtPath.Text.Trim()

    if ([string]::IsNullOrWhiteSpace($dest)) {
        $lblStatus.ForeColor = $C_RED
        $lblStatus.Text      = "Please enter or browse to an install location."
        return
    }

    $protected = ($dest -like "*$env:ProgramFiles*") -or
                 ($dest -like "*$env:CommonProgramFiles*") -or
                 ($dest -like "*$env:SystemRoot*")

    if ($protected -and -not $isAdmin) {
        $lblStatus.ForeColor = $C_DIM
        $lblStatus.Text      = "Requesting administrator access…"
        $form.Refresh()
        $ctrlDest = if ($txtCtrlPath) { $txtCtrlPath.Text.Trim() } else { $ControllerPath }
        Invoke-AsAdmin -ClapDest $dest -CtrlDest $ctrlDest
        $form.Close()
        return
    }

    $btnInstall.Enabled  = $false
    $lblStatus.ForeColor = $C_DIM
    $form.Refresh()

    try {
        if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest -Force | Out-Null }

        foreach ($src in $allFiles) {
            $name           = Split-Path -Leaf $src
            $lblStatus.Text = "Copying  $name …"
            $form.Refresh()
            Copy-Item -Path $src -Destination (Join-Path $dest $name) -Force
        }

        $syspath = [Environment]::GetEnvironmentVariable("PATH", "Machine")
        if ($syspath -notlike "*$dest*") {
            [Environment]::SetEnvironmentVariable("PATH", "$syspath;$dest", "Machine")
        }

        $lblStatus.ForeColor  = $C_GREEN
        $lblStatus.Text       = "Done!  Restart your DAW and scan for new plugins."
        $btnInstall.Text      = "Installed  ✓"
        $btnInstall.BackColor = $C_GREEN
        $btnInstall.Enabled   = $true

    } catch {
        $lblStatus.ForeColor = $C_RED
        $lblStatus.Text      = "Error: $($_.Exception.Message)"
        $btnInstall.Enabled  = $true
    }
})
$form.Controls.Add($btnInstall)

# ── Bitwig controller script section (shown only when DAWvid.control.js is present) ──

$txtCtrlPath     = $null   # referenced in the Install click handler above
$btnCtrlInstall  = $null

if ($hasCtrl) {
    # $yC: start of controller section, below Install button (ends at $y2+86) + 16px gap
    $yC = $y2 + 46 + 40 + 16   # = 460

    $sep             = New-Object Windows.Forms.Panel
    $sep.Location    = New-Object Drawing.Point(20, ($yC - 8))
    $sep.Size        = New-Object Drawing.Size(460, 1)
    $sep.BackColor   = $C_BORDER
    $form.Controls.Add($sep)

    $lblCtrlTitle          = New-Object Windows.Forms.Label
    $lblCtrlTitle.Text     = "BITWIG CONTROLLER SCRIPT"
    $lblCtrlTitle.Font     = $FNT_SMALL
    $lblCtrlTitle.ForeColor = $C_DIM
    $lblCtrlTitle.Location = New-Object Drawing.Point(20, $yC)
    $lblCtrlTitle.AutoSize = $true
    $form.Controls.Add($lblCtrlTitle)

    $txtCtrlPath          = New-Object Windows.Forms.TextBox
    $txtCtrlPath.Text     = $ControllerPath
    $txtCtrlPath.Location = New-Object Drawing.Point(20, ($yC + 20))
    $txtCtrlPath.Size     = New-Object Drawing.Size(370, 26)
    $txtCtrlPath.BackColor = $C_PANEL
    $txtCtrlPath.ForeColor = $C_TEXT
    $txtCtrlPath.BorderStyle = "FixedSingle"
    $form.Controls.Add($txtCtrlPath)

    $btnCtrlBrowse        = New-Object Windows.Forms.Button
    $btnCtrlBrowse.Text   = "Browse…"
    $btnCtrlBrowse.Location = New-Object Drawing.Point(398, ($yC + 19))
    $btnCtrlBrowse.Size   = New-Object Drawing.Size(82, 28)
    $btnCtrlBrowse.FlatStyle = "Flat"
    $btnCtrlBrowse.BackColor = $C_PANEL
    $btnCtrlBrowse.ForeColor = $C_TEXT
    $btnCtrlBrowse.FlatAppearance.BorderColor = $C_BORDER
    $btnCtrlBrowse.Add_Click({
        $dlg2              = New-Object Windows.Forms.FolderBrowserDialog
        $dlg2.Description  = "Choose the Bitwig Controller Scripts folder for DAWvid"
        $dlg2.SelectedPath = $txtCtrlPath.Text
        if ($dlg2.ShowDialog() -eq "OK") { $txtCtrlPath.Text = $dlg2.SelectedPath }
    })
    $form.Controls.Add($btnCtrlBrowse)

    $lblCtrlHint          = New-Object Windows.Forms.Label
    $lblCtrlHint.Text     = "Default: Documents\Bitwig Studio\Controller Scripts\DAWvid"
    $lblCtrlHint.Font     = $FNT_SMALL
    $lblCtrlHint.ForeColor = $C_DIM
    $lblCtrlHint.Location = New-Object Drawing.Point(20, ($yC + 50))
    $lblCtrlHint.Size     = New-Object Drawing.Size(460, 18)
    $form.Controls.Add($lblCtrlHint)

    $lblCtrlStatus        = New-Object Windows.Forms.Label
    $lblCtrlStatus.Text   = "Enables transport sync and bi-directional seek with Bitwig."
    $lblCtrlStatus.Font   = $FNT_SMALL
    $lblCtrlStatus.ForeColor = $C_DIM
    $lblCtrlStatus.Location = New-Object Drawing.Point(20, ($yC + 72))
    $lblCtrlStatus.Size   = New-Object Drawing.Size(460, 18)
    $form.Controls.Add($lblCtrlStatus)

    $btnCtrlInstall       = New-Object Windows.Forms.Button
    $btnCtrlInstall.Text  = "Install Bitwig Script"
    $btnCtrlInstall.Font  = $FNT_BTN
    $btnCtrlInstall.Location = New-Object Drawing.Point(20, ($yC + 94))
    $btnCtrlInstall.Size  = New-Object Drawing.Size(460, 36)
    $btnCtrlInstall.FlatStyle = "Flat"
    $btnCtrlInstall.BackColor = $C_PANEL
    $btnCtrlInstall.ForeColor = $C_TEXT
    $btnCtrlInstall.FlatAppearance.BorderColor = $C_BORDER
    $btnCtrlInstall.FlatAppearance.BorderSize  = 1

    $btnCtrlInstall.Add_Click({
        $ctrlDest = $txtCtrlPath.Text.Trim()
        if ([string]::IsNullOrWhiteSpace($ctrlDest)) {
            $lblCtrlStatus.ForeColor = $C_RED
            $lblCtrlStatus.Text      = "Please enter or browse to a folder."
            return
        }
        $btnCtrlInstall.Enabled  = $false
        $lblCtrlStatus.ForeColor = $C_DIM
        $lblCtrlStatus.Text      = "Installing…"
        $form.Refresh()
        try {
            if (-not (Test-Path $ctrlDest)) { New-Item -ItemType Directory -Path $ctrlDest -Force | Out-Null }
            Copy-Item -Path $controllerJs -Destination (Join-Path $ctrlDest "DAWvid.control.js") -Force
            $lblCtrlStatus.ForeColor  = $C_GREEN
            $lblCtrlStatus.Text       = "Script installed — add it in Bitwig › Settings › Controllers."
            $btnCtrlInstall.Text      = "Installed  ✓"
            $btnCtrlInstall.BackColor = $C_GREEN
            $btnCtrlInstall.ForeColor = [Drawing.Color]::White
            $btnCtrlInstall.FlatAppearance.BorderSize = 0
            $btnCtrlInstall.Enabled   = $true
        } catch {
            $lblCtrlStatus.ForeColor = $C_RED
            $lblCtrlStatus.Text      = "Error: $($_.Exception.Message)"
            $btnCtrlInstall.Enabled  = $true
        }
    })
    $form.Controls.Add($btnCtrlInstall)
}

# ── Auto-proceed when re-launched elevated ────────────────────────────────────
# The non-elevated instance closed after calling Invoke-AsAdmin; this new elevated
# window auto-clicks Install so the user doesn't have to click again.

if ($AutoInstall) {
    $btnInstall.Text    = "Continue Installation"
    $script:_autoTimer  = New-Object Windows.Forms.Timer
    $script:_autoTimer.Interval = 200
    $script:_autoTimer.Add_Tick({
        $script:_autoTimer.Stop()
        if ($btnCtrlInstall) { $btnCtrlInstall.PerformClick() }
        $btnInstall.PerformClick()
    })
    $form.Add_Shown({ $script:_autoTimer.Start() })
}

# ── Run ───────────────────────────────────────────────────────────────────────
[void]$form.ShowDialog()

} catch {
    $detail = "DAWvid installer crashed before the window could open.`n`n" +
              "Error: $($_.Exception.Message)`n" +
              "Line:  $($_.InvocationInfo.ScriptLineNumber)`n`n" +
              "Full log: $ErrorLog"
    $_ | Out-File -FilePath $ErrorLog -Encoding utf8 -Force
    try {
        [System.Windows.Forms.MessageBox]::Show(
            $detail, "DAWvid Installer - Fatal Error",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    } catch { }
}
