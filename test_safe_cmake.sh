export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
mkdir -p /tmp/zenonia2-build-safe
cd /tmp/zenonia2-build-safe
cp /Volumes/Seagate/PSVITA\ Develop/Zenonia2-vita/CMakeLists.txt ./CMakeLists.txt.bak
sed -e '/FILE sce_sys/d' -e '/FILE splash.rgba/d' -e '/FILE title.rgba/d' -e '/FILE touch.rgba/d' /Volumes/Seagate/PSVITA\ Develop/Zenonia2-vita/CMakeLists.txt > CMakeLists.txt
cmake /Volumes/Seagate/PSVITA\ Develop/Zenonia2-vita
make zenonia_2.vpk -j4
ls -l zenonia_2.vpk
