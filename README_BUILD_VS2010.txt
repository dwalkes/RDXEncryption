Use the instructions at
http://diskcryptor.net/wiki/Compilation/en to install the WDK, FASM and YASM.

Note: add all environment variables for the computer, not the current user.

Add environment variable fasm as directed.
Add yasm extracted path into the %PATH% variable

Setup an environment variable for the computer DDK=<ddk_install_path>
where DDK install path is the location you installed the DDK (probably 
something like C:\WinDDK\7600.16385.1

I've noticed you need to build twice after a clean, I believe some of the builds do
not happen in the correct order but haven't figured this out yet.  On the first build
after clean one build fails, all subsequent builds work.  It looks like pe2boot needs
to be built before boot_hook.

