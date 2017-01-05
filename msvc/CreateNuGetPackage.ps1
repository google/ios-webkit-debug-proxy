Param(
  [string]$build
)

Write-Host Changing build number to $build

# Update the build number
(gc .\ios-webkit-debug-proxy.autoconfig).replace('{build}', $build)|sc .\ios-webkit-debug-proxy.out.autoconfig

# Create the NuGet package
Import-Module "C:\Program Files (x86)\Outercurve Foundation\modules\CoApp"
Write-NuGetPackage .\ios-webkit-debug-proxy.out.autoconfig