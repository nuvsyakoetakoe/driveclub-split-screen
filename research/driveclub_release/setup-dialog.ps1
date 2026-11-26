$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$window = New-Object Windows.Forms.Form
$window.Text = 'DRIVECLUB Split Screen 0.1.0'
$window.StartPosition = 'CenterScreen'
$window.AutoScaleMode = 'Font'
$window.Font = New-Object Drawing.Font('Segoe UI', 10)
$window.ClientSize = New-Object Drawing.Size(490, 211)
$window.FormBorderStyle = 'FixedDialog'
$window.MaximizeBox = $false

$info = New-Object Windows.Forms.Label
$language = New-Object Windows.Forms.ComboBox
$language.DropDownStyle = 'DropDownList'
$language.Location = New-Object Drawing.Point(18, 16)
$language.AccessibleName = 'Language / Язык'
[void]$language.Items.AddRange(@('English', 'Русский'))
$window.Controls.Add($language)
$info.Location = New-Object Drawing.Point(18, 64)
$info.Size = New-Object Drawing.Size(455, 75)
$window.Controls.Add($info)

$install = New-Object Windows.Forms.Button
$install.Location = New-Object Drawing.Point(18, 151)
$install.Size = New-Object Drawing.Size(140, 37)
$remove = New-Object Windows.Forms.Button
$remove.Location = New-Object Drawing.Point(170, 151)
$remove.Size = $install.Size
$window.Controls.AddRange(@($install, $remove))
$window.AcceptButton = $install
$language.Add_SelectedIndexChanged({
    if ($language.SelectedIndex -eq 1) {
        $info.Text = 'Закройте игру и лаунчер. Выберите действие, затем укажите DriveClub.exe из папки порта 0.4.2. При установке будет сохранена копия исходного лаунчера.'
        $install.Text = 'Установить'
        $remove.Text = 'Удалить'
    } else {
        $info.Text = 'Close the game and launcher. Choose an action, then select DriveClub.exe from your 0.4.2 installation. Setup will back up the original launcher.'
        $install.Text = 'Install'
        $remove.Text = 'Uninstall'
    }
})
$language.SelectedIndex = 0

function Select-Game([string]$action) {
    $picker = New-Object Windows.Forms.OpenFileDialog
    try {
        $ru = $language.SelectedIndex -eq 1
        $picker.Title = if ($ru) { 'Выберите DriveClub.exe' } else { 'Select DriveClub.exe' }
        $picker.Filter = 'DriveClub.exe|DriveClub.exe'
        if ($picker.ShowDialog($window) -eq 'OK') {
            $window.Tag = @{ operation = $action; file = $picker.FileName; language = $(if ($ru) {'ru'} else {'en'}) }
            $window.Close()
        }
    } finally {
        $picker.Dispose()
    }
}
$install.Add_Click({ Select-Game 'install' })
$remove.Add_Click({ Select-Game 'remove' })
[void]$window.ShowDialog()
if ($window.Tag) { [Console]::Write(($window.Tag | ConvertTo-Json -Compress)) }
$window.Dispose()
