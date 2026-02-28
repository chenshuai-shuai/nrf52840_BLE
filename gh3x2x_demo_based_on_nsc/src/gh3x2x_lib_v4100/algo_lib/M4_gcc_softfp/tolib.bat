@echo off 

 for /f "delims=" %%f in ('dir/b/s/a-d *.a') do (if not "%%~nxf"=="%0" ren "%%f" "lib%%~nxf")