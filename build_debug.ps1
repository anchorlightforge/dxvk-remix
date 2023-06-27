git submodule update --init
. .\build_common.ps1
meson setup --buildtype debug --backend ninja --wipe -Denable_d3d9=true .\_Comp64Debug\
cd .\_Comp64Debug\
meson compile
cd ..\..