# How to build iOS Debug Proxy from source

1. Download [MSYS2 32bits](http://www.msys2.org/)

2. Install MSYS2 by following the 6 steps on their Homepage

3. Terminate the terminal via Task Manager after running `pacman -Syu`

4. **(Optional)** Disable Windows Defender's real time protection to speed up

5. Open MSYS **MingGW 32-Bit** terminal

6. Install 32 bit toolchain (all packages):
    * `pacman -S mingw-w64-i686-gcc mingw-w64-i686-libtool mingw-w64-i686-pkg-config`

7. Install additional tools
    * `pacman -S automake-wrapper autoconf make unzip`

8. Restart MSYS **MingGW 32-Bit** terminal

9. Go to /home/
    * `cd /home/`

10. Download projects
    * `wget https://github.com/openssl/openssl/archive/OpenSSL_1_1_1.zip -O openssl.zip`
    * `wget https://github.com/libimobiledevice/libplist/archive/2.0.0.zip -O libplist.zip`
    * `wget https://github.com/libimobiledevice/libusbmuxd/archive/b097ea39f391f5c2c83d8f4687843a3634f7cd54.zip -O libusbmuxd.zip`
    * `wget https://github.com/libimobiledevice/libimobiledevice/archive/a9e69b1252e5918b6d8ada1209ccefde301cfa26.zip -O libimobiledevice.zip`
    * `wget https://github.com/OutSystems/ios-webkit-debug-proxy/archive/outsystems.zip -O ios-webkit-debug-proxy.zip`
    * `wget https://ftp.pcre.org/pub/pcre/pcre-8.43.zip -O pcre.zip`

11. Extract projects
    * `unzip openssl.zip`
    * `unzip libplist.zip`
    * `unzip libusbmuxd.zip`
    * `unzip libimobiledevice.zip`
    * `unzip ios-webkit-debug-proxy.zip`
    * `unzip pcre.zip`

12. Go to **libplist** folder
    * `cd /home/libplist-2.0.0/`

13. Build library
    * `./autogen.sh --without-cython`
    * `make install -j4`

14. Go to **libusbmuxd** folder
    * `cd /home/libusbmuxd-b097ea39f391f5c2c83d8f4687843a3634f7cd54/`

15. Build library
    * `./autogen.sh`
    * `make install -j4`

16. Go to **openssl** folder
    * `cd /home/openssl-OpenSSL_1_1_1`

17. Build library
    * `./Configure --prefix=/mingw32 no-idea no-mdc2 no-rc5 shared mingw`
    * `make -j4`
    * `make install_sw -j4`

18. Go to **libimobiledevice** folder
    * `cd /home/libimobiledevice-a9e69b1252e5918b6d8ada1209ccefde301cfa26/`

19. Build library
    * `./autogen.sh --without-cython`
    * `make install -j4`

20. Go to **pcre** folder
    * `cd /home/pcre-8.43/`

21. Build library
    * `./configure`
    * `make install -j4`

22. Go to **ios-webkit-debug-proxy** folder
    * `cd /home/ios-webkit-debug-proxy-outsystems/`

_**Warning:** If you are using OutSystems Github fork you don’t need to apply these changes and you can skip to Build library step._

23. Edit **socket_manager.c**
    * Open `C:\msys32\home\ios-webkit-debug-proxy-outsystems\src\socket_manager.c`
    * Edit line 120 from:
        * `local.sin_addr.s_addr = INADDR_ANY;`
    * To:
        * `local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);`

24. Edit **ios_webkit_debug_proxy_main.c**
    * Open `C:\msys32\home\ios-webkit-debug-proxy-outsystems\src\ios_webkit_debug_proxy_main.c`
    * Add the following lines to the start of the main method:
        * `setbuf(stdout, NULL);`
        * `setbuf(stderr, NULL);`

25. Build library
    * `./autogen.sh`
    * `make -j4`

26. Use **exe** generated in **.libs** folder, and get all other necessary **dll’s**
    * `C:\msys32\home\ios-webkit-debug-proxy-outsystems\src\.libs\ios_webkit_debug_proxy.exe`
    * `C:\msys32\home\openssl-OpenSSL_1_1_1\libcrypto-1_1.dll`
    * `C:\msys32\home\openssl-OpenSSL_1_1_1\libssl-1_1.dll`
    * `C:\msys32\home\libimobiledevice-a9e69b1252e5918b6d8ada1209ccefde301cfa26\src\.libs\libimobiledevice.dll`
    * `C:\msys32\home\pcre-8.43\.libs\libpcre-1.dll`
    * `C:\msys32\home\pcre-8.43\.libs\libpcreposix-0.dll`
    * `C:\msys32\home\libplist-2.0.0\src\.libs\libplist.dll`
    * `C:\msys32\home\libusbmuxd-b097ea39f391f5c2c83d8f4687843a3634f7cd54\src\.libs\libusbmuxd.dll`

---

Source: [https://docs.google.com/document/d/1oLee6DqWDdeoi_k-_PV8KNETkJW3h7aOBV1UhMUG_HU/edit?usp=sharing](https://docs.google.com/document/d/1oLee6DqWDdeoi_k-_PV8KNETkJW3h7aOBV1UhMUG_HU/edit?usp=sharing)