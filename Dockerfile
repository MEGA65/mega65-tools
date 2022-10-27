FROM ubuntu:focal

USER root
RUN apt update && apt install -y libusb-1.0-0-dev libgif-dev libpng-dev mingw-w64-common libz-mingw-w64-dev libgtest-  dev libgmock-dev libpng-mingw-w64 libusb-1.0-0-mingw-w64 cc65 wget
RUN wget gurce.net/ubuntu/pool/main/libp/libpng-mingw-w64/libpng-mingw-w64_1.6.37-1_amd64.deb && sudo dpkg -i libpng-mingw-w64_1.6.37-1_amd64.deb
RUN wget http://gurce.net/ubuntu/libusb-mingw-w64_1.0.24-1_amd64.deb && sudo dpkg -i libusb-mingw-w64_1.0.24- 1_amd64.deb

