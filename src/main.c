/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (C) 2010-2012 Ken Tossell
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/
#include <stdio.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/videoio/videoio_c.h>

#include "libuvc/libuvc.h"

CvVideoWriter *wr = NULL;
int preview_req_cnt = 0;

const char *preview_path = "/home/wangha/Documents/git/camera-goweb-app/images/preview.jpg";

void
writepreview(uvc_frame_t *frame) {
  FILE *fp;
  switch (frame->frame_format) {
  case UVC_FRAME_FORMAT_H264:
    printf("IOS Format photo, not supported\n");
    /* use `ffplay H264_FILE` to play */
    /* fp = fopen(H264_FILE, "a");
     * fwrite(frame->data, 1, frame->data_bytes, fp);
     * fclose(fp); */
    break;
  case UVC_COLOR_FORMAT_MJPEG:
    fp = fopen(preview_path, "w");
    fwrite(frame->data, 1, frame->data_bytes, fp);
    fclose(fp);
    break;
  case UVC_COLOR_FORMAT_YUYV:
    /* Do the BGR conversion */
    printf("YUYV Format photo, not supported\n");
	break;
	/*
    ret = uvc_any2bgr(frame, bgr);
    if (ret) {
      uvc_perror(ret, "uvc_any2bgr");
      uvc_free_frame(bgr);
      return;
    }
    break;
	*/
  default:
    printf("Unknown Format photo, not supported\n");
    break;
  }
}

void cb(uvc_frame_t *frame, void *ptr) {
  uvc_frame_t *bgr;
  uvc_error_t ret;
  IplImage* cvImg;

  // printf("callback! length = %u, ptr = %d\n", frame->data_bytes, (int) ptr);

  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    printf("unable to allocate bgr frame!");
    return;
  }

  if (preview_req_cnt > 0) {
    writepreview(frame);
    preview_req_cnt --;
  }

  ret = uvc_any2bgr(frame, bgr);
  if (ret) {
    uvc_perror(ret, "uvc_any2bgr");
    uvc_free_frame(bgr);
    return;
  }

  cvImg = cvCreateImageHeader(
      cvSize(bgr->width, bgr->height),
      IPL_DEPTH_8U,
      3);

  cvSetData(cvImg, bgr->data, bgr->width * 3);

  if (wr == NULL) {
    //wr = cvCreateVideoWriter("test.mp4", CV_FOURCC_DEFAULT,
    wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('M','J','P','G'),
    //wr = cvCreateVideoWriter("test1.avi", CV_FOURCC('X','V','I','D'),
      25, cvSize(bgr->width, bgr->height), true);
	if (!wr)
		printf("Error in create cv video writer.\n");
  }
  cvWriteFrame(wr, cvImg);

  /*
   * For Debug
  cvNamedWindow("Test", CV_WINDOW_AUTOSIZE);
  cvShowImage("Test", cvImg);
  cvWaitKey(10);
  */

  cvReleaseImageHeader(&cvImg);

  uvc_free_frame(bgr);
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_error_t res;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;

  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");

  res = uvc_find_device(
      ctx, &dev,
      0, 0, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_find_device");
  } else {
    puts("Device found");

    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open");
    } else {
      puts("Device opened");

      uvc_print_diag(devh, stderr);

      res = uvc_get_stream_ctrl_format_size(
          devh, &ctrl, UVC_FRAME_FORMAT_YUYV, 640, 480, 30
      );

      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode");
      } else {
        res = uvc_start_streaming(devh, &ctrl, cb, 12345, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming");
        } else {
          puts("Streaming for 10 seconds...");
          //uvc_error_t resAEMODE = uvc_set_ae_mode(devh, 1);
          uvc_error_t resAEMODE = uvc_set_ae_mode(devh, 8);
          uvc_perror(resAEMODE, "set_ae_mode");
          int i;
          /* 
          for (i = 1; i <= 10; i++) {
            uvc_error_t resPT = uvc_set_pantilt_abs(devh, i * 20 * 3600, 0);
            uvc_perror(resPT, "set_pt_abs");
            uvc_error_t resEXP = uvc_set_exposure_abs(devh, 20 + i * 50);
            uvc_perror(resEXP, "set_exp_abs");

            sleep(1);
          }
		  */
          sleep(120);
          uvc_stop_streaming(devh);
		  if (wr) {
			  cvReleaseVideoWriter(&wr);
		  }
          puts("Done streaming.");
        }
      }

      uvc_close(devh);
      puts("Device closed");
    }

    uvc_unref_device(dev);
  }

  uvc_exit(ctx);
  puts("UVC exited");

  return 0;
}

