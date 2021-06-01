# How to build iOS Debug Proxy from source

**NOTE**
If you're using an ARM machine (M1 processor, etc) all the steps must be executed on 
an emulated x86 Terminal.
How to 'create' a x86 Terminal: https://osxdaily.com/2020/11/18/how-run-homebrew-x86-terminal-apple-silicon-mac/

1. Open the (x86) Terminal

2. Check that the correct architecture (`i386`) is being used
    * `arch`

3. Navigate to the home folder
    * `cd ~`

4. Create a folder `build_ios_proxy` and navigate to it
    * `mkdir ~/build_ios_proxy`
    * `cd ~/build_ios_proxy`

5. Install Homebrew
    * `mkdir ~/build_ios_proxy/homebrew && curl -L https://github.com/Homebrew/brew/tarball/master | tar xz --strip 1 -C homebrew`

6. Add Homebrew to `PATH`
    * `export PATH="$HOME/build_ios_proxy/homebrew/bin:$PATH"`

7. Install additional tools
    * `brew install autoconf automake libtool pkg-config`

8. Create folder where the projects will be install
    * `mkdir ~/build_ios_proxy/install_folder`

9. Create `PKG_CONFIG_PATH` with the installation folder
    * `export PKG_CONFIG_PATH="$HOME/build_ios_proxy/install_folder/lib/pkgconfig"`

10. Download the projects
    * `curl -L https://github.com/openssl/openssl/archive/OpenSSL_1_1_1.zip -o openssl.zip`
    * `curl -L https://github.com/libimobiledevice/libplist/archive/2.2.0.zip -o libplist.zip`
    * `curl -L https://github.com/libimobiledevice/libusbmuxd/archive/2.0.2.zip -o libusbmuxd.zip`
    * `curl -L https://github.com/libimobiledevice/libimobiledevice/archive/1.3.0.zip -o libimobiledevice.zip`
    * `curl -L https://github.com/OutSystems/ios-webkit-debug-proxy/archive/outsystems.zip -o ios-webkit-debug-proxy.zip`
    * `curl -L https://ftp.pcre.org/pub/pcre/pcre-8.43.zip -o pcre.zip`

11. Extract the projects
    * `unzip openssl.zip`
    * `unzip libplist.zip`
    * `unzip libusbmuxd.zip`
    * `unzip libimobiledevice.zip`
    * `unzip ios-webkit-debug-proxy.zip`
    * `unzip pcre.zip`

12. Go to **libplist** folder
    * `cd ~/build_ios_proxy/libplist-2.2.0/`

13. Build library
    * `./autogen.sh --without-cython --prefix=$HOME/build_ios_proxy/install_folder`
    * `make install -j4`

14. Go to **libusbmuxd** folder
    * `cd ~/build_ios_proxy/libusbmuxd-2.0.2/`

15. Build library
    * `./autogen.sh --prefix=$HOME/build_ios_proxy/install_folder`
    * `make install -j4`

16. Go to **openssl** folder
    * `cd ~/build_ios_proxy/openssl-OpenSSL_1_1_1`

17. Build library
    * `./config --prefix=$HOME/build_ios_proxy/install_folder no-idea no-mdc2 no-rc5 shared`
    * `make -j4`
    * `make install_sw -j4`

18. Go to **libimobiledevice** folder
    * `cd ~/build_ios_proxy/libimobiledevice-1.3.0/`

19. Build library
    * `./autogen.sh --without-cython --prefix=$HOME/build_ios_proxy/install_folder`
    * `make install -j4`

20. Go to **pcre** folder
    * `cd ~/build_ios_proxy/pcre-8.43/`

21. Build library
    * `./configure --prefix=$HOME/build_ios_proxy/install_folder`
    * `make install -j4`

22. Go to **ios-webkit-debug-proxy** folder
    * `cd ~/build_ios_proxy/ios-webkit-debug-proxy-outsystems/`

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
    * `./autogen.sh --prefix=$HOME/build_ios_proxy/install_folder`
    * `make -j4`

25. Create a directory to hold necessary files and navigate to it
    * `mkdir ~/build_ios_proxy/bundle`
    * `cd ~/build_ios_proxy/bundle`

26. Copy **ios_webkit_debug_proxy** generated in **src** folder, and get all other necessary **dylib**
    * `cp ~/build_ios_proxy/ios-webkit-debug-proxy-outsystems/src/ios_webkit_debug_proxy .`
    * `cp ~/build_ios_proxy/install_folder/lib/libcrypto.1.1.dylib .`
    * `cp ~/build_ios_proxy/install_folder/lib/libimobiledevice-1.0.6.dylib .`
    * `cp ~/build_ios_proxy/install_folder/lib/libplist-2.0.3.dylib .`
    * `cp ~/build_ios_proxy/install_folder/lib/libssl.1.1.dylib .`
    * `cp ~/build_ios_proxy/install_folder/lib/libusbmuxd-2.0.6.dylib .`

27. Change binary files to look for dependencies on the executable's folder
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libimobiledevice-1.0.6.dylib @executable_path/libimobiledevice-1.0.6.dylib ios_webkit_debug_proxy`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libusbmuxd-2.0.6.dylib @executable_path/libusbmuxd-2.0.6.dylib ios_webkit_debug_proxy`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libplist-2.0.3.dylib @executable_path/libplist-2.0.3.dylib ios_webkit_debug_proxy`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libssl.1.1.dylib @executable_path/libssl.1.1.dylib ios_webkit_debug_proxy`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libcrypto.1.1.dylib @executable_path/libcrypto.1.1.dylib ios_webkit_debug_proxy`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libssl.1.1.dylib @executable_path/libssl.1.1.dylib libimobiledevice-1.0.6.dylib`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libcrypto.1.1.dylib @executable_path/libcrypto.1.1.dylib libimobiledevice-1.0.6.dylib`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libusbmuxd-2.0.6.dylib @executable_path/libusbmuxd-2.0.6.dylib libimobiledevice-1.0.6.dylib`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libplist-2.0.3.dylib @executable_path/libplist-2.0.3.dylib libimobiledevice-1.0.6.dylib`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libcrypto.1.1.dylib @executable_path/libcrypto.1.1.dylib libssl.1.1.dylib`
    * `install_name_tool -change $HOME/build_ios_proxy/install_folder/lib/libplist-2.0.3.dylib @executable_path/libplist-2.0.3.dylib libusbmuxd-2.0.6.dylib`
