About
=====

clang-misracpp2008 is a project which tries to create an open source checker for
the MISRA C++ 2008 rules using the LLVM/Clang infrastructure.

Some rules can easily be checked by enabling compiler-provided warnings. For
those rules we simply name those switches. Other rules require custom logic and
are therefore realized as plugin.

Installation
============

It is assumed that you have already have compiled LLVM/Clang from SVN. To add
clang-misracpp2008, the following steps are required:

    cd ~/${LLVM_CHECKOUT_FOLDER}/tools/clang/tools 
    git clone https://github.com/rettichschnidi/clang-misracpp2008.git
    echo 'add_subdirectory(clang-misracpp2008)' >>  CMakeLists.txt
    
The next time you build LLVM/Clang, clang-misracpp2008 gets picked up and built.

Please note that currently only the CMake build system is supported.

Running tests
=============
Go to the LLVM/Clang build directory and run `make check-misracpp2008`
