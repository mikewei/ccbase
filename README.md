# CCBASE

## Introduction

CCBASE is a C++11 base library aimed for high performance server development. It contains a collection of useful foundation classes which are good complement to STL.

Main features:

* designed for high-performance, multi-threading, production environment
* only common building blocks for high-volume server development
* make full use of lock-free techniques
* use what C++11 have and concentrate on what C++11 lack
* clean and robust code with sufficient tests

Currently CCBASE mainly contains these compoments:

* lock-free M to N fifo queue
* O(1) timer manager using timer wheel
* lock-free worker thread pool
* a faster closure implementation (than std::function)
* thread safe memory reclamation
* a faster lock-free concurrent smart pointer (than atomic shared\_ptr)
* token bucket implementation

## HowToUse

It provides a [bazel](https://bazel.build) BUILD file so just set deps on it if you are using bazel, or you need to import .h and .cc files to your build system.

If you want to build unit-tests, install bazel and [gtestx](https://github.com/mikewei/gtestx) first.

