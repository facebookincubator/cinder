# parallel-hashmap

How to run these benchmarks
===========================

These bencharks were run on windows using Visual Studio 2017, in a cygwin window with the VC++ 2017 compiler env vars (add something like this in your Cygwin.bat:

CALL "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"

Running them on linux would require Makefile changes.

To build and run the tests, just update the path to the abseil libraries in the makefile, and run make. 

Your charts are now in charts.html.


