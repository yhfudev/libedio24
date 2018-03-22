libedio24
=========

A network library to control E-DIO24 devices.


[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/yhfudev/libedio24/blob/master/COPYING)
[![Travis Build status](https://travis-ci.org/yhfudev/libedio24.svg?branch=master)](https://travis-ci.org/yhfudev/libedio24)
[![Appveyor Build status](https://ci.appveyor.com/api/projects/status/afefa9w0h06uv2ee/branch/master?svg=true)](https://ci.appveyor.com/project/yhfudev/libedio24/branch/master)
[![codecov](https://codecov.io/gh/yhfudev/libedio24/branch/master/graph/badge.svg)](https://codecov.io/gh/yhfudev/libedio24)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/15281/badge.svg)](https://scan.coverity.com/projects/yhfudev-libedio24)


This library provides the creating of various control packets for E-DIO24 devices.
It also provides a E-DIO24 simulator for development debug, and a client which can negotiate with the device via
command line or command lists(see directory utilities and the configure file testcmds.txt).



Compilation and Installation
----------------------------

You need to compile the library from source code by

    ./autogen.sh
    ./configure --disable-shared --enable-static
    make
    sudo make install


You can also generate the library API document by

    make -C doc


If you want to run the unit tests, please install the "Tiny unit test framework":

    git clone https://github.com/yhfudev/cpp-ci-unit-test.git

and run

    ./configure --disable-shared --enable-static --with-ciut=`pwd`/cpp-ci-unit-test
    make check

Usage
-----


### libedio24

The library can be linked staticly, and there's API examples in the directory 'utils'.


### edio24sim

The edio24sim is a EDIO-24 device simulator which can be used to test your custom commands with edio24cli.
To run it, just type

    edio24sim


### edio24cli

The edio24cli is a client for the device, you can use the tool to test the device by command line.
You need to prepare the execute list for the client. The file utils/testcmds.txt is a example which
contains all of support commands.

You man use the option '-h' to show all of options supported.

    edio24cli -h

If you prepared the execute list, you can pass the content of file to the client by either pipe or argument.

    # by pipe
    edio24cli < testcmds.txt
    # or
    cat testcmds.txt | edio24cli
    
    # by argument
    edio24cli -e testcmds.txt

You can also specify the device IPv4 address

    edio24cli -e testcmds.txt -r 192.168.0.100

To discover the device in the LAN, you can specify the '-d' option:

    edio24cli -d

