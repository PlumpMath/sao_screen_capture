# sao_screen_capture
Single header file c/cpp library for real time screen capture.

Currenly OSX only.
I'll probably get to Windows at some point but also open to contributions for windows or any other platforms.

## Usage
Library is implemented as a single header file with a c api to be easily included in any project.

`#define SAO_SCREEN_CAPTURE_IMPLEMENTATION` in *ONE* c/cpp file before including the header to instantiate the implementation.
An example of how to use it can be found in test_sao_screen_capture.c.

You have to link in the osx/cocoa frameworks that are necessesary for this to work.
`-framework CoreGraphics -framework IOSurface -framework CoreFoundation`

See test_screen_capture.c for an example use. Works like this.

* Set up a capture queue by calling `sc_allocate`. You can then read info about the screen and capture from the struct.
* Call `sc_startCapture(cq)` to start capturing.
* Poll for new screen images by calling `sc_aquireNextFrame(cq)`.
* If you get a new frame you can copy view the screen data, once you are done with the frame call `sc_releaseFrame(cq, frame)` to release the memory back to the capture_queue.
