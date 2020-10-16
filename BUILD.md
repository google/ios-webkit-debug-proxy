# How to build iOS Debug Proxy from source

1. Download [MSYS2 32bits](http://www.msys2.org/)

2. Install MSYS2 by following the 6 steps on their Homepage

3. Terminate the terminal via Task Manager after running `pacman -Syu`

4. **(Optional)** Disable Windows Defender's real time protection to speed up

5. Open MSYS **MingGW 32-Bit** terminal

6. Install 32 bit toolchain (all packages):
    * `pacman -S mingw-w64-i686-gcc mingw-w64-i686-libtool mingw-w64-i686-pkg-config`

7. Install additional tools
    * `pacman -S automake-wrapper autoconf make unzip patch`

8. Restart MSYS **MingGW 32-Bit** terminal

9. Go to /home/
    * `cd /home/`

10. Download projects
    * `wget https://github.com/openssl/openssl/archive/OpenSSL_1_1_1.zip -O openssl.zip`
    * `wget https://github.com/libimobiledevice/libplist/archive/2.2.0.zip -O libplist.zip`
    * `wget https://github.com/libimobiledevice/libusbmuxd/archive/2.0.2.zip -O libusbmuxd.zip`
    * `wget https://github.com/libimobiledevice/libimobiledevice/archive/1.3.0.zip -O libimobiledevice.zip`
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
    * `cd /home/libplist-2.2.0/`

13. Build library
    * `./autogen.sh --without-cython`
    * `make install -j4`

14. Go to **libusbmuxd** folder
    * `cd /home/libusbmuxd-2.0.2/`

15. Build library (apply workaround for build error: https://github.com/libimobiledevice/libusbmuxd/issues/95)
	* `wget https://raw.githubusercontent.com/Orbif/libimobiledevice-patchs/master/libimobiledevice-socket-mingw-compatibility.patch`
	* `patch -p 1 < libimobiledevice-socket-mingw-compatibility.patch`
    * `./autogen.sh`
    * `make install -j4`

16. Go to **openssl** folder
    * `cd /home/openssl-OpenSSL_1_1_1`

17. Build library
    * `./Configure --prefix=/mingw32 no-idea no-mdc2 no-rc5 shared mingw`
    * `make -j4`
    * `make install_sw -j4`

18. Go to **libimobiledevice** folder
    * `cd /home/libimobiledevice-1.3.0/`

19. Build library (apply workaround for build error: https://github.com/libimobiledevice/libusbmuxd/issues/95)
	* `wget https://raw.githubusercontent.com/Orbif/libimobiledevice-patchs/master/libimobiledevice-socket-mingw-compatibility.patch`
	* `patch -p 1 < libimobiledevice-socket-mingw-compatibility.patch`
    * `./autogen.sh --without-cython`
    * `make install -j4`

20. Go to **pcre** folder
    * `cd /home/pcre-8.43/`

21. Build library
    * `./configure`
    * `make install -j4`

22. Go to **ios-webkit-debug-proxy** folder
    * `cd /home/ios-webkit-debug-proxy-outsystems/`

23. Check if the following changes are commited
    ```diff
    diff --git a/src/ios_webkit_debug_proxy_main.c b/src/ios_webkit_debug_proxy_main.c
    index f02733e..588bfc7 100644
    --- a/src/ios_webkit_debug_proxy_main.c
    +++ b/src/ios_webkit_debug_proxy_main.c
    @@ -231,6 +231,7 @@ int iwdpm_configure(iwdpm_t self, int argc, char **argv) {
        {"debug", 0, NULL, 'd'},
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
    +    {"no-buffer", 0, NULL, 'B'},
        {NULL, 0, NULL, 0}
    };
    const char *DEFAULT_CONFIG = "null:9221,:9222-9322";
    @@ -292,6 +293,10 @@ int iwdpm_configure(iwdpm_t self, int argc, char **argv) {
        case 'd':
            self->is_debug = true;
            break;
    +      case 'B':
    +        setbuf(stdout, NULL);
    +        setbuf(stderr, NULL);
    +        break;
        default:
            ret = 2;
            break;
    ```

    ```diff
    diff --git a/src/socket_manager.c b/src/socket_manager.c
    index c43b53f..807f2d9 100755
    --- a/src/socket_manager.c
    +++ b/src/socket_manager.c
    @@ -96,7 +96,7 @@ int sm_listen(int port) {
    }
    struct sockaddr_in local;
    local.sin_family = AF_INET;
    -  local.sin_addr.s_addr = INADDR_ANY;
    +  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(port);
    int ra = 1;
    u_long nb = 1;
    @@ -117,7 +117,7 @@ int sm_listen(int port) {
    int opts = fcntl(fd, F_GETFL);
    struct sockaddr_in local;
    local.sin_family = AF_INET;
    -  local.sin_addr.s_addr = INADDR_ANY;
    +  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(port);
    int ra = 1;
    int nb = 1;
    ```

24. Build library
    * `./autogen.sh`
    * `make -j4`

25. Use **exe** generated in **.libs** folder, and get all other necessary **dll’s**
    * `C:\msys64\home\ios-webkit-debug-proxy-outsystems\src\.libs\ios_webkit_debug_proxy.exe`
    * `C:\msys64\home\openssl-OpenSSL_1_1_1\libcrypto-1_1.dll`
    * `C:\msys64\home\openssl-OpenSSL_1_1_1\libssl-1_1.dll`
    * `C:\msys64\home\libimobiledevice-1.3.0\src\.libs\libimobiledevice-1.0.dll`
    * `C:\msys64\home\pcre-8.43\.libs\libpcre-1.dll`
    * `C:\msys64\home\pcre-8.43\.libs\libpcreposix-0.dll`
    * `C:\msys64\home\libplist-2.2.0\src\.libs\libplist-2.0.dll`
    * `C:\msys64\home\libusbmuxd-2.0.2\src\.libs\libusbmuxd-2.0.dll`
    * `C:\msys64\mingw32\bin\libgcc_s_dw2-1.dll`
    * `C:\msys64\mingw32\bin\libwinpthread-1.dll`

---

Source: [https://docs.google.com/document/d/1oLee6DqWDdeoi_k-_PV8KNETkJW3h7aOBV1UhMUG_HU/edit?usp=sharing](https://docs.google.com/document/d/1oLee6DqWDdeoi_k-_PV8KNETkJW3h7aOBV1UhMUG_HU/edit?usp=sharing)