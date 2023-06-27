git submodule update --init
. .\build_common.ps1
meson setup --buildtype release --backend ninja --wipe -Denable_d3d9=true .\_Comp64Release\
cd .\_Comp64Release\
meson compile
ii .
cd ..\..
Read-Host -Prompt "Press Enter to exit."