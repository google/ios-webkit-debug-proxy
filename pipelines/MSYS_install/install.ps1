# https://www.powershellgallery.com/packages/AppVeyorBYOC/1.0.107-beta/Content/scripts%5CWindows%5Cinstall_msys64.ps1

Write-Host "Installing MSYS2..." -ForegroundColor Cyan

Set-Content -Value "Write-Host 'Sleep then kill gpg-agent.exe'; sleep 300; Stop-Process -name gpg-agent -Force" -Path .\kill-gpg-agent.ps1
Start-Process powershell.exe -ArgumentList .\kill-gpg-agent.ps1

if(Test-path C:\msys64) {
    Write-Host "Removing old installation, this can take a while..." -ForegroundColor Cyan
    del C:\msys64 -Recurse -Force
}

# download installer
$zipPath = "$($env:TEMP)\msys2-x86_64-latest.tar.xz"
$tarPath = "$($env:TEMP)\msys2-x86_64-latest.tar"
Write-Host "Downloading MSYS installation package..."
Invoke-WebRequest -Uri 'http://repo.msys2.org/distrib/msys2-x86_64-latest.tar.xz' -OutFile $zipPath

Write-Host "Untaring installation package..."
7z x $zipPath -y -o"$env:TEMP" | Out-Null

Write-Host "Unzipping installation package..."
7z x $tarPath -y -oC:\ | Out-Null
del $zipPath
del $tarPath

function bash($command) {
    Write-Host $command
    cmd /c start /wait C:\msys64\usr\bin\sh.exe --login -c $command
    Write-Host " - OK" -ForegroundColor Green
}

[Environment]::SetEnvironmentVariable("MSYS2_PATH_TYPE", "inherit", "Machine")

# update core packages
bash 'pacman -Syuu --noconfirm'
bash 'pacman -Syu --noconfirm'

# cleanup pacman cache
Remove-Item c:\msys64\var\cache\pacman\pkg -Recurse -Force -Verbose

# add mapping for .sh files
cmd /c ftype sh_auto_file="C:\msys64\usr\bin\bash.exe" "`"%L`"" %* | out-null
cmd /c assoc .sh=sh_auto_file

Remove-Item .\kill-gpg-agent.ps1 -Force -ErrorAction Ignore

Write-Host "MSYS2 installed" -ForegroundColor Green