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

## Configuration
By default, the configuration file is config.yml in the same folder as the executable on Windows, and /usr/local/etc/casproxyserver.yml on Linux. You can also specify a custom path as a command-line argument:
```bash
./casproxyserver ./casproxyserver.yml
```

### Example configuration
```yaml
listenIp: 0.0.0.0
port: 24000

allowedIps:
  - 127.0.0.0/8
  - 10.0.0.0/8
  - 172.16.0.0/12
  - 192.168.0.0/16
```
