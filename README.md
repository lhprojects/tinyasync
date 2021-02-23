
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
