#!/bin/sh
# $Id: buildconf.sh,v 1.3 2005-10-08 09:48:32 adam Exp $

if automake --version|head -1 |grep '1\.[4-8]'; then
    echo "automake 1.4-1.8 is active. You should use automake 1.9 or later"
    if test -f /etc/debian_version; then
	echo " sudo apt-get install automake1.9"
	echo " sudo update-alternatives --config automake"
    fi
    exit 1
fi

set -x
aclocal -I m4
autoheader
libtoolize --automake --force 
automake --add-missing 
autoconf
set -
if [ -f config.cache ]; then
	rm config.cache
fi

enable_configure=false
enable_help=true
sh_flags=""
conf_flags=""
case $1 in
    -d)
	sh_flags="-g -Wall"
	enable_configure=true
	enable_help=false
	shift
	;;
    -c)
	sh_flags=""
	enable_configure=true
	enable_help=false
	shift
	;;
esac

if $enable_configure; then
    if test -n "$sh_flags"; then
	CXXFLAGS="$sh_flags" ./configure $*
    else
	./configure $*
    fi
fi
if $enable_help; then
    cat <<EOF

Build the Makefiles with the configure command.
  ./configure [--someoption=somevalue ...]

For help on options or configuring run
  ./configure --help

Build and install binaries with the usual
  make
  make check
  make install

Build distribution tarball with
  make dist

Verify distribution tarball with
  make distcheck

Or just build the Debian packages without configuring
  dpkg-buildpackage -rfakeroot

When building from a CVS checkout, you need these Debian tools:
  docbook-utils, docbook, docbook-xml, docbook-dsssl, jade, jadetex,
  libxslt1-dev, libyaz++-dev
EOF
fi
