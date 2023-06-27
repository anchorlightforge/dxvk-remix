git submodule update --init
. .\build_common.ps1
meson setup --buildtype debugoptimized --backend ninja --wipe -Denable_d3d9=true .\_Comp64DebugOptimized\
cd .\_Comp64DebugOptimized\
meson compile
cd ..\..