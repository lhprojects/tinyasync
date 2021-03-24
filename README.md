
# `tinyasyc`

## What is `tinyasyc`?

`tinyasyc` is **tiny** **async** IO library, making use of **c++20 stackless coroutines**. It is for senior users of library of c++20 couroutine and for beginner of author of libarary. 

## Prons:

* Tiny. It has only 5000+ lines currently. This is a header only libarary.
* Focusing. It implemented only core featrures of an async IO library.
* Simple. Flat namespace/class structures, plain implementation, avoiding too fancy template technique.
* Portable. It supports windows and linux (epoll). 
* Educational! Educational! Educational!
 
## Cons:
* It isn't now functional enough. Altough it has several working examples. It lack lots features would be necessary for industry production.
* It currently doens't support multi-threading. Although, I was trying to write the library in a way that it would be easy to support multi-threading.
* I'm not good at English. My English writing is really slow and bad. You may can't learn much from comments in the source.

In summary, it's great place to start your learning in writing your the c++20 coroutine library and the underlying mechanism of a stackless coroutine based asyc IO libarary.

## Build

This is a header library. You needn't to build the library itself. Just adding the `<root_of_project>/include` to your inlcue path. Use `tinyasync` as following:
```c++
#include <tinyasync/tinyasync.h>
```

## Introduction

### IoContext
```c++
// multiple thread io contex
IoContext ctx(std::true_type{});
// multiple thread io contex by default
IoContext ctx;
// single thread io context
IoContext ctx(std::false_type{});
```

Note:
* You don't have to link with e.g. pthread if you are using single thread version.
* Performance is tittle different. Recommend you always to use multiple thread version.
Single thread version can not satisfy your need for real world application.

## Some note on compilers

* MSVC: The code has not been compile on MSVC for a while, so I don't know much about the quality of generated code.
 Exactly speaking, it is about the Windows API not the compiler.
* gcc: The front end of g++ supports coroutine well, but the generated code is not very optimial (compared to clang and expectation).
It seems that the g++-10 can't inline the coroutine and the dynamic memory allocation can't be eliminated.
* clang: The generated code is good. It can inline the coroutine and dynamic memory allocation can be eliminated.
But they are not always guaranteed. The dynamic memory allocation elision will not happen for deep (e.g. three) nested coroutine.
For the geneator use case, the generated code is about -20%-50% slower than naive code (see bench_task), in which it does little work for each iteration.
It seems the compiler generates good codes for both coroutine and naive code, but just not the same.
The difference should be less significant, if you have some real work to do in each iteration.

## Examples

See [README.md](./examples/README.md) .

## Play on `godbolt.org`

You need to include tinyasync as following:
```c++
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/basics.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/task.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/io_context.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/buffer.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/awaiters.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/mutex.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/dns_resolver.h>
#include <https://raw.githubusercontent.com/lhprojects/tinyasync/master/include/tinyasync/memory_pool.h>
<your code>
```

* flags with clang
```
-O2 -std=c++2a -fcoroutines-ts -stdlib=libc++ -DNDEBUG
```
* flags with gcc
```
-O2 -std=c++2a -fcoroutines -DNDEBUG
```
* flags with MSVC
```
/std:c++latest /O2
```
