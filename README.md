== Building GDB for Avatar ==
Install packages 
- libexpat1-dev (for XML support)
- python2.7-dev (for Python support)

Configure gdb with: *./configure --with-python --with-expat=yes --target=arm-none-eabi*
