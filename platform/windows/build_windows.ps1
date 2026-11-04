# Builds e7vm on Windows with Visual Studio 2022 and runs the tests.
#   -ClangCl : use clang-cl (enables computed-goto dispatch, ~20-30% faster interpreter)
#   -Shared  : build e7vm.dll (needed for the C# binding)
param([switch]$ClangCl, [switch]$Shared)
$root = Resolve-Path "$PSScriptRoot\..\.."
$cfg = @("-S", $root, "-B", "$root\build-windows", "-G", "Visual Studio 17 2022", "-A", "x64")
if ($ClangCl) { $cfg += @("-T", "ClangCL") }
if ($Shared) { $cfg += "-DE7_BUILD_SHARED=ON" }
cmake @cfg
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build "$root\build-windows" --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir "$root\build-windows" -C Release --output-on-failure
