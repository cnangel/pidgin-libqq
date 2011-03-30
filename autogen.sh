mkdir -pv m4
aclocal
libtoolize
aclocal -I m4
automake --add-missing --copy
autoconf


