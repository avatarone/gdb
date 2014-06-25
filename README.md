== Building GDB for Avatar ==
Install packages 
- libexpat1-dev (for XML support)
- python2.7-dev (for Python support)

Configure gdb with: *./configure --with-python --with-expat=yes --target=arm-none-eabi*


== Notes ==
if you see errors like the following when running Avatar, the python support of GDB was not compiled well.
```
    avatar.bintools.gdb.parse_stream.ParseError: Unhandled Output: Python Exception <type 'exceptions.ImportError'> No module named gdb:
```
