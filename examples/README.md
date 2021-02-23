# Examples

## Build
* On Linux

Execute command:
```bash
mkdir build
cd build
cmake ../examples -DCMAKE_BUILD_TYPE=Release
cmake --build . -DCMAKE_BUILD_TYPE=Release
```
* On Windows

Ensure cmake.exe is in your PATH, then execute
```bash
mkdir build
cd build
cmake.exe ../examples
cmake.exe --build . --config Release
```

## Note for Windows

We use linux as an example to show how to run the examples.
If you are using Windows, you may need WSL to use linux utility like `nc` to test your executable (Windows program).
So you need to install WSL. And if you want your linux utility to communicate with your Windows program, you should
use ip directly instead of `localhost`. For example, you have a server running on Windows using port `8899`,
 you want to connect it via `nc`, you should execute following command in WSL:
```c++
> cat /etc/resolv.conf
> nc <ip_from_above> <port>
```


## bench_task
`Task<>` benchmark.

```bash
> ./bench_task/bench_task
task: 2.582392 ns/iteration
iter: 1.696028 ns/iteration
naive: 0.242517 ns/iteration
```

* task: Use Task<> to simulate generator
* iter: tranditional iterator, but inline not allowed for `operator++` .
* naive: raw loop (or inlined tranditional iterator).

The performance of `iter` should be the up bound for the `Task<>`.
See source for more details.

## chatroom_server
In the first terminal
```bash
> ./chatroom_server/chatroom_server
```

In the second terminal
```bash
> nc localhost 8899       #1 <- time order
**** server received **** #1
<1>: second               #4
```

In the third terminal
```bash
> nc localhost 8899       #2
**** server received **** #2
second                    #3
**** server received **** #3
```

You can open more terminals.

## echo_server
In the first terminal
```bash
> ./echo_server/echo_server
echo server listening localhost:8899
```

In the second terminal
```bash
> nc localhost 8899
I love you,
I love you,
```
You can open more terminals.


## http_helloworld_server

In the first terminal
```bash
> ./http_helloworld_server/http_helloworld_server 
```

In the second terminal
```bash
> curl localhost:8899
<html>
<head><title>Hello, World</title></head>
<body>Hello, World</body>
</html>
```

You open browser to open `127.0.0.1:8899` .

## pingpong
pingpong benchmark. In the first terminal
```bash
> ./pingpong/pingpong_server
```

In the second terminal
```bash
> ./pingpong/pingpong_client
10 connection
1024 block size
362.19 M/s bytes read
362.19 M/s bytes write
```

## sleepsort

```bash
> ./sleepsort/sleepsort 3 1 2
sorting 3 1 2
1 2 3
```
