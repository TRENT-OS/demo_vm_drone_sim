cmake --build build -j8
export GZ_PARTITION=relay
./build/cpp_proxy $@

