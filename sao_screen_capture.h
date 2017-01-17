// Screen Capture (OSX) - v1.0 - public domain Stephen Olsen 2016
// Real time screen capture.

#ifndef _INCLUDE_SAO_SCREEN_CAPTURE_H
#define _INCLUDE_SAO_SCREEN_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> // uint types.
#include <string.h> // memcpy

// Types
typedef struct sc_frame {
    struct sc_frame* next_frame;
    
    long int capture_time;

    uint8_t *data;
    uint32_t data_size;
} sc_Frame;

typedef struct {
    int capture_width;
    int capture_height;
    int pixel_components;

    sc_Frame* frames_storage;
    uint8_t* frames_data_storage;

    sc_Frame* head;
    sc_Frame* free_frames;

    void* platform_data;
} sc_CaptureQueue;

// Api
sc_CaptureQueue* sc_allocate();
void sc_free(sc_CaptureQueue* cq);
void sc_startCapture(sc_CaptureQueue* cq);
void sc_stopCapture(sc_CaptureQueue* cq);
sc_Frame* sc_aquireNextFrame(sc_CaptureQueue* cq);
void sc_releaseFrame(sc_CaptureQueue* cq, sc_Frame *frame);

#ifdef __cplusplus
}
#endif

#endif //_INCLUDE_SAO_SCREEN_CAPTURE_H

#ifdef SAO_SCREEN_CAPTURE_IMPLEMENTATION

#ifdef __APPLE__

#include <dispatch/dispatch.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurface.h>
#include <libkern/OSAtomic.h>

//@Improvement: Allow passing in memory or allocators.
sc_CaptureQueue* sc_allocate()
{
    // Get screen stats.
    CGDirectDisplayID display_id = 0; //@Improvement: Support multiple monitors.

    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);

    int capture_width = CGDisplayModeGetPixelWidth(mode);
    int capture_height = CGDisplayModeGetPixelHeight(mode);
    int pixel_components = 4;

    CGDisplayModeRelease(mode);
    
    sc_CaptureQueue* cq = (sc_CaptureQueue*)malloc(sizeof(*cq));

    int num_allocated_frames = 5; //@TODO: Think about this or let it be variable.
    int bytes_per_frame = capture_width * capture_height * pixel_components;
    cq->capture_width = capture_width;
    cq->capture_height = capture_height;
    cq->pixel_components = pixel_components;
    
    cq->frames_storage = (sc_Frame*)calloc(1, sizeof(*cq->frames_storage) * num_allocated_frames);
    // @TODO: At least align if we have to, i'm not totally sure, I think I'd need to do stuff if it's not 4 components.
    cq->frames_data_storage = (uint8_t*)malloc(bytes_per_frame * num_allocated_frames); //@Improvement: Pad each frame to a cache line boundry.

    // Push all frames onto the free frames list.
    sc_Frame *last_frame = NULL;

    for (int frame_index = 0; frame_index < num_allocated_frames; frame_index++) {
        sc_Frame *frame = cq->frames_storage + frame_index;
        frame->next_frame = last_frame;
        frame->data_size = bytes_per_frame;
        frame->data = cq->frames_data_storage + (frame_index * bytes_per_frame); //@TODO: If I have to align above handle it here too.

        last_frame = frame;
    }

    cq->head = NULL;
    cq->free_frames = last_frame;

    cq->platform_data = (void*)malloc(sizeof(CGDisplayStreamRef));

    OSMemoryBarrier();

    return cq;
}

void sc_free(sc_CaptureQueue* cq)
{
    free(cq->frames_data_storage);
    free(cq->frames_storage);
    free(cq->platform_data);
    free(cq);
}

