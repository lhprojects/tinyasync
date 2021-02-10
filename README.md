
# `tinyasyc`

## What is `tinyasyc`?

`tinyasyc` is **tiny** **async** IO library, making use of **c++20 stackless coroutines**. It is for senior users of library of c++20 couroutine and begginer as author of libarary.

## Prons:

* Tiny. It has only 2000+ lines currently. This is a header only libarary.

* Focusing. It implemented only core featrures of an async IO library.

* Simple. Plat namespace/class structures, plain implementation, avoiding too fancy template technique.

* Portable. It supports windows and linux (epoll). 

* Educational! Educational! Educational!
 
In summary, it's great place to start your learning in writing your the c++20 coroutine library and the underlying mechanism of a stackless coroutine based asyc IO libarary.

## Cons:
* It isn't now functional enough. Altough it has several working examples. It lack lots features would be necessary for industry production.
* It currently doens't support multi-threading. Although, I was trying to write the library in a way that it would be easy to support multi-threading.
* I'm not good at English. My English writing is really slow and bad. You may can't learn much from comments in the source.

## Build

This is a header library. You needn't to build the library itself. Just adding the `<root_of_project>/include` to your inlcue path.

## Examples

### Build
* On Linux
Executing command:
```bash
mkdir build_linux
cd build_linux
cmake ..
cmake --build .
```
* On Windows

Ensure cmake.exe is your PATH.
```bash
mkdir build_win
cd build_win
cmake.exe ..
cmake.exe --build . --config Release
```

### Run
#### sleepsort
```bash
./sleepsort/sleepsort 3 1 2             # linux
./sleepsort/Release/sleepsort.exe 3 1 2 # Windows
```

#### echo_server
In the first bash/cmd
```bash
./echo_server/echo_server              # linux
./echo_server/Release/echo_server.exe  # Windows
```

* If you are using linux, open second terminal, type
```bash
nc localhost 8899
```
Then you just randomly type something in the terminal.
* If you are using Windows, you need to install WSL and turn off firewall. In WSL:
```bash
cat /etc/resolv.conf
nc <ip_from_above> 8899
```

#### http_client
```bash
./http_client/http_client             # linux
./http_client/Release/http_client     # Windows
```

#### http_helloworld_server
```bash
./http_helloworld_server/http_helloworld_server              # linux
./http_helloworld_server/Release/http_helloworld_server.exe  # Windows
```

* If you are using linux, open second terminal, type
```bash
curl localhost:8899
```
* If you are using Windows, you need to install WSL and turn off firewall. In WSL:
```bash
cat /etc/resolv.conf
curl <ip_from_above>:8899
```
