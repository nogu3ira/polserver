@echo off

@rmdir /S /Q .vs
@rmdir /S /Q CMakeFiles
@rmdir /S /Q docs
@rmdir /S /Q format.dir
@rmdir /S /Q stackwalk.dir
@rmdir /S /Q tinyxml.dir
@rmdir /S /Q z.dir
@rmdir /S /Q pol-core
@rmdir /S /Q x64
@rmdir /S /Q x64-Release
@rmdir /S /Q boost-prefix
@rmdir /S /Q curl
@rmdir /S /Q kaitai.dir
@rmdir /S /Q sqlite
@rmdir /S /Q module_defs
@del /S /F *.vcxproj*
@del /S /F *.cmake
@del /S /F *.h
@del /S /F *.txt
@del /S /F *.sln
@del /S /F *.tmp