#!/bin/sh

./autoclean.sh

rm -f configure

rm -f Makefile.in

rm -f config.guess
rm -f config.sub
rm -f install-sh
rm -f missing
rm -f depcomp

if [ 0 = 1 ]; then
autoscan
else

touch NEWS
touch README
touch AUTHORS
touch ChangeLog
touch config.h.in
touch install-sh

libtoolize --force --copy --install --automake
aclocal
automake -a -c
autoconf
# run twice to get rid of 'ltmain.sh not found'
autoreconf -f -i -Wall,no-obsolete
autoreconf -f -i -Wall,no-obsolete

#./configure

#./configure --enable-debug
if [ 1 = 1 ]; then
  #./configure --disable-shared --enable-static --enable-coverage
  ./configure --disable-shared --enable-static

else
  if [ ! -d cpp-ci-unit-test ]; then
    git clone -q --depth=1 https://github.com/yhfudev/cpp-ci-unit-test.git
  fi
  if [ ! -d libuv ]; then
    git clone -q --depth=1 https://github.com/libuv/libuv.git
  fi
  cd cpp-ci-unit-test && git pull && cd ..
  cd libuv && git pull && ./autogen.sh && ./configure && make clean && make -j8 && cd ..

  CC=clang CXX=clang++ ./configure --disable-shared --enable-static --enable-coverage --with-libuv-include=`pwd`/libuv/include --with-libuv-lib=`pwd`/libuv/ --with-ciut=`pwd`/cpp-ci-unit-test
  make clean; make coverage CC=clang CXX=clang++
fi

#make clean
#make ChangeLog

#make

#make check
#make -C doc/latex/
#make dist-gzip

fi
