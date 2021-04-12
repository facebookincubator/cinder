[![Build Status](https://travis-ci.org/pfalcon/python-compiler.png?branch=master)](https://travis-ci.org/pfalcon/python-compiler)

Python Bytecode Compiler Written in Python
==========================================

This is WIP port of Python2 stdlib
[compiler](https://docs.python.org/2/library/compiler.html) package
to Python3.

Motivation: to have an easily hackable Python compiler for experimenting
(e.g. various optimizations, instrumentation, semantic variants, etc.)

The porting project concentrates on the conversion of AST (as provided by
the builtin "ast" module) to bytecode and code objects. The original Python2
package included another important part: conversion of concrete parse tree
into Abstract Syntax Tree (AST). While it would be interesting to ultimately
have complete pure-Python closed loop for Python compilation, to keep this
specific project maintainable, lexing, parsing, AST building are outside
of its scope. Other projects are welcome to provide integrated maintainable
solutions for those areas (indeed, generic/non-integrated solutions for them
definitely exist).

Short-term goals:

* Port the original "compiler" package to work with AST as produced by
  Python3's "ast" module.
* Initially, implement support for Python3.5 syntax and bytecode.
* Cleanup the original code.

History of Python2 "compiler" package:

1. The code is based on earlier work done by Greg Stein and Bill Tutt
for Python2C (aka Py2C aka p2c) project circa 1997-1999. That code however
didn't include bytecode compiler, but just transformer.py module, which
converted low-level Python parse tree, as produced by the built "parser"
module, into a higher-level Abstract Syntax Tree (AST). The Python2C
project itself generated C code from this AST.
2. Actual bytecode compiler was started and largely written by Jeremy
Hylton. Initial commits importing Python2C files and starting pycodegen.py
were made on 2000-02-04.
3. 66 commits were made in 2000, 73 in 2001, 10 in 2002, 6 in 2003,
15 in 2004, 9 in 2005, 51 in 2006, 16 in 2007.
4. In May 2007, complaints are heard that it's hard to maintain and regularly
broken: https://mail.python.org/pipermail/python-3000/2007-May/007575.html
5. Those transformed into an entry in [PEP3108](https://www.python.org/dev/peps/pep-3108/)
for its removal.
6. Removed in of 3.x branch in revision a8add0ec5ef05c26e1641b8310b65ddd75c0fec3
on 2007-05-14.
7. The funtionality wasn't totally gone, instead functionality of internal
C-based compiler was exposed in a similar fashion (albeit with changed/cleaned
up API). E.g., compiler.ast and compiler.transformer was replaced with
builtin "_ast" module (in other words, AST node type definitions and
transformation of parse tree to AST are now done on C level). compiler.visitor
was replaced with Python-level "ast" module. Compilation of AST into bytecode
is handled using builtin compile() function with suitable parameters.

Usage
-----

Currently, the package is intended to work with CPython3.5 only.

```
python3.5 -m compiler --help
python3.5 -m compiler <input.py>
```

By default, the command above compiles source to in-memory code objects
and executes it. If `-c` switch is passed, instead of execution, it will
be saved to `.pyc` file. If `--dis` is passed, code will be disassembed
before executing/saving.

Running Tests
-------------

The projects includes a builtin test corpus of various syntactic constructs
to verify codegeneration against reference output produced by CPython3.5.
Currently, the project does not include peephole optimizer as included as
a postprocessing pass in CPython. This means that testing should happen
against modified CPython3.5 build with the peephole optimizer disabled.

The patch is available at https://github.com/pfalcon/cpython/tree/3.5-noopt
This repository includes `build-cpython-compiler.sh` helper script to
download and build it. It will produce a `python3.5-nopeephole` symlink
in the top-level directory, where scripts below expect to find it.

To produce reference code generation output from python3.5-nopeephole, run:

~~~
./test_testcorpus_prepare.py
~~~

This needs to be done once. Afterwards, you can run

~~~
./test_testcorpus_run.py
~~~

to compare the output produced by this compiler package against the
reference.

Authorship and Licensing Info
-----------------------------

The source code is based on the "compiler" package from Python2 standard
library. It is licensed under Python Software Foundation License v2.
See complete licensing terms and details in file LICENSE.

The "compiler" package is a result of dedicated work of a number of
individuals, listed below (based on the git history of the official
CPython repository).

Porting of the code to Python3 and further maintenance is handled by
Paul Sokolovsky.

Contributors to Python2 version of the package:

```
$ git clone https://github.com/python/cpython
$ cd cpython
$ git checkout 2.7
$ git log --follow Lib/compiler | grep ^Author | sed -e 's/<.*>//' | sort | uniq -c | sort -n -r
# Email addresses not included to minimize spam
    143 Author: Jeremy Hylton
     18 Author: Neal Norwitz
     18 Author: Guido van Rossum
     17 Author: Georg Brandl
     11 Author: Tim Peters
      9 Author: Thomas Wouters
      7 Author: Neil Schemenauer
      6 Author: Michael W. Hudson
      6 Author: Martin v. Löwis
      4 Author: Brett Cannon
      3 Author: Nick Coghlan
      3 Author: Antoine Pitrou
      2 Author: Raymond Hettinger
      2 Author: Ezio Melotti
      2 Author: Christian Heimes
      2 Author: Benjamin Peterson
      2 Author: Anthony Baxter
      2 Author: Andrew M. Kuchling
      2 Author: Alexandre Vassalotti
      1 Author: Serhiy Storchaka
      1 Author: Phillip J. Eby
      1 Author: Jeffrey Yasskin
      1 Author: Gustavo Niemeyer
      1 Author: Greg Stein
      1 Author: Facundo Batista
      1 Author: Eric Smith
      1 Author: Collin Winter
      1 Author: Barry Warsaw
      1 Author: Amaury Forgeot d'Arc
```

Related Projects
----------------

Aka "why I took Python2's compiler package and spent all this effort on it
instead of using something else".

* [PyPy](https://bitbucket.org/pypy/pypy) is the obvious direction of thought.
  PyPy is a very advanced and big project. Somewhere in there a small hadron
  collider may be lurking. But what if you want a simple Python compiler
  without a hadron collider? You may be out of luck to extract it, or at
  least I was.
* [YaPyPy](https://github.com/Xython/YAPyPy) - looks absolutely great, but
  its docs miss to explain how to actually run it. I mean, that small part
  of docs (README) which is written in English. There're more docs in Chinese,
  but unfortunately, I can't read it.
* [tailbiter](https://github.com/darius/tailbiter) is a great project
  showing how to develop a Python bytecode compiler from scratch, and is
  highly recommended for that purpose. But I wanted to get a "production
  ready" (read: behaving the same way as CPython's) compiler in reasonable
  time with reasonable effort, not write one from scratch.
