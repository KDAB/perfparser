win32-g++ {
    MAKE = mingw32-make
} else {
    MAKE = make
}

win32: GENDIS = i386_gendis.exe
else: GENDIS = i386_gendis
