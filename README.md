# Camera-record-app

## Requires

* opencv 3.4.16
*     Yes. It requires image codec, video io and highgui(this one is just for debug).
* libuvc.
* NanoSDK. commitid. e5516c1b606fa62b83b67758b58ec60a78c16e05

## Start

```
mkdir -p build && cd build
cmake ..
make -j8
sudo ./camerarecord # Access camera device need sudo
```
