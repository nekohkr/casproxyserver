# casproxyserver
**casproxyserver** is a cross-platform smartcard proxy server.

## Features
- Supports WinSCard API calls:
  - `SCardEstablishContext`
  - `SCardReleaseContext`
  - `SCardListReaders`
  - `SCardConnect`
  - `SCardDisconnect`
  - `SCardBeginTransaction`
  - `SCardEndTransaction`
  - `SCardTransmit`
  - `SCardGetAttrib`

## Build

```bash
git clone https://github.com/nekohkr/casproxyserver.git
cd casproxyserver
git submodule update --init --recursive
cd thirdparty/yaml-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build --config Release
cd ../..
make
sudo make install
sudo ./scripts/install_systemd.sh
```