void sc_startCapture(sc_CaptureQueue* cq)
{
    // This is actually objective c syntax but clang on osx is fine with embedding this in c/cpp.
    // For this screen capture api we have to pass a closure to the runtime which includes a callback
    // that gets called every time the screen updates.
    // This executes on some os managed thread and not our programs thread which is why we need the threadsafe
    // queue to communicate the data back to our main thread.
    void(^handler)(CGDisplayStreamFrameStatus,
                   uint64_t,
                   IOSurfaceRef,
                   CGDisplayStreamUpdateRef) = ^void(CGDisplayStreamFrameStatus status,
                                                     uint64_t time,
                                                     IOSurfaceRef frame,
                                                     CGDisplayStreamUpdateRef update)
    {
        if (status == kCGDisplayStreamFrameStatusFrameComplete &&
            frame != NULL) {
            IOSurfaceLock(frame, kIOSurfaceLockReadOnly, NULL);

            uint8_t* data = (uint8_t*)IOSurfaceGetBaseAddress(frame);

            if (data != NULL) {
                // Aquire a new frame to write to.
                sc_Frame* frame = NULL;

                while (!frame) {
                    sc_Frame *try_frame = cq->free_frames;
                    if (!try_frame) {
                        // Error: No free frames to write to.
                        // @Improvement: Have some way to notify the main app of this.
                        break;
                    }

                    if (OSAtomicCompareAndSwapPtrBarrier((void*)try_frame,
                                                         (void*)try_frame->next_frame,
                                                         (void*volatile*)&cq->free_frames)) {
                        frame = try_frame;
                    }

                }


                if (frame) {
                    // Copy data.
                    frame->next_frame = NULL;
                    // @TODO: Capture time.
                    frame->capture_time = 0;
                    memcpy(frame->data, data, frame->data_size);

                    // Publish frame to queue.
                    frame->next_frame = NULL;

                    OSMemoryBarrier();

                    int published = 0;
                    while (!published) {
                        sc_Frame **tail = &cq->head;

                        while(*tail != NULL) {
                            tail = &((*tail)->next_frame);
                        }

                        // Try to add our frame to the end.
                        if (OSAtomicCompareAndSwapPtrBarrier((void*)NULL,
                                                             (void*)frame,
                                                             (void*volatile*)tail)) {
                            published = 1;
                        }
                    }
                }
            }

            IOSurfaceUnlock(frame, kIOSurfaceLockReadOnly, NULL);
        }
    };

    dispatch_queue_t q;
    q = dispatch_queue_create("sao_screen_capture_q", NULL);

    CGDirectDisplayID display_id = 0; //@NOTE: If we support multiple screens use that info here.

    *(CGDisplayStreamRef*)cq->platform_data = CGDisplayStreamCreateWithDispatchQueue(display_id,
                                                                                    cq->capture_width,
                                                                                    cq->capture_height,
                                                                                    'BGRA', //@IMPROVEMENT: Support different color formats.
                                                                                    NULL,
                                                                                    q,
                                                                                    handler);
    CGDisplayStreamStart(*(CGDisplayStreamRef*)cq->platform_data);
}

void sc_stopCapture(sc_CaptureQueue* cq)
{
    CGDisplayStreamStop(*(CGDisplayStreamRef*)cq->platform_data);
}

sc_Frame* sc_aquireNextFrame(sc_CaptureQueue* cq)
{
    // @TODO: This works fine for single comsumer, for multiple consumber
    // we want to also have this atomically pop it.
    sc_Frame* frame = cq->head;
    return frame;
}

void sc_releaseFrame(sc_CaptureQueue* cq, sc_Frame *frame)
{
    // @TODO: Think about ABA.
    cq->head = frame->next_frame;

    for (;;) {
        sc_Frame* old_free_head = cq->free_frames;
        frame->next_frame = old_free_head;

        if (OSAtomicCompareAndSwapPtrBarrier((void*)old_free_head,
                                             (void*)frame,
                                             (void*volatile*)&cq->free_frames)) {
            // frame appended to freelist
            break;
        }
    }
}


#endif //__APPLE__

#endif //SAO_SCREEN_CAPTURE_IMPLEMENTATION
